#pragma once
//  ForgeView — scope engine (platform-independent).
//
//  This header holds ALL of the scope's mutable state (file-scope globals) plus
//  the acquisition + render + encoder logic.  It is included unchanged by:
//    • src/main.cpp                — the RP2040 firmware
//    • vcv-plugin/.../fw_engine.cpp — the VCV Rack engine (via the Arduino shim)
//
//  Acquisition is PUSH-based: the host calls ScopeFeedSample() once per input
//  sample (per ADC read on hardware, per audio sample in Rack) and the engine
//  decimates / captures internally.  This is the key adaptation over the original
//  SAMD21 firmware, which pulled samples in a blocking analogRead()+delay loop —
//  a model that cannot work under Rack's one-sample-at-a-time process().  The
//  timebase knob becomes a sample-decimation factor instead of a busy-wait delay.
//
//  The on-screen menu is a PER-MODE named parameter list (see the ParamId
//  registry below): each mode declares its own ordered list of parameters, the
//  encoder scrolls the cursor through them, and a click toggles editing of the
//  cursor row.  A long-press of the encoder toggles freeze-frame.
//
//  Every mutable global below is mirrored in the VCV engine_state.def registry so
//  multiple Rack instances can each own an independent copy (context-swap).

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>

#include <math.h>

// Custom fonts for the Tuner's big readout only. The overlay menu and axis
// labels keep the classic 5x7 font (dense layouts built on its 6x8 cell).
#include "fonts/helvB12.h"
#include "fonts/helvB24.h"

// fixfft.cpp is #included (not compiled separately) so this single source builds
// as part of whichever TU pulls in scope.hpp — the RP2040 firmware or the VCV
// engine — mirroring how ClockForge vendors quantizer.cpp / scales.cpp.
#include "fixfft.cpp"

#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH 128
#endif
#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT 64
#endif

// ── Scope modes ──────────────────────────────────────────────────────────────
// New modes are APPENDED (X-Y, Tuner) so existing patch/JSON mode integers keep
// meaning across upgrades (LFO..Spectrum stay 1..4).
enum ScopeMode {
    MODE_LFO = 1,      // dual-trace slow scope of CV1 + CV2
    MODE_WAVE = 2,     // single-channel oscilloscope (CV1)
    MODE_SHOT = 3,     // triggered single-shot capture of CV1
    MODE_SPECTRUM = 4, // 128-point FFT spectrum of CV1
    MODE_XY = 5,       // X-Y (Lissajous): CV1 = X, CV2 = Y
    MODE_TUNER = 6,    // frequency meter / tuner on CV1
};
#define SCOPE_MODE_COUNT 6

static const char *kScopeModeNames[SCOPE_MODE_COUNT] = {
    "Dual", "Single", "Shot", "Spectrum", "X-Y", "Tuner"};

static inline const char *ScopeModeName(int m) {
    if (m < 1 || m > SCOPE_MODE_COUNT)
        return "?";
    return kScopeModeNames[m - 1];
}

// ── Trigger modes (Shot) ─────────────────────────────────────────────────────
enum TrigModeId { TRIG_OFF = 0,
                  TRIG_RISING = 1,
                  TRIG_FALLING = 2,
                  TRIG_AUTO = 3 };
static const char *kTrigNames[4] = {"Off", "Rising", "Falling", "Auto"};

// ── X-Y phosphor persistence ─────────────────────────────────────────────────
// The X-Y trajectory is captured into a ring buffer and every point is drawn
// each frame, so the Lissajous figure "persists" like a scope phosphor. The
// selected duration sets how long a point stays on screen; the capture is
// decimated so the fixed-size ring spans that whole window (longer persistence
// => coarser sampling, which is fine because a stable figure fills in over time).
#define XY_RING 256
#define XY_PERSIST_COUNT 13
static const char *kXYPersistNames[XY_PERSIST_COUNT] = {
    "Live", "0.1s", "0.25s", "0.5s", "1s", "2s", "5s", "10s", "30s", "60s", "2m", "3m", "5m"};
static const int kXYPersistMs[XY_PERSIST_COUNT] = {
    15, 100, 250, 500, 1000, 2000, 5000, 10000, 30000, 60000, 120000, 180000, 300000};

// Max value of the timebase knob (param1) for the time-domain modes. The slowest
// setting decimates by 1<<(TIMEBASE_MAX-1) input samples per stored pixel, i.e.
// a ~2^(TIMEBASE_MAX-1) longer window than the fastest setting — enough to fit
// many LFO cycles on screen. Spectrum mode reuses param1 as HF emphasis and is
// kept at 8 (see ScopeParam1Max).
#define TIMEBASE_MAX 14

// ── Mutable state (registered in engine_state.def for the VCV context-swap) ──
int menuMode = MODE_LFO;    // current scope mode (1..6)
int oldMenuMode = MODE_LFO; // previous mode (detects mode change)
int param = 0;              // overlay cursor row (0-based index into the mode list)
int param_select = 0;       // 0 = navigating rows; 1 = editing the cursor row
int param1 = 2;             // per-mode parameter A (timebase / HF emphasis)
int param2 = 1;             // per-mode parameter B (refresh / noise floor)
float scale = 1.0f;         // vertical gain multiplier

