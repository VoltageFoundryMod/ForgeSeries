#pragma once
// forgevcv widgets — the reusable Rack UI pieces shared by ForgeSeries plugins:
//   FramebufferDisplay : blits the firmware's 128x64 framebuffer as a crisp OLED.
//   EncoderKnob        : drag-to-rotate / click-to-push hardware encoder.
//   BpmSlider          : a wide horizontal slider for a tempo submenu.
//
// All three couple only to forgevcv::ForgeModule (fb + the encoder event queue +
// encoderPixelsPerDetent), so any firmware's module works with them unchanged.
// Member functions are defined in-class (implicitly inline) so this header is
// ODR-safe even if several module translation units include it.

#include <rack.hpp>

#include "ForgeModule.hpp"

#include <cmath>

namespace forgevcv {
using namespace rack;

// ── Emulated OLED: blits the firmware's 128x64 framebuffer via NanoVG ─────────
// A 128x64 texture sampled nearest-neighbor => crisp pixels at any zoom.
// The image is created once and updated each frame (NEVER deleted inside draw():
// NanoVG batches draws and flushes at end-of-frame, so deleting here renders
// nothing).
struct FramebufferDisplay : Widget {
    ForgeModule *module = nullptr;
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
    ForgeModule *module = nullptr;
    float accum = 0.f;    // sub-detent rotation accumulator
    float pathLen = 0.f;  // total drag distance, to distinguish click from turn
    float visAngle = 0.f; // visual indicator angle

    // Queue rotation detents for the engine and spin the graphic. Also called
    // from the widget's keyboard shortcuts so both input paths stay in sync.
    void emit(int steps) {
        if (module)
            module->encDelta.fetch_add(steps);
        visAngle -= steps * 0.35f;
    }

    void push() {
        if (module)
            module->encClick.fetch_add(1);
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
        if (pathLen < 3.f) // negligible movement -> treat as a push
            push();
    }

    void draw(const DrawArgs &args) override {
        float r = box.size.x / 2.f;
        Vec c = box.size.div(2.f);
        // Outer metallic rim: subtle highlight/shadow so it reads as hardware.
        float rimOuter = r - 0.45f;
        float rimInner = r - 2.8f;
        float rimCenter = (rimOuter + rimInner) * 0.5f;

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, c.x, c.y, rimOuter);
        nvgCircle(args.vg, c.x, c.y, rimInner);
        nvgPathWinding(args.vg, NVG_HOLE);
        nvgFillColor(args.vg, nvgRGB(128, 130, 138));
        nvgFill(args.vg);

        NVGpaint rimSheen = nvgLinearGradient(
            args.vg, c.x - r, c.y - r, c.x + r, c.y + r,
            nvgRGBA(255, 255, 255, 70), nvgRGBA(255, 255, 255, 0));
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, c.x, c.y, rimOuter);
        nvgCircle(args.vg, c.x, c.y, rimInner);
        nvgPathWinding(args.vg, NVG_HOLE);
        nvgFillPaint(args.vg, rimSheen);
        nvgFill(args.vg);

        NVGpaint rimShade = nvgLinearGradient(
            args.vg, c.x + r, c.y + r, c.x - r, c.y - r,
            nvgRGBA(0, 0, 0, 58), nvgRGBA(0, 0, 0, 0));
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, c.x, c.y, rimOuter);
        nvgCircle(args.vg, c.x, c.y, rimInner);
        nvgPathWinding(args.vg, NVG_HOLE);
        nvgFillPaint(args.vg, rimShade);
        nvgFill(args.vg);

        // Bright and dark edge accents make the rim pop at small sizes.
        nvgBeginPath(args.vg);
        nvgArc(args.vg, c.x, c.y, rimCenter, 3.2f, 4.9f, NVG_CW);
        nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 96));
        nvgStrokeWidth(args.vg, 1.1f);
        nvgStroke(args.vg);

        nvgBeginPath(args.vg);
        nvgArc(args.vg, c.x, c.y, rimCenter, 0.25f, 1.85f, NVG_CW);
        nvgStrokeColor(args.vg, nvgRGBA(0, 0, 0, 120));
        nvgStrokeWidth(args.vg, 1.2f);
        nvgStroke(args.vg);

        float bodyR = rimInner - 0.35f;
        // Knob body
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, c.x, c.y, bodyR);
        nvgFillColor(args.vg, nvgRGB(45, 45, 50));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGB(20, 20, 22));
        nvgStrokeWidth(args.vg, 1.2f);
        nvgStroke(args.vg);

        // Knurled grip: evenly spaced notches around the rim. Because the
        // pattern is rotationally symmetric there is no "start/end" position
        // (unlike a pointer line) but it visibly spins as the encoder turns.
        const int teeth = 12;
        float rOut = bodyR - 0.55f;
        float rIn = bodyR * 0.64f;
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

// ── Wide horizontal slider for a value submenu (e.g. tempo) ───────────────────
// Pair with a firmware-specific rack::Quantity that reads/writes through that
// module's parameter bridge.
struct BpmSlider : ui::Slider {
    BpmSlider() { box.size.x = 200.f; }
};

} // namespace forgevcv
