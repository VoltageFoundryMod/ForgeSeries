#include "engine/fw_engine.hpp"
#include "plugin.hpp"
#include <atomic>

// Control-rate decimation: run the firmware engine every N audio samples.
// PPQN tick rate peaks at ~4.8 kHz (300 BPM); at 44.1 kHz, N=8 -> ~5.5 kHz
// engine rate keeps every clock edge while staying cheap.
static const int ENGINE_DECIM = 8;

struct ClockForge : Module {
    enum ParamId {
        PARAMS_LEN
    };
    enum InputId {
        CLKIN_INPUT,
        CV1IN_INPUT,
        CV2IN_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        OUT1_OUTPUT,
        OUT2_OUTPUT,
        OUT3_OUTPUT,
        OUT4_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        LIGHTS_LEN
    };

    cfengine::Engine *engine = nullptr;
    uint8_t fb[1024] = {0};          // latest framebuffer (written in process, read in draw)
    float outHold[4] = {0, 0, 0, 0}; // held output volts between control-rate updates
    int decim = 0;

    // ── Context-menu settings ────────────────────────────────────────────────
    // CV input range: the firmware engine works in a 0..5V (0..4095) domain. In
    // bipolar mode we linearly remap -5..+5V onto that full range before feeding
    // the engine (a hardware-impossible convenience — see VCVRack_Plugin.md).
    enum CvRange { CV_UNIPOLAR,
                   CV_BIPOLAR,
                   CV_0TO10V };
    int cvRange = CV_UNIPOLAR;
    // Encoder drag sensitivity (pixels per detent); lower = more sensitive.
    enum EncSensitivity { ENC_LOW,
                          ENC_MEDIUM,
                          ENC_HIGH };
    int encoderSensitivity = ENC_MEDIUM;

    // Map a raw input voltage to the engine's 0..5V domain per the CV range mode.
    float mapCvInput(float v) const {
        if (cvRange == CV_BIPOLAR)
            v = (v + 5.f) * 0.5f; // -5..+5V -> 0..5V (linear, full range)
        else if (cvRange == CV_0TO10V)
            v = v * 0.5f; // 0..10V -> 0..5V (linear, full range)
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

    // Encoder UI events from the widget (UI thread) -> consumed in process (audio thread).
    std::atomic<int> encDelta{0};
    std::atomic<int> encClick{0};

    ClockForge() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configInput(CLKIN_INPUT, "Clock (TRIG)");
        configInput(CV1IN_INPUT, "CV 1");
        configInput(CV2IN_INPUT, "CV 2");
        configOutput(OUT1_OUTPUT, "Out 1");
        configOutput(OUT2_OUTPUT, "Out 2");
        configOutput(OUT3_OUTPUT, "Out 3");
        configOutput(OUT4_OUTPUT, "Out 4");
        engine = cfengine::createEngine();
    }

    ~ClockForge() override {
        if (engine)
            cfengine::destroyEngine(engine);
    }

    void process(const ProcessArgs &args) override {
        if (!engine)
            return;

        // Drain encoder UI events.
        int d = encDelta.exchange(0);
        if (d)
            cfengine::encoderTurn(engine, d);
        int c = encClick.exchange(0);
        for (int i = 0; i < c; i++) {
            cfengine::encoderButton(engine, true);
            cfengine::encoderButton(engine, false);
        }

        // Run the engine at control rate; hold outputs between updates.
        if (++decim >= ENGINE_DECIM) {
            decim = 0;
            float dt = ENGINE_DECIM * args.sampleTime;
            float cv[2] = {
                mapCvInput(inputs[CV1IN_INPUT].getVoltage()),
                mapCvInput(inputs[CV2IN_INPUT].getVoltage())};
            bool clk = inputs[CLKIN_INPUT].getVoltage() > 1.f;
            cfengine::process(engine, dt, cv, clk, outHold);
            cfengine::getFramebuffer(engine, fb);
        }

        for (int i = 0; i < 4; i++)
            outputs[OUT1_OUTPUT + i].setVoltage(outHold[i]);
    }