int offset0 = 0; // CH1 vertical offset (screen units, +up)
int offset1 = 0; // CH2 vertical offset (screen units, +up)

int trigMode = TRIG_RISING; // Shot-mode trigger selector
bool frozen = false;        // freeze-frame: hold the display, ignore new samples
bool showLabels = true;     // draw engineering-unit axis labels (V/div, time/div)

bool switchState = true; // encoder push-button level (1 = released)
bool oldSwitchState = true;
unsigned long pressStartMs = 0; // millis() at the last press edge (long-press timing)

unsigned long hideTimer = 0; // millis() of last interaction (overlay visibility)
bool overlayOn = true;       // draw the parameter overlay box

bool trig = false;     // latched external-trigger level (shot mode)
bool old_trig = false; // previous trigger level (edge detect)
int rfrs = 0;          // wave-mode refresh divider counter

int _decimCount = 0;         // samples seen since the last stored trace sample
bool _shotCapturing = false; // shot mode: capture in progress
int _shotIdx = 0;            // shot mode: next capture index
int _capIdx = 0;             // spectrum mode: next FFT capture index

// Tuner state (zero-crossing frequency estimate).
int tunerChan = 1;             // input channel measured by the tuner (1 or 2)
float tunerHz = 0.0f;          // smoothed detected frequency (0 = no signal)
float tunerAmp = 0.0f;         // tracked signal amplitude (ADC counts, envelope)
int tunerPrevSign = 0;         // arming state: -1 armed (saw a clear negative), +1 fired
float tunerPrevV = 0.0f;       // previous sample value (for sub-sample interpolation)
float tunerLastCrossUs = 0.0f; // interpolated µs of the last rising zero crossing
unsigned long lastCrossUs = 0; // micros() of the last rising crossing / auto-trigger

// Timing used for engineering-unit horizontal scale (measured feed interval).
unsigned long lastFeedUs = 0; // micros() of the previous feed
float feedIntervalUs = 20.0f; // smoothed inter-feed interval (µs)

// Display calibration: full-scale ADC (0..4095) spans this many input volts.
// Set by the host (VCV: from the CV-range setting); hardware leaves the default.
float scopeInputSpanV = 5.0f;

// Trace buffers: index 0 = oldest, 127 = newest (shift register).
// Values are signed "screen units": ADC midscale (2048) -> 0, ±2048 -> ±64.
// int8_t, not plain char: char is unsigned on the RP2040's ARM compiler, so
// storing negative screen units in char breaks on hardware (works only on x86).
int8_t cv0[SCREEN_WIDTH] = {0}; // channel 1 trace
int8_t cv1[SCREEN_WIDTH] = {0}; // channel 2 trace (LFO / X-Y mode)

int8_t fftCap[SCREEN_WIDTH] = {0};          // spectrum: rolling capture buffer
unsigned char spec[SCREEN_WIDTH / 2] = {0}; // spectrum: last computed magnitudes (64 bins)
int specPeakBin = 0;                        // spectrum: bin index of the tallest peak (0 = none)
int specChan = 1;                           // spectrum: input channel analyzed (1 or 2)

// X-Y persistence ring (screen units); drawn in full every frame.
int xyPersist = 3;    // persistence level index (see kXYPersistNames; 3 = 0.5s)
int xyHead = 0;       // next write position in the ring
int xyCount = 0;      // valid points currently in the ring
int xyDecimCount = 0; // capture-decimation counter
int8_t xyx[XY_RING] = {0};
int8_t xyy[XY_RING] = {0};

// ── Small helpers ────────────────────────────────────────────────────────────
// Map a 0..4095 ADC reading to a signed screen unit (-64..+63 at full scale).
static inline int8_t ScopeMapSample(int adc) {
    int v = (adc - 2048) >> 5; // ±2048 -> ±64
    return (int8_t)constrain(v, -120, 120);
}

// Timebase decimation: knob 1..TIMEBASE_MAX -> keep 1 sample every 1,2,4,…
// (exponential/div). Higher = slower sweep = more waveforms across the screen.
static inline int ScopeTimebaseDecim(int p) {
    p = constrain(p, 1, TIMEBASE_MAX);
    return 1 << (p - 1);
}

// Max for the param1 knob in the current mode: the extended timebase range for
// the time-domain modes, but only 1..8 in Spectrum (there param1 is HF emphasis).
static inline int ScopeParam1Max() {
    return (menuMode == MODE_SPECTRUM) ? 8 : TIMEBASE_MAX;
}

// Vertical placement: center + scaled sample (positive sample = up on screen).
static inline int ScopeY(int center, int8_t s, float sc) {
    int y = center - (int)((float)s * sc * 0.5f);
    return constrain(y, 0, SCREEN_HEIGHT - 1);
}

static void DrawHDashedLine(Adafruit_SSD1306 &display, int x0, int y0, int width, int color) {
    for (int i = 0; i < width; i += 4)
        display.drawPixel(x0 + i, y0, color);
}
static void DrawVDashedLine(Adafruit_SSD1306 &display, int x0, int y0, int height, int color) {
    for (int i = 0; i < height; i += 4)
        display.drawPixel(x0, y0 + i, color);
}

