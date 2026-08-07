#include "engine/fw_engine.hpp"
#include "plugin.hpp"

#include "forgevcv/ForgeModule.hpp"
#include "forgevcv/widgets.hpp"

#include <functional>

struct ChaosForge : forgevcv::ForgeModule {
    enum ParamId {
        PARAMS_LEN
    };
    enum InputId {
        TRIG_INPUT, // IN 1 — role is menu-selectable (reset / reset A / B / freeze)
        CV1_INPUT,  // IN 2 — assignable modulation
        CV2_INPUT,  // IN 3 — assignable modulation
        INPUTS_LEN
    };
    enum OutputId {
        A1_OUTPUT, // generator A, first axis
        A2_OUTPUT, // generator A, second axis
        B1_OUTPUT, // generator B, first axis
        B2_OUTPUT, // generator B, second axis
        OUTPUTS_LEN
    };
    enum LightId {
        LIGHTS_LEN
    };

    // The concrete firmware engine. Held as a typed pointer for the curated param
    // bridge (context menu); the base owns and deletes it via ForgeModule::engine.
    chengine::VcvEngine *cf = nullptr;

    ChaosForge() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configInput(TRIG_INPUT, "Re-seed / freeze");
        configInput(CV1_INPUT, "Modulation CV 1");
        configInput(CV2_INPUT, "Modulation CV 2");
        configOutput(A1_OUTPUT, "Generator A, first axis");
        configOutput(A2_OUTPUT, "Generator A, second axis");
        configOutput(B1_OUTPUT, "Generator B, first axis");
        configOutput(B2_OUTPUT, "Generator B, second axis");
        cf = new chengine::VcvEngine();
        engine = cf; // base takes ownership
    }

    void process(const ProcessArgs &args) override {
        // These inputs are modulation rather than 1V/oct pitch, so the base
        // class's linear CvRange remap is exactly right here — there is no
        // volts-per-octave relationship to preserve.
        float cv[2] = {
            mapCvInput(inputs[CV1_INPUT].getVoltage()),
            mapCvInput(inputs[CV2_INPUT].getVoltage())};
        bool trig = inputs[TRIG_INPUT].getVoltage() > 1.f;
        stepEngine(args.sampleTime, cv, 2, trig, 4);

        // outHold is in DAC/jack order — top-left, top-right, bottom-left,
        // bottom-right — and the generators are columns, so it is not the enum's
        // order. Mapped by name rather than by `A1_OUTPUT + i`, because the two
        // orders genuinely differ and a loop would silently cross the pairs.
        //
        // The PORT IDS are deliberately left alone. A Rack patch stores cables by
        // port id, so keeping A2 as id 1 means a patch made before the columns
        // change still has its cable on generator A's second axis — it is drawn
        // in a different place on the panel, but it is carrying the same signal.
        // Renumbering instead would have preserved the position and swapped the
        // signal, which is the worse half to keep.
        outputs[A1_OUTPUT].setVoltage(outHold[0]); // top-left
        outputs[B1_OUTPUT].setVoltage(outHold[1]); // top-right
        outputs[A2_OUTPUT].setVoltage(outHold[2]); // bottom-left
        outputs[B2_OUTPUT].setVoltage(outHold[3]); // bottom-right
    }

    json_t *dataToJson() override {
        json_t *root = json_object();
        baseToJson(root); // eeprom blob + cvRange + encoderSensitivity
        return root;
    }

    void dataFromJson(json_t *root) override {
        baseFromJson(root);
    }
};

// ── Generic sliders for the context menu ─────────────────────────────────────
// Both read and write a firmware parameter straight through the engine bridge.
// The quantities and the sliders are forgevcv's shared ones.
static void addIntSlider(Menu *menu, const std::string &label, const std::string &unit,
                         float minV, float maxV, float defV,
                         std::function<int()> get, std::function<void(int)> set) {
    forgevcv::EngineIntQuantity *q = new forgevcv::EngineIntQuantity;
    q->label = label;
    q->unit = unit;
    q->minV = minV;
    q->maxV = maxV;
    q->defV = defV;
    q->getFn = get;
    q->setFn = set;
    forgevcv::IntSlider *slider = new forgevcv::IntSlider;
    slider->quantity = q; // Slider takes ownership
    menu->addChild(slider);
}

