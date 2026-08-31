#pragma once
// forgevcv::ForgeModule — reusable rack::Module base for ForgeSeries firmwares.
//
// Holds everything a hosted firmware needs that is NOT specific to a particular
// module's port layout or parameter set: the engine handle, the framebuffer, the
// two host-side settings (CV input range, encoder sensitivity), the UI->audio
// encoder event queue, the control-rate engine step + shared JSON state, and the
// Initialize/Randomize module actions.
//
// A firmware's module subclasses this, declares its own Input/Output/Param/Light
// enums, constructs a concrete IEngine into `engine`, and implements process()
// as: gather CV (via mapCvInput) + clock level -> stepEngine(...) -> write
// outHold to its outputs -> updateOutputLights(...) for the panel LEDs.
// dataToJson/dataFromJson call baseToJson/baseFromJson.

#include <rack.hpp>

#include "IEngine.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace forgevcv {
using namespace rack;

// Maximum DAC channels a ForgeSeries module can drive: the board's own
// MCP4728, plus a second one on an expander. Modules without expander support
// simply pass their own nOut and never touch the upper half.
static const int FORGE_MAX_OUT = 8;

// ── Panel LED transfer curve ─────────────────────────────────────────────────
// Taken from the hardware rather than invented: each output pin drives a BC547
// base through 100k, emitter to ground, with the LED and a 330R series resistor
// hanging off the collector on the 3.3 V rail. Two numbers fall out of that, and
// they are the whole model.
//
// Below the base-emitter drop the transistor is off, so the LED is dark. Above
// it, collector current is beta * (V - Vbe) / 100k — but the collector branch
// can only ever pass (3.3 V - Vf - Vce_sat) / 330R, about 3.5 mA, which that
// reaches at roughly 1.9 V. From there to the 5 V top of the range the LED is
// simply saturated: an output at 2 V and one at 5 V look the same on the panel.
//
// The saturation point moves with the transistor's gain (~2.5 V at hFE 200,
// ~1.5 V at 450, so 1.9 V is the middle of a BC547B), which is inherent to a
// common-emitter driver and not worth chasing. Refitting the base resistor is
// what would move it far: the value is beta * (5 V - Vbe) / Rbase-ish, so a 330k
// base would push it to the top of the range and make the LED track the whole
// 0..5 V span.
static const float FORGE_LED_ON_VOLTS = 0.7f;   // Vbe — dark below this
static const float FORGE_LED_FULL_VOLTS = 1.9f; // saturated above this

struct ForgeModule : Module {
    // Owned engine handle; the subclass creates it, this base deletes it.
    IEngine *engine = nullptr;

    // Latest framebuffer (written in process on the audio thread, read in draw).
    uint8_t fb[1024] = {0};

    // ── Host-side settings (exposed via the plugin's context menu) ────────────
    // CV input range: firmware works in a 0..5 V (0..4095) domain. Non-unipolar
    // modes linearly remap the incoming voltage onto that full range before it
    // reaches the engine (a hardware-impossible host convenience).
    enum CvRange { CV_UNIPOLAR,
                   CV_BIPOLAR,
                   CV_0TO10V };
    int cvRange = CV_UNIPOLAR;

    // Encoder drag sensitivity (pixels per detent); lower = more sensitive.
    enum EncSensitivity { ENC_LOW,
                          ENC_MEDIUM,
                          ENC_HIGH };
    int encoderSensitivity = ENC_MEDIUM;

    // ── UI -> audio encoder events (widget thread -> process on audio thread) ──
    std::atomic<int> encDelta{0};
    std::atomic<int> encClick{0};

    // Control-rate decimation: run the engine every engineDecim audio samples and
    // hold outputs in between. Default 8 keeps every clock edge cheaply (PPQN tick
    // rate peaks ~4.8 kHz at 300 BPM; 44.1 kHz / 8 -> ~5.5 kHz engine rate).
    int engineDecim = 8;
    float outHold[FORGE_MAX_OUT] = {0};
    int decim = 0;

    ~ForgeModule() override { delete engine; }

    // Map a raw input voltage to the engine's 0..5 V domain per the range mode.
    float mapCvInput(float v) const {
        if (cvRange == CV_BIPOLAR)
            v = (v + 5.f) * 0.5f; // -5..+5 V -> 0..5 V (linear, full range)
        else if (cvRange == CV_0TO10V)
            v = v * 0.5f;         // 0..10 V -> 0..5 V (linear, full range)
        return clamp(v, 0.f, 5.f);
    }

    // Encoder pixels-per-detent for the current sensitivity setting.
    float encoderPixelsPerDetent() const {
        switch (encoderSensitivity) {
        case ENC_LOW:
            return 30.f;
        case ENC_HIGH:
            return 10.f;
        default:
            return 20.f;
        }
    }