// ── Per-mode parameter registry ──────────────────────────────────────────────
// Each mode exposes an ordered list of named parameters; the overlay renders it
// and the encoder edits it. Adding a per-mode option means adding a ParamId and
// listing it in the mode's array — no per-row switch scattered through the code.
enum ParamId {
    P_MODE,     // scope mode (name)
    P_TIMEBASE, // horizontal timebase (param1)
    P_TRIG,     // shot trigger mode (name)
    P_OFF1,     // CH1 vertical offset
    P_OFF2,     // CH2 vertical offset
    P_VSCALE,   // vertical gain
    P_REFRESH,  // wave refresh divider (param2)
    P_HF,       // spectrum high-frequency emphasis (param1)
    P_FILT,     // spectrum noise floor (param2)
    P_SCHAN,    // spectrum: which input channel to analyze (1 or 2)
    P_LABELS,   // engineering-unit labels on/off
    P_CHAN,     // tuner: which input channel to measure (1 or 2)
    P_PERSIST,  // X-Y: phosphor persistence duration
};

#define SCOPE_ARRLEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const ParamId kParamsLFO[] = {P_MODE, P_TIMEBASE, P_OFF1, P_OFF2, P_VSCALE, P_LABELS};
static const ParamId kParamsWave[] = {P_MODE, P_TIMEBASE, P_OFF1, P_VSCALE, P_REFRESH, P_LABELS};
static const ParamId kParamsShot[] = {P_MODE, P_TIMEBASE, P_TRIG, P_OFF1, P_VSCALE, P_LABELS};
static const ParamId kParamsSpec[] = {P_MODE, P_SCHAN, P_HF, P_FILT, P_LABELS};
static const ParamId kParamsXY[] = {P_MODE, P_VSCALE, P_PERSIST, P_LABELS};
static const ParamId kParamsTuner[] = {P_MODE, P_CHAN};

static const ParamId *ScopeParamList(int &count) {
    switch (menuMode) {
    case MODE_LFO:
        count = SCOPE_ARRLEN(kParamsLFO);
        return kParamsLFO;
    case MODE_WAVE:
        count = SCOPE_ARRLEN(kParamsWave);
        return kParamsWave;
    case MODE_SHOT:
        count = SCOPE_ARRLEN(kParamsShot);
        return kParamsShot;
    case MODE_SPECTRUM:
        count = SCOPE_ARRLEN(kParamsSpec);
        return kParamsSpec;
    case MODE_XY:
        count = SCOPE_ARRLEN(kParamsXY);
        return kParamsXY;
    case MODE_TUNER:
        count = SCOPE_ARRLEN(kParamsTuner);
        return kParamsTuner;
    }
    count = SCOPE_ARRLEN(kParamsTuner);
    return kParamsTuner;
}

static inline int ScopeParamCount() {
    int c;
    ScopeParamList(c);
    return c;
}
static inline ParamId ScopeParamAt(int row) {
    int c;
    const ParamId *list = ScopeParamList(c);
    return list[constrain(row, 0, c - 1)];
}

static const char *ScopeParamLabel(ParamId id) {
    switch (id) {
    case P_MODE:
        return "Mode";
    case P_TIMEBASE:
        return "Time";
    case P_TRIG:
        return "Trig";
    case P_OFF1:
        return "Off1";
    case P_OFF2:
        return "Off2";
    case P_VSCALE:
        return "Vert";
    case P_REFRESH:
        return "Refr";
    case P_HF:
        return "High";
    case P_FILT:
        return "Filt";
    case P_LABELS:
        return "Info";
    case P_CHAN:
    case P_SCHAN:
        return "Chan";
    case P_PERSIST:
        return "Pers";
    }
    return "?";
}

// Format the current value of a parameter into buf (embedded-safe snprintf).
static void ScopeParamFormat(ParamId id, char *buf, int n) {
    switch (id) {
    case P_MODE:
        snprintf(buf, n, "%s", ScopeModeName(menuMode));
        break;
    case P_TIMEBASE:
        snprintf(buf, n, "%d", param1);
        break;
    case P_TRIG:
        snprintf(buf, n, "%s", kTrigNames[constrain(trigMode, 0, 3)]);
        break;
    case P_OFF1:
        snprintf(buf, n, "%+d", offset0);
        break;
    case P_OFF2:
        snprintf(buf, n, "%+d", offset1);
        break;
    case P_VSCALE:
        snprintf(buf, n, "%.2f", (double)scale);
        break;
    case P_REFRESH:
        snprintf(buf, n, "%d", param2);
        break;
    case P_HF:
        snprintf(buf, n, "%d", param1);
        break;
    case P_FILT:
        snprintf(buf, n, "%d", param2);
        break;
    case P_LABELS:
        snprintf(buf, n, "%s", showLabels ? "On" : "Off");
        break;
    case P_CHAN:
        snprintf(buf, n, "CH%d", tunerChan);
        break;
    case P_SCHAN:
        snprintf(buf, n, "CH%d", specChan);
        break;
    case P_PERSIST:
        snprintf(buf, n, "%s", kXYPersistNames[constrain(xyPersist, 0, XY_PERSIST_COUNT - 1)]);
        break;
    }
}

