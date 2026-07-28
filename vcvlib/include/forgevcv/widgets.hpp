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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace forgevcv {
using namespace rack;

// ── Emulated OLED: blits the firmware's 128x64 framebuffer via NanoVG ─────────
// The naive approach — upload 128x64 and let the GPU stretch it — only looks
// right when the widget happens to be wider than 128 screen pixels. A ForgeSeries
// screen cutout is ~35 mm ≈ 103 px at 100 % zoom, so the panel is *downscaling*:
// nearest sampling then drops one column in five and 1 px font stems vanish,
// while linear sampling smears them into mush. Hardware shows all 128x64 pixels,
// hence the crisper look.
//
// So we rasterize on the CPU straight to the destination resolution instead.
// Each output texel integrates the exact area of the OLED pixels it covers, so a
// stem that lands between two texels survives as two dimmer ones rather than
// disappearing. The texture is then blitted 1:1 onto device-pixel-snapped
// coordinates with nearest sampling, i.e. the GPU never resamples it at all.
//
// On top of that fidelity work:
//   * `pixelGap` leaves the inter-pixel grid of a real OLED dark, which is a big
//     part of why hardware reads as "sharp" rather than "smooth";
//   * coverage is gamma-encoded, because an OLED's light adds linearly while
//     NanoVG blends in sRGB — without it, partially covered pixels come out far
//     too dark and downscaled text looks anaemic;
//   * the lit pixels are drawn on Rack's light layer, so the screen keeps
//     glowing when the room is dimmed, like the real thing;
//   * the texture is rebuilt only when the framebuffer or the on-screen size
//     actually changes, which more than pays for the extra work per rebuild.
struct FramebufferDisplay : Widget {
    ForgeModule *module = nullptr;

    // ── Appearance ───────────────────────────────────────────────────────────
    NVGcolor litColor = nvgRGB(220, 220, 255); // emissive pixel colour
    NVGcolor bgColor = nvgRGB(6, 10, 16);      // unlit glass
    float pixelGap = 0.16f;                    // dark fraction of each cell, only once resolvable
    float sharpen = 4.f;                       // >1 snaps edges hard; 1 = plain coverage
    float gamma = 2.2f;                        // coverage -> alpha encode; 1 = linear (dimmer)
    float bloom = 0.f;                         // 0..1 light spill into the 4 neighbours
    bool glassSheen = false;                   // diagonal reflection; costs screen contrast

    // Set after changing any of the above at runtime to force a re-raster.
    bool dirty = true;

    // ── Cached rasterization ─────────────────────────────────────────────────
    int img = -1;
    int pendingDelete = -1; // freed at the top of the *next* frame, see below
    int texW = 0, texH = 0;
    std::vector<uint8_t> tex;
    uint8_t fbCache[1024] = {0};
    bool haveCache = false;

    // Area-exact resampling weights for one axis: `n` source cells, each lit
    // over the sub-interval [i+g, i+1-g], resampled to `m` destination samples.
    // Weights are normalized by the destination footprint, so they sum to the
    // fraction of that footprint covered by lit cells.
    struct AxisMap {
        int k = 0;              // max source cells touched by one destination sample
        std::vector<int> first; // [m] index of the first cell touched
        std::vector<float> w;   // [m * k], zero-padded

        void build(int n, int m, float g) {
            double step = (double)n / m;
            k = (int)std::ceil(step) + 1;
            first.assign(m, 0);
            w.assign((size_t)m * k, 0.f);
            for (int d = 0; d < m; d++) {
                double a = d * step, b = a + step;
                int i0 = std::max((int)std::floor(a), 0);
                int i1 = std::min((int)std::ceil(b) - 1, n - 1);
                i1 = std::max(i1, i0);
                first[d] = i0;
                for (int i = i0; i <= i1 && i - i0 < k; i++) {
                    double lo = std::max(a, (double)i + g);
                    double hi = std::min(b, (double)i + 1.0 - g);
                    if (hi > lo)
                        w[(size_t)d * k + (i - i0)] = (float)((hi - lo) / step);
                }
            }
        }
    };

    // Rasterize fbCache into `tex` at texW x texH. Separable: the horizontal
    // pass collapses 128 columns per source row, the vertical pass then folds
    // the 64 rows together.
    void rasterize() {
        // Destination texels per OLED pixel. Under 1 the screen physically
        // cannot show every pixel, which is what the gap fade, the gain and the
        // sharpen below all key off.
        float s = (float)texW / 128.f;

        // A dark inter-pixel grid only reads as a grid once there are enough
        // texels to draw one. Below that it is invisible and merely costs
        // ~(1-gap)^2 of the light — which is what made the emulated screen look
        // dimmer and softer than the hardware it is imitating.
        float fade = clamp((s - 4.f) / 4.f, 0.f, 1.f);
        fade = fade * fade * (3.f - 2.f * fade); // smoothstep over 4..8 texels
        float g = clamp(pixelGap, 0.f, 0.9f) * fade * 0.5f;

        AxisMap mx, my;
        mx.build(128, texW, g);
        my.build(64, texH, g);

        std::vector<float> tmp((size_t)64 * texW, 0.f);
        for (int j = 0; j < 64; j++) {
            const uint8_t *row = &fbCache[j * 16];
            float *out = &tmp[(size_t)j * texW];
            for (int dx = 0; dx < texW; dx++) {
                const float *wp = &mx.w[(size_t)dx * mx.k];
                int i0 = mx.first[dx];
                float s = 0.f;
                for (int t = 0; t < mx.k; t++) {
                    int i = i0 + t;
                    if (i >= 128)
                        break;
                    if (row[i >> 3] & (0x80 >> (i & 7)))
                        s += wp[t];
                }
                out[dx] = s;
            }
        }

        std::vector<float> cov((size_t)texW * texH, 0.f);
        for (int dy = 0; dy < texH; dy++) {
            const float *wp = &my.w[(size_t)dy * my.k];
            int j0 = my.first[dy];
            float *out = &cov[(size_t)dy * texW];
            for (int t = 0; t < my.k; t++) {
                int j = j0 + t;
                if (j >= 64)
                    break;
                if (wp[t] <= 0.f)
                    continue;
                const float *src = &tmp[(size_t)j * texW];
                for (int dx = 0; dx < texW; dx++)
                    out[dx] += wp[t] * src[dx];
            }
        }

        if (bloom > 0.f) {
            std::vector<float> b = cov;
            float f = clamp(bloom, 0.f, 1.f) * 0.25f;
            for (int y = 0; y < texH; y++)
                for (int x = 0; x < texW; x++) {
                    size_t o = (size_t)y * texW + x;
                    float n = (x > 0 ? b[o - 1] : 0.f) + (x < texW - 1 ? b[o + 1] : 0.f) +
                              (y > 0 ? b[o - texW] : 0.f) + (y < texH - 1 ? b[o + texW] : 0.f);
                    cov[o] = b[o] + f * n;
                }
        }

        // When downscaling, an isolated lit pixel can only ever cover `s` of a
        // texel, so without this a 1 px stem could never reach the full
        // brightness it has on hardware. Solid runs are already at 1 and clamp.
        float gain = (s < 1.f) ? std::min(1.f / std::max(s, 0.25f), 2.f) : 1.f;
        // Pull partial coverage away from the middle, so an edge that lands well
        // inside a texel goes fully on or off instead of grey. Safe to push hard:
        // the gain above pins an evenly-split 1 px stem to exactly 0.5, this
        // curve's pivot, so no amount of sharpening can make one disappear —
        // unlike the nearest-neighbour blit this replaced. Backed off as the gap
        // ring fades in, which is a soft edge we deliberately want to keep.
        float k = 1.f + (std::max(sharpen, 1.f) - 1.f) * (1.f - fade);

        // NanoVG premultiplies RGBA textures in the shader, so straight colour +
        // coverage-as-alpha is what it wants.
        tex.resize((size_t)texW * texH * 4);
        uint8_t cr = (uint8_t)std::lround(clamp(litColor.r, 0.f, 1.f) * 255.f);
        uint8_t cg = (uint8_t)std::lround(clamp(litColor.g, 0.f, 1.f) * 255.f);
        uint8_t cb = (uint8_t)std::lround(clamp(litColor.b, 0.f, 1.f) * 255.f);
        float inv = 1.f / std::max(gamma, 0.01f);
        for (size_t i = 0, n = (size_t)texW * texH; i < n; i++) {
            float c = clamp((cov[i] * gain - 0.5f) * k + 0.5f, 0.f, 1.f);
            tex[i * 4 + 0] = cr;
            tex[i * 4 + 1] = cg;
            tex[i * 4 + 2] = cb;
            tex[i * 4 + 3] = (uint8_t)std::lround(255.f * std::pow(c, inv));
        }
    }

    // The screen rect in local coordinates, plus its size in device pixels.
    // Snapped so the blit covers whole device pixels rather than straddling
    // them; both layers call this so the glass and the pixels can never drift.
    struct Rect {
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        int devW = 0, devH = 0;
    };

    Rect screenRect(const DrawArgs &args) const {
        Rect r;
        r.w = box.size.x;
        r.h = r.w * 64.f / 128.f; // 128x64 is 2:1, square pixels
        r.y = (box.size.y - r.h) / 2.f;

        // Where does that land in device pixels? Rack only ever applies scale +
        // translation, so the transform inverts trivially.
        float xf[6];
        nvgCurrentTransform(args.vg, xf);
        float pr = APP->window ? APP->window->pixelRatio : 1.f;
        bool aligned = std::abs(xf[1]) < 1e-4f && std::abs(xf[2]) < 1e-4f &&
                       xf[0] > 1e-6f && xf[3] > 1e-6f;
        if (!aligned) { // unexpected transform: don't snap, just size the texture
            float s = std::hypot(xf[0], xf[1]) * pr;
            r.devW = (int)(r.w * s);
            r.devH = (int)(r.h * s);
            return r;
        }

        float devW = std::max(std::round(xf[0] * r.w * pr), 8.f);
        float devH = std::max(std::round(xf[3] * r.h * pr), 4.f);
        float devX = std::round(xf[4] * pr);
        float devY = std::round((xf[5] + xf[3] * r.y) * pr);

        r.x = (devX / pr - xf[4]) / xf[0];
        r.y = (devY / pr - xf[5]) / xf[3];
        r.w = devW / (xf[0] * pr);
        r.h = devH / (xf[3] * pr);
        r.devW = (int)devW;
        r.devH = (int)devH;
        return r;
    }

    // Panel layer: the unlit glass. Dims with the room, unlike the pixels.
    void draw(const DrawArgs &args) override {
        if (box.size.x <= 0.f)
            return;
        Rect r = screenRect(args);

        nvgBeginPath(args.vg);
        nvgRect(args.vg, r.x, r.y, r.w, r.h);
        nvgFillColor(args.vg, bgColor);
        nvgFill(args.vg);

        if (glassSheen) {
            NVGpaint s = nvgLinearGradient(args.vg, r.x, r.y, r.x + r.w * 0.55f, r.y + r.h,
                                           nvgRGBA(255, 255, 255, 12), nvgRGBA(255, 255, 255, 0));
            nvgBeginPath(args.vg);
            nvgRect(args.vg, r.x, r.y, r.w, r.h);
            nvgFillPaint(args.vg, s);
            nvgFill(args.vg);
        }

        Widget::draw(args);
    }

    // Light layer: the lit pixels, so the screen keeps glowing in a dim room.
    void drawLayer(const DrawArgs &args, int layer) override {
        if (layer == 1)
            drawPixels(args);
        Widget::drawLayer(args, layer);
    }

    void drawPixels(const DrawArgs &args) {
        // Safe here: this image belongs to a previous frame's batch, which has
        // already been flushed. Deleting one mid-batch would draw nothing.
        if (pendingDelete >= 0) {
            nvgDeleteImage(args.vg, pendingDelete);
            pendingDelete = -1;
        }
        if (!module || box.size.x <= 0.f)
            return;
        Rect r = screenRect(args);

        // Below 3 device pixels per OLED pixel, rasterize 1:1 with the screen so
        // the GPU never resamples. Above it, an integer supersample keeps every
        // OLED pixel the same size; residual scaling error stays under 1/S px.
        int nw, nh;
        if (r.devW >= 128 * 3) {
            int s = std::min(std::max((int)std::lround(r.devW / 128.f), 3), 8);
            nw = 128 * s;
            nh = 64 * s;
        } else {
            nw = std::min(std::max(r.devW, 16), 383);
            nh = std::min(std::max(r.devH, 8), 191);
        }

        // Racy byte-wise read of fb (written on the audio thread) — a torn read
        // just means this frame shows a half-updated screen and the next one
        // catches up, which is also what the real I2C refresh does.
        bool changed = !haveCache || std::memcmp(fbCache, module->fb, sizeof(fbCache)) != 0;
        if (changed) {
            std::memcpy(fbCache, module->fb, sizeof(fbCache));
            haveCache = true;
        }
        if (nw != texW || nh != texH) {
            texW = nw;
            texH = nh;
            if (img >= 0) {
                pendingDelete = img; // freed next frame, never mid-batch
                img = -1;
            }
            dirty = true;
        }
        if (changed || dirty) {
            rasterize();
            dirty = false;
            if (img >= 0)
                nvgUpdateImage(args.vg, img, tex.data());
        }
        if (img < 0)
            img = nvgCreateImageRGBA(args.vg, texW, texH, NVG_IMAGE_NEAREST, tex.data());

        NVGpaint p = nvgImagePattern(args.vg, r.x, r.y, r.w, r.h, 0, img, 1.f);
        nvgBeginPath(args.vg);
        nvgRect(args.vg, r.x, r.y, r.w, r.h);
        nvgFillPaint(args.vg, p);
        nvgFill(args.vg);
    }

    void onContextDestroy(const ContextDestroyEvent &e) override {
        if (img >= 0)
            nvgDeleteImage(e.vg, img);
        if (pendingDelete >= 0)
            nvgDeleteImage(e.vg, pendingDelete);
        img = pendingDelete = -1;
        texW = texH = 0;
        Widget::onContextDestroy(e);
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