    // Drain queued encoder events and advance the engine at control rate. The
    // subclass supplies CV already mapped via mapCvInput, the clock level, and the
    // number of outputs; results land in outHold[0..nOut-1] and fb.
    void stepEngine(float sampleTime, const float *cv, int nCv, bool clockHigh, int nOut) {
        if (!engine)
            return;

        int d = encDelta.exchange(0);
        if (d)
            engine->encoderTurn(d);
        int c = encClick.exchange(0);
        for (int i = 0; i < c; i++) {
            engine->encoderButton(true);
            engine->encoderButton(false);
        }

        if (++decim >= engineDecim) {
            decim = 0;
            float dt = engineDecim * sampleTime;
            engine->process(dt, cv, nCv, clockHigh, outHold, nOut);
            engine->getFramebuffer(fb);
        }
    }

    // ── Output LEDs ──────────────────────────────────────────────────────────
    // Mirror the outputs onto the panel LEDs. On the hardware an LED hangs off
    // each output pin through a transistor, so it is not something the firmware
    // drives: it glows with whatever voltage the DAC is putting out. Emulate the
    // driver's actual transfer curve (see FORGE_LED_ON_VOLTS above) rather than
    // brightness-equals-volts, so a gate blinks, a disabled output sits dark,
    // and an envelope shows only the part of its shape the real LED resolves —
    // the bottom two volts — instead of a fade the panel never performs.
    //
    // Call once per process() with args.sampleTime, passing the first of a
    // contiguous run of light ids (LED1_LIGHT). The smoothing slows only the
    // *fall*: a rise applies immediately, so a trigger shorter than a frame
    // still lights the LED fully and then decays, the way the real LED and the
    // eye do between frames. It reads outHold, so an LED follows its jack
    // whether or not anything is patched into it — as on the hardware.
    void updateOutputLights(int firstLight, int nOut, float sampleTime) {
        for (int i = 0; i < nOut && i < FORGE_MAX_OUT; i++)
            lights[firstLight + i].setBrightnessSmooth(
                outputLedBrightness(outHold[i]), sampleTime);
    }

    // The curve on its own, for a module whose jacks are not driven from outHold
    // — ForgeView's buffered pass-throughs, say. The hardware LED senses the jack
    // net rather than the DAC pin, so feeding it the voltage actually leaving the
    // module is the faithful thing either way.
    static float outputLedBrightness(float volts) {
        const float span = FORGE_LED_FULL_VOLTS - FORGE_LED_ON_VOLTS;
        return clamp((volts - FORGE_LED_ON_VOLTS) / span, 0.f, 1.f);
    }

    // Serialize the shared state (EEPROM blob + host settings) into a patch. The
    // subclass creates the root object, calls this, then adds its own keys.
    void baseToJson(json_t *root) {
        if (engine) {
            std::string blob = engine->serialize();
            json_t *arr = json_array();
            for (unsigned char ch : blob)
                json_array_append_new(arr, json_integer(ch));
            json_object_set_new(root, "eeprom", arr);
        }
        json_object_set_new(root, "cvRange", json_integer(cvRange));
        json_object_set_new(root, "encoderSensitivity", json_integer(encoderSensitivity));
    }

    // ── Rack module actions ──────────────────────────────────────────────────
    // Rack's default handlers only reset/randomize `params`, and a hosted
    // firmware has none — its whole state lives inside the engine. Forward both
    // events so "Initialize" and "Randomize" do something. Chaining to
    // Module::on* first keeps any params/bypass handling a subclass may add.
    //
    // The two host-side settings are hardware configuration, not patch content:
    // Initialize restores them (a fresh module's defaults), Randomize does not
    // touch them — randomizing the input range would silently rescale every
    // incoming CV.
    void onReset(const ResetEvent &e) override {
        Module::onReset(e);
        cvRange = CV_UNIPOLAR;
        encoderSensitivity = ENC_MEDIUM;
        if (engine)
            engine->reset();
    }

    void onRandomize(const RandomizeEvent &e) override {
        Module::onRandomize(e);
        if (engine)
            engine->randomize();
    }

    // Restore the shared state written by baseToJson.
    void baseFromJson(json_t *root) {
        json_t *arr = json_object_get(root, "eeprom");
        if (engine && arr && json_is_array(arr)) {
            std::string blob;
            size_t n = json_array_size(arr);
            blob.reserve(n);
            for (size_t i = 0; i < n; i++)
                blob.push_back((char)json_integer_value(json_array_get(arr, i)));
            engine->deserialize(blob);
        }
        if (json_t *j = json_object_get(root, "cvRange"))
            cvRange = (int)json_integer_value(j);
        if (json_t *j = json_object_get(root, "encoderSensitivity"))
            encoderSensitivity = (int)json_integer_value(j);
    }
};

} // namespace forgevcv