static void ScopeApplyModeDefaults(); // fwd

// Apply one detent of change (dir = ±1) to a parameter.
static void ScopeParamEdit(ParamId id, int dir) {
    switch (id) {
    case P_MODE:
        menuMode = constrain(menuMode + dir, 1, SCOPE_MODE_COUNT);
        if (menuMode != oldMenuMode) {
            ScopeApplyModeDefaults();
            oldMenuMode = menuMode;
        }
        break;
    case P_TIMEBASE:
    case P_HF:
        param1 = constrain(param1 + dir, 1, ScopeParam1Max());
        break;
    case P_TRIG:
        trigMode = constrain(trigMode + dir, 0, 3);
        break;
    case P_OFF1:
        offset0 = constrain(offset0 + dir, -24, 24);
        break;
    case P_OFF2:
        offset1 = constrain(offset1 + dir, -24, 24);
        break;
    case P_VSCALE:
        scale *= (dir > 0) ? 1.1f : (1.0f / 1.1f);
        if (scale > 0.91f && scale < 1.09f)
            scale = 1.0f;
        scale = constrain(scale, 0.05f, 20.0f);
        break;
    case P_REFRESH:
    case P_FILT:
        param2 = constrain(param2 + dir, 1, 8);
        break;
    case P_LABELS:
        showLabels = (dir > 0); // CW = on, CCW = off
        break;
    case P_CHAN:
        tunerChan = constrain(tunerChan + dir, 1, 2);
        break;
    case P_SCHAN:
        specChan = constrain(specChan + dir, 1, 2);
        break;
    case P_PERSIST:
        xyPersist = constrain(xyPersist + dir, 0, XY_PERSIST_COUNT - 1);
        break;
    }
}

// ── Mode defaults ────────────────────────────────────────────────────────────
// Applied when the mode changes; mirrors the original firmware's per-mode seeds.
static void ScopeApplyModeDefaults() {
    switch (menuMode) {
    case MODE_LFO:
        param1 = 2; // timebase
        break;
    case MODE_WAVE:
        param1 = 3; // timebase
        param2 = 3; // refresh divider
        break;
    case MODE_SHOT:
        param1 = 2; // timebase
        _shotCapturing = false;
        lastCrossUs = 0;
        break;
    case MODE_SPECTRUM:
        param1 = 2; // high-frequency emphasis
        param2 = 3; // noise floor
        _capIdx = 0;
        specPeakBin = 0;
        break;
    case MODE_XY:
        xyHead = 0;
        xyCount = 0;
        xyDecimCount = 0;
        break;
    case MODE_TUNER:
        tunerHz = 0.0f;
        tunerAmp = 0.0f;
        tunerPrevSign = 0;
        tunerPrevV = 0.0f;
        tunerLastCrossUs = 0.0f;
        lastCrossUs = 0;
        break;
    }
    _decimCount = 0;
}

void ScopeInit() {
    menuMode = MODE_LFO;
    oldMenuMode = MODE_LFO;
    param = 0;
    param_select = 0;
    scale = 1.0f;
    offset0 = offset1 = 0;
    trigMode = TRIG_RISING;
    tunerChan = 1;
    specChan = 1;
    xyPersist = 3;
    frozen = false;
    showLabels = true;
    switchState = oldSwitchState = true;
    pressStartMs = 0;
    hideTimer = millis();
    overlayOn = true;
    lastFeedUs = 0;
    feedIntervalUs = 20.0f;
    ScopeApplyModeDefaults();
}