static void addFloatSlider(Menu *menu, const std::string &label, const std::string &unit,
                           float minV, float maxV, float defV,
                           std::function<float()> get, std::function<void(float)> set) {
    forgevcv::EngineFloatQuantity *q = new forgevcv::EngineFloatQuantity;
    q->label = label;
    q->unit = unit;
    q->minV = minV;
    q->maxV = maxV;
    q->defV = defV;
    q->getFn = get;
    q->setFn = set;
    forgevcv::FloatSlider *slider = new forgevcv::FloatSlider;
    slider->quantity = q; // Slider takes ownership
    menu->addChild(slider);
}

// A system parameter shown with as many decimals as it needs — the twelve
// systems' constants span four orders of magnitude, so a fixed format is either
// unreadable at one end or a lie at the other. Mirrors FmtParam() on the panel.
static std::string fmtParam(float v) {
    const float a = std::fabs(v);
    if (a < 0.01f)
        return string::f("%.4f", v);
    if (a < 1.f)
        return string::f("%.3f", v);
    if (a < 10.f)
        return string::f("%.2f", v);
    return string::f("%.1f", v);
}

static std::string fmtSpeed(float v) {
    return (v < 10.f) ? string::f("%.2fx", v) : string::f("%.1fx", v);
}

struct ChaosForgeWidget : ModuleWidget {
    forgevcv::EncoderKnob *encoder = nullptr; // for the keyboard shortcuts (see onHoverKey)