    json_t *dataToJson() override {
        json_t *root = json_object();
        std::string blob = cfengine::serialize(engine);
        json_t *arr = json_array();
        for (unsigned char ch : blob)
            json_array_append_new(arr, json_integer(ch));
        json_object_set_new(root, "eeprom", arr);
        json_object_set_new(root, "cvRange", json_integer(cvRange));
        json_object_set_new(root, "encoderSensitivity", json_integer(encoderSensitivity));
        return root;
    }

    void dataFromJson(json_t *root) override {
        json_t *arr = json_object_get(root, "eeprom");
        if (arr && json_is_array(arr)) {
            std::string blob;
            size_t n = json_array_size(arr);
            blob.reserve(n);
            for (size_t i = 0; i < n; i++)
                blob.push_back((char)json_integer_value(json_array_get(arr, i)));
            cfengine::deserialize(engine, blob);
        }
        if (json_t *j = json_object_get(root, "cvRange"))
            cvRange = (int)json_integer_value(j);
        if (json_t *j = json_object_get(root, "encoderSensitivity"))
            encoderSensitivity = (int)json_integer_value(j);
    }
};

// ── Emulated OLED: blits the firmware's 128x64 framebuffer via NanoVG ─────────
// A 128x64 texture sampled nearest-neighbor => crisp pixels at any zoom.
// The image is created once and updated each frame (NEVER deleted inside draw():
// NanoVG batches draws and flushes at end-of-frame, so deleting here renders
// nothing).
struct FramebufferDisplay : Widget {
    ClockForge *module = nullptr;
    int img = -1;
    uint8_t rgba[128 * 64 * 4] = {0};

    void draw(const DrawArgs &args) override {
        // Fit 128x64 (2:1) to width, center vertically in the cutout.
        float w = box.size.x;
        float h = w * 64.f / 128.f;
        float oy = (box.size.y - h) / 2.f;

        // Screen background (near-black OLED).
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, oy, w, h);
        nvgFillColor(args.vg, nvgRGB(6, 10, 16));
        nvgFill(args.vg);

        if (!module)
            return;

        // 1bpp framebuffer -> RGBA (lit = OLED-blue, unlit = transparent).
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 128; x++) {
                bool px = module->fb[(y * 128 + x) >> 3] & (0x80 >> (x & 7));
                int o = (y * 128 + x) * 4;
                rgba[o + 0] = px ? 130 : 0;
                rgba[o + 1] = px ? 220 : 0;
                rgba[o + 2] = px ? 255 : 0;
                rgba[o + 3] = px ? 255 : 0;
            }
        }
        if (img < 0)
            img = nvgCreateImageRGBA(args.vg, 128, 64, NVG_IMAGE_NEAREST, rgba);
        else
            nvgUpdateImage(args.vg, img, rgba);

        NVGpaint p = nvgImagePattern(args.vg, 0, oy, w, h, 0, img, 1.f);
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, oy, w, h);
        nvgFillPaint(args.vg, p);
        nvgFill(args.vg);
    }
};

// ── Encoder: drag to rotate (relative detents), click to push ────────────────
struct EncoderKnob : OpaqueWidget {
    ClockForge *module = nullptr;
    float accum = 0.f;    // sub-detent rotation accumulator
    float pathLen = 0.f;  // total drag distance, to distinguish click from turn
    float visAngle = 0.f; // visual indicator angle

    void emit(int steps) {
        if (module)
            module->encDelta.fetch_add(steps);
        visAngle -= steps * 0.35f;
    }