// ── Acquisition ──────────────────────────────────────────────────────────────
// Called once per input sample. ch1/ch2 are 0..4095 ADC; clkHigh is the CLK/
// trigger input level. Handles per-mode decimation and capture.
void ScopeFeedSample(int ch1adc, int ch2adc, bool clkHigh) {
    // Track the real inter-feed interval for the horizontal engineering scale.
    unsigned long nowUs = micros();
    if (lastFeedUs != 0) {
        unsigned long dtUs = nowUs - lastFeedUs;
        if (dtUs > 0 && dtUs < 1000000UL)
            feedIntervalUs += 0.05f * ((float)dtUs - feedIntervalUs);
    }
    lastFeedUs = nowUs;

    // Freeze-frame: hold the display, drop all new samples.
    if (frozen)
        return;

    // Trigger edge detection is never decimated (shot mode needs every edge).
    old_trig = trig;
    trig = clkHigh;

    if (menuMode == MODE_TUNER) {
        // Pitch detection: measure the period between successive RISING zero
        // crossings of the selected channel. Two refinements keep the readout
        // steady and accurate:
        //   • Hysteresis arming — a crossing only counts after the signal has
        //     dipped clearly negative (< -HYST), so noise near zero can't
        //     retrigger within a cycle.
        //   • Sub-sample interpolation — the true zero crossing rarely lands on
        //     a sample boundary; linearly interpolating between the straddling
        //     samples removes the ±½-sample period quantization that otherwise
        //     makes the reading jump by a few Hz every frame.
        int adc = (tunerChan == 2) ? ch2adc : ch1adc;
        float v = (float)(adc - 2048); // ±2048
        float mag = v < 0 ? -v : v;
        tunerAmp += 0.01f * (mag - tunerAmp);
        const float HYST = 40.0f; // ignore noise near zero

        if (v < -HYST)
            tunerPrevSign = -1; // arm once the signal is clearly negative
        // Rising zero crossing while armed: prev sample below zero, this one at/above.
        if (tunerPrevSign < 0 && tunerPrevV < 0.0f && v >= 0.0f) {
            float dvdt = v - tunerPrevV; // > 0 here
            float frac = (dvdt > 0.0f) ? (0.0f - tunerPrevV) / dvdt : 0.0f;
            frac = constrain(frac, 0.0f, 1.0f); // crossing is frac samples past prev sample
            // Sample times: prev at nowUs-feedIntervalUs, current at nowUs.
            float crossUs = (float)nowUs - (1.0f - frac) * feedIntervalUs;
            if (tunerLastCrossUs != 0.0f) {
                float periodUs = crossUs - tunerLastCrossUs;
                if (periodUs > 30.0f && periodUs < 1000000.0f) {
                    float f = 1.0e6f / periodUs;
                    if (tunerHz <= 0.0f)
                        tunerHz = f;
                    else
                        tunerHz += 0.2f * (f - tunerHz); // smooth
                }
            }
            tunerLastCrossUs = crossUs;
            tunerPrevSign = 1; // disarm until the signal goes negative again
        }
        tunerPrevV = v;
        if (tunerAmp < 60.0f) // signal too small -> no reading
            tunerHz = 0.0f;
        return;
    }

    if (menuMode == MODE_SPECTRUM) {
        // Sample as fast as possible for maximum bandwidth; run the FFT when the
        // 128-sample window is full.
        int adc = (specChan == 2) ? ch2adc : ch1adc;
        fftCap[_capIdx++] = (int8_t)constrain((adc - 2048) >> 4, -127, 127);
        if (_capIdx >= SCREEN_WIDTH) {
            _capIdx = 0;
            int8_t data[SCREEN_WIDTH];
            int8_t im[SCREEN_WIDTH];
            // Subtract the window mean before the FFT: hardware inputs never
            // idle exactly at ADC midscale, and the residual DC otherwise pins
            // the bin-0 bar at full height no matter what signal is patched.
            int mean = 0;
            for (int i = 0; i < SCREEN_WIDTH; i++)
                mean += fftCap[i];
            mean /= SCREEN_WIDTH;
            for (int i = 0; i < SCREEN_WIDTH; i++) {
                data[i] = (int8_t)constrain(fftCap[i] - mean, -128, 127);
                im[i] = 0;
            }
            fix_fft(data, im, 7, 0); // 2^7 = 128-point FFT
            int peakBin = 0, peakMag = 0;
            for (int i = 0; i < SCREEN_WIDTH / 2; i++) {
                int level = (int)sqrtf((float)(data[i] * data[i] + im[i] * im[i]));
                spec[i] = (unsigned char)constrain(level, 0, SCREEN_HEIGHT);
                // Track the tallest bin for the peak-frequency readout. Skip bins
                // 0/1 (DC + leakage) so a sub-bin offset doesn't win over a tone.
                if (i >= 2 && level > peakMag) {
                    peakMag = level;
                    peakBin = i;
                }
            }
            specPeakBin = (peakMag >= param2) ? peakBin : 0; // gate on the noise floor
        }
        return;
    }

    if (menuMode == MODE_XY) {
        // X-Y: push points into the persistence ring. Capture is decimated so the
        // fixed-size ring spans the selected persistence window (derived from the
        // measured feed interval, so the duration holds on either platform).
        int target = kXYPersistMs[constrain(xyPersist, 0, XY_PERSIST_COUNT - 1)];
        int decim = 1;
        if (feedIntervalUs > 0.1f) {
            decim = (int)((float)target * 1000.0f / (feedIntervalUs * (float)XY_RING));
            if (decim < 1)
                decim = 1;
        }
        if (++xyDecimCount < decim)
            return;
        xyDecimCount = 0;
        xyx[xyHead] = ScopeMapSample(ch1adc);
        xyy[xyHead] = ScopeMapSample(ch2adc);
        xyHead = (xyHead + 1) % XY_RING;
        if (xyCount < XY_RING)
            xyCount++;
        return;
    }

    if (menuMode == MODE_SHOT) {
        if (trigMode == TRIG_OFF) {
            // Free-run roll like Wave (single channel), no trigger required.
            if (++_decimCount < ScopeTimebaseDecim(param1))
                return;
            _decimCount = 0;
            for (int i = 0; i < SCREEN_WIDTH - 1; i++)
                cv0[i] = cv0[i + 1];
            cv0[SCREEN_WIDTH - 1] = ScopeMapSample(ch1adc);
            return;
        }
        // Detect the configured trigger edge; Auto self-triggers on a timeout so a
        // trace always appears even without an edge.
        bool edge = false;
        if (trigMode == TRIG_RISING)
            edge = (trig && !old_trig);
        else if (trigMode == TRIG_FALLING)
            edge = (!trig && old_trig);
        else if (trigMode == TRIG_AUTO)
            edge = (trig && !old_trig) ||
                   (!_shotCapturing && (nowUs - lastCrossUs) > 250000UL);
        if (!_shotCapturing && edge) {
            _shotCapturing = true;
            _shotIdx = 0;
            _decimCount = 0;
            lastCrossUs = nowUs; // remember for the Auto timeout
        }
        if (_shotCapturing) {
            if (++_decimCount < ScopeTimebaseDecim(param1))
                return;
            _decimCount = 0;
            cv0[_shotIdx++] = ScopeMapSample(ch1adc);
            if (_shotIdx >= SCREEN_WIDTH)
                _shotCapturing = false; // frozen until next trigger
        }
        return;
    }

    // LFO + WAVE: rolling shift-register scope at the selected timebase.
    if (++_decimCount < ScopeTimebaseDecim(param1))
        return;
    _decimCount = 0;
    int8_t s0 = ScopeMapSample(ch1adc);
    int8_t s1 = ScopeMapSample(ch2adc);
    for (int i = 0; i < SCREEN_WIDTH - 1; i++) {
        cv0[i] = cv0[i + 1];
        cv1[i] = cv1[i + 1];
    }
    cv0[SCREEN_WIDTH - 1] = s0;
    cv1[SCREEN_WIDTH - 1] = s1;
}