    ChaosForgeWidget(ChaosForge *module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/ChaosForge.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Jack positions match the shared Forge Series hardware, so these are the
        // same coordinates every other module in the series uses.
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.2405, 66.795)), module, ChaosForge::TRIG_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.153, 80.797)), module, ChaosForge::CV1_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.647, 80.797)), module, ChaosForge::CV2_INPUT));

        // Generator A down the left column, B down the right — see the jack map
        // in lib/engine.hpp. The panel silkscreen reads A1/A2 on the left and
        // B1/B2 on the right to match.
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.412, 95.068)), module, ChaosForge::A1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.652, 95.068)), module, ChaosForge::B1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.407, 109.34)), module, ChaosForge::A2_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.647, 109.34)), module, ChaosForge::B2_OUTPUT));

        // Emulated OLED over the display cutout.
        forgevcv::FramebufferDisplay *disp = new forgevcv::FramebufferDisplay();
        disp->module = module;
        disp->box.pos = mm2px(Vec(2.559, 19.776));
        disp->box.size = mm2px(Vec(25.362, 14.994));
        addChild(disp);

        // Encoder (drag to scroll, click to select).
        forgevcv::EncoderKnob *enc = new forgevcv::EncoderKnob();
        enc->module = module;
        enc->box.size = mm2px(Vec(9.0, 9.0));
        enc->box.pos = mm2px(Vec(15.24, 50.918)).minus(enc->box.size.div(2));
        addChild(enc);
        encoder = enc;
    }

    // ── Keyboard shortcuts (while hovering the module) ───────────────────────
    // '[' / ']' turn the encoder one detent, space pushes it. Bracket keys are
    // matched by keyName so they follow the user's keyboard layout; space has no
    // printable name, so it is matched by keycode.
    void onHoverKey(const event::HoverKey &e) override {
        if (encoder && (e.mods & RACK_MOD_MASK) == 0) {
            if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
                if (e.keyName == "[") {
                    encoder->emit(-1);
                    e.consume(this);
                    return;
                }
                if (e.keyName == "]") {
                    encoder->emit(+1);
                    e.consume(this);
                    return;
                }
            }
            // Push on press only: no auto-repeat, so holding space is one click.
            if (e.action == GLFW_PRESS && e.key == GLFW_KEY_SPACE) {
                encoder->push();
                e.consume(this);
                return;
            }
        }
        ModuleWidget::onHoverKey(e);
    }

    // Per-generator submenu, mirroring the firmware's own two pages.
    void appendGeneratorMenu(Menu *menu, ChaosForge *m, int g) {
        chengine::Engine *e = m->cf->raw();
        const char *name = (g == 0) ? "Generator A (A1 + A2)" : "Generator B (B1 + B2)";

        menu->addChild(createSubmenuItem(
            name, chengine::systemName(chengine::systemGet(e, g)), [=](Menu *menu) {
                // ── The system ────────────────────────────────────────────────
                std::vector<std::string> systems;
                for (int s = 0; s < chengine::systemCount(); s++)
                    systems.push_back(chengine::systemName(s));
                menu->addChild(createIndexSubmenuItem(
                    "Attractor", systems,
                    [=]() { return (size_t)chengine::systemGet(e, g); },
                    [=](size_t s) { chengine::systemSet(e, g, (int)s); }));

                // A multiplier on the rate the system was catalogued at, so the
                // number means the same thing whichever system is selected.
                menu->addChild(createSubmenuItem(
                    "Speed", fmtSpeed(chengine::speedGet(e, g)), [=](Menu *menu) {
                        addFloatSlider(menu, "Speed", "x", chengine::speedMin(),
                                       chengine::speedMax(), 1.f,
                                       [=]() { return chengine::speedGet(e, g); },
                                       [=](float v) { chengine::speedSet(e, g, v); });
                    }));

                // ── The system's own constants ────────────────────────────────
                // Built from the engine, because how many there are, what they
                // are called and what they are allowed to be all depend on which
                // system is selected right now.
                const int np = chengine::paramCount(e, g);
                if (np > 0) {
                    menu->addChild(new MenuSeparator);
                    menu->addChild(createMenuLabel("Constants"));
                    for (int k = 0; k < np; k++) {
                        const std::string pname = chengine::paramName(e, g, k);
                        const float lo = chengine::paramMin(e, g, k);
                        const float hi = chengine::paramMax(e, g, k);
                        const float def = chengine::paramDefault(e, g, k);
                        menu->addChild(createSubmenuItem(
                            pname, fmtParam(chengine::paramGet(e, g, k)), [=](Menu *menu) {
                                addFloatSlider(menu, pname, "", lo, hi, def,
                                               [=]() { return chengine::paramGet(e, g, k); },
                                               [=](float v) { chengine::paramSet(e, g, k, v); });
                            }));
                    }
                }

                menu->addChild(new MenuSeparator);

                // ── How it is heard ───────────────────────────────────────────
                menu->addChild(createMenuLabel("Output"));

                std::vector<std::string> axes;
                for (int a = 0; a < chengine::axisCount(); a++)
                    axes.push_back(chengine::axisName(a));
                for (int j = 0; j < 2; j++) {
                    const std::string label =
                        string::f("Out %d follows", g * 2 + j + 1);
                    menu->addChild(createIndexSubmenuItem(
                        label, axes,
                        [=]() { return (size_t)chengine::srcGet(e, g, j); },
                        [=](size_t a) { chengine::srcSet(e, g, j, (int)a); }));
                }

                menu->addChild(createSubmenuItem(
                    "Level", string::f("%d%%", chengine::levelGet(e, g)), [=](Menu *menu) {
                        addIntSlider(menu, "Level", "%", 0.f, 100.f, 100.f,
                                     [=]() { return chengine::levelGet(e, g); },
                                     [=](int v) { chengine::levelSet(e, g, v); });
                    }));

                menu->addChild(createSubmenuItem(
                    "Offset", string::f("%d%%", chengine::offsetGet(e, g)), [=](Menu *menu) {
                        addIntSlider(menu, "Offset", "%", -100.f, 100.f, 0.f,
                                     [=]() { return chengine::offsetGet(e, g); },
                                     [=](int v) { chengine::offsetSet(e, g, v); });
                    }));

                // A one-pole lag on the pair. Rounds the one fast event some of
                // these systems have (Rössler's z fold) into a swell.
                menu->addChild(createSubmenuItem(
                    "Smooth", string::f("%d%%", chengine::smoothGet(e, g)), [=](Menu *menu) {
                        addIntSlider(menu, "Smooth", "%", 0.f, 100.f, 0.f,
                                     [=]() { return chengine::smoothGet(e, g); },
                                     [=](int v) { chengine::smoothSet(e, g, v); });
                    }));

                // FIXED scales the jack by the window measured at the system's
                // published constants — predictable and identical every boot.
                // AUTO tracks the orbit's own window, which is what you want once
                // the constants have been moved far enough that the figure no
                // longer fills the jack.
                menu->addChild(createBoolMenuItem(
                    "Auto range", "",
                    [=]() { return chengine::autoRangeGet(e, g); },
                    [=](bool v) { chengine::autoRangeSet(e, g, v); }));

                menu->addChild(new MenuSeparator);
                menu->addChild(createMenuItem("Re-seed this generator", "",
                                              [=]() { chengine::reseedGen(e, g); }));
            }));
    }

    void appendContextMenu(Menu *menu) override {
        ChaosForge *m = dynamic_cast<ChaosForge *>(module);
        if (!m)
            return;
        chengine::Engine *e = m->cf->raw();

        // ── Hardware: the host-side settings ──────────────────────────────────
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("ChaosForge Settings"));
        menu->addChild(createSubmenuItem("Hardware", "", [=](Menu *menu) {
            menu->addChild(createIndexPtrSubmenuItem(
                "Input CV Range", {"0..5V", "-5..5V", "0..10V"}, &m->cvRange));
            menu->addChild(createIndexPtrSubmenuItem(
                "Encoder Sensitivity", {"Low", "Medium", "High"}, &m->encoderSensitivity));

            std::vector<std::string> views;
            for (int v = 0; v < chengine::viewCount(); v++)
                views.push_back(chengine::viewName(v));
            menu->addChild(createIndexSubmenuItem(
                "Screen shows", views,
                [=]() { return (size_t)chengine::viewGet(e); },
                [=](size_t v) { chengine::viewSet(e, (int)v); }));
        }));

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Module Parameters"));

        // ── Link: the module's signature control ──────────────────────────────
        // At 0 the two orbits are unrelated. Turned up they entrain — sharing
        // their timing while keeping their own shapes — and near the top, two
        // copies of one system lock into a single orbit.
        menu->addChild(createSubmenuItem(
            "Link", string::f("%d%%", chengine::coupleGet(e)), [=](Menu *menu) {
                menu->addChild(createSubmenuItem(
                    "Couple", string::f("%d%%", chengine::coupleGet(e)), [=](Menu *menu) {
                        addIntSlider(menu, "Couple", "%", 0.f, 100.f, 0.f,
                                     [=]() { return chengine::coupleGet(e); },
                                     [=](int v) { chengine::coupleSet(e, v); });
                    }));

                menu->addChild(new MenuSeparator);

                std::vector<std::string> roles;
                for (int r = 0; r < chengine::in1RoleCount(); r++)
                    roles.push_back(chengine::in1RoleName(r));
                menu->addChild(createIndexSubmenuItem(
                    "IN 1 role", roles,
                    [=]() { return (size_t)chengine::in1RoleGet(e); },
                    [=](size_t r) { chengine::in1RoleSet(e, (int)r); }));

                menu->addChild(new MenuSeparator);
                // On a system this sensitive, re-seeding is not "the same
                // pattern again": the two orbits are seeded a thousandth apart
                // and are somewhere else entirely within seconds. It is a fresh
                // draw from the same figure.
                menu->addChild(createMenuItem("Re-seed both", "",
                                              [=]() { chengine::reseedAll(e); }));
            }));

        // ── CV modulation matrix ──────────────────────────────────────────────
        menu->addChild(createSubmenuItem("CV modulation", "", [=](Menu *menu) {
            for (int in = 0; in < 2; in++) {
                const char *jack = (in == 0) ? "IN 2" : "IN 3";
                menu->addChild(createSubmenuItem(
                    jack, chengine::cvTargetName(chengine::cvTargetGet(e, in)), [=](Menu *menu) {
                        std::vector<std::string> targets;
                        for (int t = 0; t < chengine::cvTargetCount(); t++)
                            targets.push_back(chengine::cvTargetName(t));
                        menu->addChild(createIndexSubmenuItem(
                            "Destination", targets,
                            [=]() { return (size_t)chengine::cvTargetGet(e, in); },
                            [=](size_t t) { chengine::cvTargetSet(e, in, (int)t); }));
                        menu->addChild(createSubmenuItem(
                            "Depth", string::f("%d%%", chengine::cvDepthGet(e, in)),
                            [=](Menu *menu) {
                                addIntSlider(menu, "Depth", "%", 0.f, 100.f, 0.f,
                                             [=]() { return chengine::cvDepthGet(e, in); },
                                             [=](int v) { chengine::cvDepthSet(e, in, v); });
                            }));
                    }));
            }
        }));

        for (int g = 0; g < 2; g++)
            appendGeneratorMenu(menu, m, g);
    }
};

Model *modelChaosForge = createModel<ChaosForge, ChaosForgeWidget>("ChaosForge");