    void onDragStart(const event::DragStart &e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT)
            return;
        accum = 0.f;
        pathLen = 0.f;
        APP->window->cursorLock();
    }

    void onDragMove(const event::DragMove &e) override {
        pathLen += std::abs(e.mouseDelta.x) + std::abs(e.mouseDelta.y);
        float ppd = module ? module->encoderPixelsPerDetent() : 11.f;
        // Right / up = clockwise.
        accum += e.mouseDelta.x - e.mouseDelta.y;
        while (accum >= ppd) {
            accum -= ppd;
            emit(+1);
        }
        while (accum <= -ppd) {
            accum += ppd;
            emit(-1);
        }
    }

    void onDragEnd(const event::DragEnd &e) override {
        APP->window->cursorUnlock();
        if (pathLen < 3.f && module) // negligible movement -> treat as a push
            module->encClick.fetch_add(1);
    }

    void draw(const DrawArgs &args) override {
        float r = box.size.x / 2.f;
        Vec c = box.size.div(2.f);
        // Outer body
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, c.x, c.y, r - 1.f);
        nvgFillColor(args.vg, nvgRGB(45, 45, 50));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGB(20, 20, 22));
        nvgStrokeWidth(args.vg, 1.2f);
        nvgStroke(args.vg);

        // Knurled grip: evenly spaced notches around the rim. Because the
        // pattern is rotationally symmetric there is no "start/end" position
        // (unlike a pointer line) but it visibly spins as the encoder turns.
        const int teeth = 12;
        float rOut = r - 1.5f;
        float rIn = r * 0.64f;
        nvgStrokeColor(args.vg, nvgRGB(120, 120, 128));
        nvgStrokeWidth(args.vg, 2.2f);
        nvgLineCap(args.vg, NVG_ROUND);
        for (int i = 0; i < teeth; i++) {
            float a = visAngle + (float)i / teeth * 2.f * M_PI;
            float ca = std::cos(a), sa = std::sin(a);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, c.x + ca * rIn, c.y + sa * rIn);
            nvgLineTo(args.vg, c.x + ca * rOut, c.y + sa * rOut);
            nvgStroke(args.vg);
        }

        // Recessed cap to set the grip apart from the flat top.
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, c.x, c.y, rIn);
        nvgFillColor(args.vg, nvgRGB(38, 38, 43));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGB(22, 22, 25));
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);
    }
};

struct ClockForgeWidget : ModuleWidget {
    ClockForgeWidget(ClockForge *module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/ClockForge.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(14.989, 66.795)), module, ClockForge::CLKIN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.153, 80.797)), module, ClockForge::CV1IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.647, 80.797)), module, ClockForge::CV2IN_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.412, 95.068)), module, ClockForge::OUT1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.652, 95.068)), module, ClockForge::OUT2_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.407, 109.34)), module, ClockForge::OUT3_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.647, 109.34)), module, ClockForge::OUT4_OUTPUT));

        // Emulated OLED over the display cutout.
        FramebufferDisplay *disp = new FramebufferDisplay();
        disp->module = module;
        disp->box.pos = mm2px(Vec(2.244, 19.776));
        disp->box.size = mm2px(Vec(25.362, 14.994));
        addChild(disp);

        // Encoder (drag to scroll, click to select).
        EncoderKnob *enc = new EncoderKnob();
        enc->module = module;
        enc->box.size = mm2px(Vec(9.0, 9.0));
        enc->box.pos = mm2px(Vec(14.924, 50.918)).minus(enc->box.size.div(2));
        addChild(enc);
    }

    void appendContextMenu(Menu *menu) override {
        ClockForge *m = dynamic_cast<ClockForge *>(module);
        if (!m)
            return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("ClockForge Settings"));
        menu->addChild(createIndexPtrSubmenuItem(
            "Input CV Range", {"0V – 5V", "-5V – +5V", "0V – 10V"}, &m->cvRange));
        menu->addChild(createIndexPtrSubmenuItem(
            "Encoder Sensitivity", {"Low", "Medium", "High"}, &m->encoderSensitivity));
    }
};

Model *modelClockForge = createModel<ClockForge, ClockForgeWidget>("ClockForge");