// ── Rendering ────────────────────────────────────────────────────────────────
static void ScopeDrawGrid(Adafruit_SSD1306 &display) {
    for (int x = 0; x < SCREEN_WIDTH; x += 16)
        DrawVDashedLine(display, x, 0, SCREEN_HEIGHT, WHITE);
    DrawVDashedLine(display, SCREEN_WIDTH - 1, 0, SCREEN_HEIGHT, WHITE);
    for (int y = 0; y < SCREEN_HEIGHT; y += 16)
        DrawHDashedLine(display, 0, y, SCREEN_WIDTH, WHITE);
    DrawHDashedLine(display, 0, SCREEN_HEIGHT - 1, SCREEN_WIDTH, WHITE);
}

static void ScopeDrawTrace(Adafruit_SSD1306 &display, const int8_t *buf, int center, float sc) {
    for (int i = 0; i < SCREEN_WIDTH - 1; i++) {
        display.drawLine(i, ScopeY(center, buf[i], sc),
                         i + 1, ScopeY(center, buf[i + 1], sc), WHITE);
    }
}

// Engineering-unit axis labels (V/div and, for time-domain modes, time/div).
// Values are approximate on hardware (derived from the measured feed interval).
static void ScopeDrawUnits(Adafruit_SSD1306 &display, bool withTime) {
    char b[24];
    display.setTextSize(1);
    display.setTextColor(WHITE, BLACK);

    // Vertical: a 16px grid division covers 1024/scale ADC counts.
    float vdiv = (1024.0f / scale) * (scopeInputSpanV / 4095.0f);
    if (vdiv >= 1.0f)
        snprintf(b, sizeof(b), "%.2fV", (double)vdiv);
    else
        snprintf(b, sizeof(b), "%dmV", (int)(vdiv * 1000.0f + 0.5f));
    display.setCursor(1, SCREEN_HEIGHT - 8);
    display.print(b);

    if (withTime) {
        // Horizontal: 16px division = 16 stored samples * feed interval * decim.
        float usPerDiv = feedIntervalUs * (float)ScopeTimebaseDecim(param1) * 16.0f;
        if (usPerDiv >= 1000.0f)
            snprintf(b, sizeof(b), "%.1fms", (double)(usPerDiv / 1000.0f));
        else
            snprintf(b, sizeof(b), "%dus", (int)(usPerDiv + 0.5f));
        int w = (int)strlen(b) * 6;
        display.setCursor(SCREEN_WIDTH - 1 - w, SCREEN_HEIGHT - 8);
        display.print(b);
    }
}

// Tuner readout: big frequency + nearest note name/octave + cents error bar.
static void ScopeDrawTuner(Adafruit_SSD1306 &display) {
    display.setTextColor(WHITE, BLACK);

    // Measured-channel indicator (top-right), shown even without a signal.
    display.setTextSize(1);
    display.setCursor(SCREEN_WIDTH - 1 - 3 * 6, 1);
    display.print("CH");
    display.print(tunerChan);

    // Big frequency readout: helvB24 digits + baseline-aligned helvB12 "Hz".
    // Custom fonts position by baseline; 27 puts the digit tops at y=3.
    // Decimals shrink with magnitude so the widest case stays inside 128px.
    char fb[16];
    if (tunerHz <= 0.0f)
        snprintf(fb, sizeof(fb), "--");
    else if (tunerHz < 100.0f)
        snprintf(fb, sizeof(fb), "%.2f", (double)tunerHz);
    else if (tunerHz < 1000.0f)
        snprintf(fb, sizeof(fb), "%.1f", (double)tunerHz);
    else
        snprintf(fb, sizeof(fb), "%.0f", (double)tunerHz);
    int16_t fx, fy;
    uint16_t fw, fh;
    display.setFont(&helvB24);
    display.getTextBounds(fb, 2, 27, &fx, &fy, &fw, &fh);
    display.setCursor(2, 27);
    display.print(fb);
    display.setFont(&helvB12);
    display.setCursor(fx + (int)fw + 4, 27);
    display.print("Hz");
    display.setFont(nullptr);
    if (tunerHz <= 0.0f)
        return;

    // Nearest note (MIDI number) + cents deviation.
    float midi = 69.0f + 12.0f * log2f(tunerHz / 440.0f);
    int note = (int)lroundf(midi);
    float cents = (midi - (float)note) * 100.0f;
    static const char *names[12] = {"C", "C#", "D", "D#", "E", "F",
                                    "F#", "G", "G#", "A", "A#", "B"};
    int idx = ((note % 12) + 12) % 12;
    int octave = note / 12 - 1;

    // Note name + octave in the big font (baseline 52: top at y=28, clear of
    // both the frequency block above and the tuning bar at y=54).
    display.setFont(&helvB12);
    display.setCursor(6, 48);
    display.print(names[idx]);
    display.print(octave);
    display.setFont(nullptr);

    display.setTextSize(1);
    display.setCursor(76, 40);
    display.print((int)cents);
    display.print("c");

    // Center tuning bar: needle deviates ±25px for ±50 cents.
    int cx = 64 + (int)(cents * 0.5f);
    cx = constrain(cx, 3, SCREEN_WIDTH - 4);
    display.drawFastVLine(64, 54, 8, WHITE); // center reference
    display.fillRect(cx - 1, 56, 3, 4, WHITE);
}

static void ScopeDrawOverlay(Adafruit_SSD1306 &display) {
    // Freeze indicator is always shown while held, independent of the overlay.
    if (frozen) {
        display.setTextSize(1);
        display.setTextColor(BLACK, WHITE);
        display.setCursor(1, 1);
        display.print(" HOLD ");
        display.setTextColor(WHITE, BLACK);
    }

    // Overlay auto-hides ~5s after the last interaction.
    overlayOn = (millis() - hideTimer) < 5000;
    if (!overlayOn)
        return;

    // The menu always renders at size 1; reset it here because some modes (Tuner)
    // leave the GFX text size at 2 from their big readouts.
    display.setTextSize(1);

    int count = ScopeParamCount();
    const int VIS = 4; // visible rows before scrolling
    int first = 0;
    if (count > VIS) {
        first = param - VIS / 2;
        if (first < 0)
            first = 0;
        if (first > count - VIS)
            first = count - VIS;
    }
    int rows = count < VIS ? count : VIS;

    int boxW = 92, boxH = rows * 10 + 6;
    int boxX = (SCREEN_WIDTH - boxW) / 2;
    int boxY = (SCREEN_HEIGHT - boxH) / 2;
    display.fillRect(boxX, boxY, boxW, boxH, BLACK);
    display.drawRect(boxX, boxY, boxW, boxH, WHITE);

    char val[24];
    for (int r = 0; r < rows; r++) {
        int idx = first + r;
        ParamId id = ScopeParamAt(idx);
        int ty = boxY + 3 + r * 10;
        bool editing = (param_select && idx == param);
        bool cursor = (idx == param);

        if (cursor && !editing)
            display.fillRect(boxX + 2, ty - 1, 2, 9, WHITE); // cursor marker

        display.setTextColor(editing ? BLACK : WHITE, editing ? WHITE : BLACK);
        display.setCursor(boxX + 6, ty);
        ScopeParamFormat(id, val, sizeof(val));
        display.print(ScopeParamLabel(id));
        display.print(":");
        display.print(val);
        display.setTextColor(WHITE, BLACK);
    }

    // Scroll indicators.
    if (first > 0) {
        display.setCursor(boxX + boxW - 7, boxY + 1);
        display.print("\x18"); // up arrow (cp437)
    }
    if (first + rows < count) {
        display.setCursor(boxX + boxW - 7, boxY + boxH - 8);
        display.print("\x19"); // down arrow (cp437)
    }
}

// Render the current mode into the display's GFX buffer (no I2C flush here).
void ScopeRender(Adafruit_SSD1306 &display) {
    display.clearDisplay();
    display.setTextSize(1);

    if (menuMode == MODE_TUNER) {
        ScopeDrawTuner(display);
        ScopeDrawOverlay(display);
        return;
    }

    if (menuMode == MODE_SPECTRUM) {
        for (int i = 0; i < SCREEN_WIDTH / 2; i++) {
            int level = spec[i] + i * (param1 - 1) / 8; // high-frequency emphasis
            if (spec[i] >= param2 && level > 0) {       // noise-floor gate
                level = constrain(level, 0, SCREEN_HEIGHT - 1);
                display.fillRect(i * 2, (SCREEN_HEIGHT - 1) - level, 2, level, WHITE);
            }
        }
        // Info readouts, gated by the Info toggle (V/div is meaningless here):
        // input-channel indicator top-right (same spot as the tuner's), peak
        // frequency right-aligned on the row below it.
        if (showLabels) {
            display.setTextSize(1);
            display.setTextColor(WHITE, BLACK);
            display.setCursor(SCREEN_WIDTH - 1 - 3 * 6, 1);
            display.print("CH");
            display.print(specChan);
            // Peak-frequency readout: bin i is at i*Fs/128, Fs = 1/feedInterval.
            if (specPeakBin > 0 && feedIntervalUs > 0.1f) {
                float peakHz = (float)specPeakBin * 1.0e6f /
                               (feedIntervalUs * (float)SCREEN_WIDTH);
                char b[16];
                if (peakHz >= 1000.0f)
                    snprintf(b, sizeof(b), "%.2fkHz", (double)(peakHz / 1000.0f));
                else
                    snprintf(b, sizeof(b), "%dHz", (int)(peakHz + 0.5f));
                int16_t x1, y1;
                uint16_t w, h;
                display.getTextBounds(b, 1, 1, &x1, &y1, &w, &h);
                display.setCursor(SCREEN_WIDTH - 1 - w, 9);
                display.print(b);
                // Downward marker over the peak bar (2px-wide bins, so column peak*2).
                int mx = specPeakBin * 2;
                display.drawLine(mx, 0, mx, 3, WHITE);
            }
        }
        ScopeDrawOverlay(display);
        return;
    }

    if (menuMode == MODE_XY) {
        // Draw the whole persistence ring as accumulated dots (symmetric ±32px
        // window so the Lissajous keeps its aspect on 128x64).
        int oldest = (xyHead - xyCount + XY_RING) % XY_RING;
        for (int k = 0; k < xyCount; k++) {
            int idx = (oldest + k) % XY_RING;
            int x = SCREEN_WIDTH / 2 + (int)((float)xyx[idx] * scale * 0.5f);
            int y = SCREEN_HEIGHT / 2 - (int)((float)xyy[idx] * scale * 0.5f);
            if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT)
                display.drawPixel(x, y, WHITE);
        }
        DrawVDashedLine(display, SCREEN_WIDTH / 2, 0, SCREEN_HEIGHT, WHITE);
        DrawHDashedLine(display, 0, SCREEN_HEIGHT / 2, SCREEN_WIDTH, WHITE);
        if (showLabels)
            ScopeDrawUnits(display, false);
        ScopeDrawOverlay(display);
        return;
    }

    if (menuMode == MODE_LFO) {
        // Two traces with independent vertical offsets.
        ScopeDrawTrace(display, cv0, SCREEN_HEIGHT / 2 - offset0, scale);
        ScopeDrawTrace(display, cv1, SCREEN_HEIGHT / 2 - offset1, scale);
    } else {
        // WAVE + SHOT: single trace (CH1 offset).
        ScopeDrawTrace(display, cv0, SCREEN_HEIGHT / 2 - offset0, scale);
    }

    ScopeDrawGrid(display);
    if (showLabels)
        ScopeDrawUnits(display, true);
    ScopeDrawOverlay(display);
}

// ── Encoder input ────────────────────────────────────────────────────────────
// One detent of rotation (dir = +1 clockwise, -1 counter-clockwise).
void ScopeEncoderTurn(int dir) {
    hideTimer = millis();
    if (!param_select) {
        // Navigate the current mode's parameter list.
        param = constrain(param + dir, 0, ScopeParamCount() - 1);
    } else {
        // Edit the cursor row; the mode row may shrink the list, so re-clamp.
        ScopeParamEdit(ScopeParamAt(param), dir);
        int c = ScopeParamCount();
        if (param > c - 1)
            param = c - 1;
    }
}

// Encoder push-button; edge-detected. pressed = true while held.
// A short click toggles editing of the cursor row; a long press (~800ms) toggles
// freeze-frame. (In VCV a click is an instantaneous press+release with no engine
// time elapsed, so it is always a short click; freeze there is driven via the API.)
void ScopeEncoderButton(bool pressed) {
    oldSwitchState = switchState;
    switchState = !pressed; // switchState mirrors the (active-low) button level

    // Only act (and wake the overlay) on real edges — the button is polled every
    // loop on hardware, so bumping hideTimer unconditionally would defeat the
    // overlay auto-hide (and keep it covering the Tuner / X-Y readouts).
    if (oldSwitchState == true && switchState == false) {
        // Press edge: start timing the hold.
        pressStartMs = millis();
        hideTimer = millis();
    } else if (oldSwitchState == false && switchState == true) {
        // Release edge: decide short click vs long press.
        hideTimer = millis();
        unsigned long held = millis() - pressStartMs;
        if (held >= 800) {
            frozen = !frozen; // long press toggles freeze
        } else if (!param_select && ScopeParamAt(param) == P_LABELS) {
            showLabels = !showLabels; // on/off row: click flips it directly
        } else {
            param_select = param_select ? 0 : 1; // short click toggles edit
        }
    }
}
