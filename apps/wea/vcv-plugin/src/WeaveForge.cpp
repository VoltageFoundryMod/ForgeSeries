#include "engine/fw_engine.hpp"
#include "plugin.hpp"

#include "forgevcv/ForgeModule.hpp"
#include "forgevcv/widgets.hpp"

#include <functional>

struct WeaveForge : forgevcv::ForgeModule {
    enum ParamId {
        PARAMS_LEN
    };
    enum InputId {
        CLOCK_INPUT, // IN 1 — always the clock; see lib/cvInputs.hpp
        CV1_INPUT,   // IN 2 — assignable modulation
        CV2_INPUT,   // IN 3 — assignable modulation
        INPUTS_LEN
    };
    // The four jacks, in physical order: top-left, top-right, bottom-left,
    // bottom-right. The panel names them by column — A1/A2 down the left, B1/B2
    // down the right — because the DUO routing puts register A on the left and B
    // on the right, matching ChaosForge's generator columns.
    enum OutputId {
        A1_OUTPUT, // top-left      what each jack IS depends on ROUTING
        B1_OUTPUT, // top-right
        A2_OUTPUT, // bottom-left
        B2_OUTPUT, // bottom-right
        OUTPUTS_LEN
    };
    enum LightId {
        LIGHTS_LEN
    };

    // The concrete firmware engine. Held as a typed pointer for the curated param
    // bridge (context menu); the base owns and deletes it via ForgeModule::engine.
    wvengine::VcvEngine *wf = nullptr;

    WeaveForge() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configInput(CLOCK_INPUT, "Clock");
        configInput(CV1_INPUT, "Modulation CV 1");
        configInput(CV2_INPUT, "Modulation CV 2");
        // Named for the panel, not for what they carry: the output matrix
        // decides whether a jack is pitch, modulation, a gate or a trigger, so
        // a descriptive name would be wrong in two routings out of three.
        configOutput(A1_OUTPUT, "A1");
        configOutput(B1_OUTPUT, "B1");
        configOutput(A2_OUTPUT, "A2");
        configOutput(B2_OUTPUT, "B2");
        wf = new wvengine::VcvEngine();
        engine = wf; // base takes ownership
    }

    void process(const ProcessArgs &args) override {
        // These are modulation inputs rather than 1V/oct pitch, so the base
        // class's linear CvRange remap is exactly right — there is no
        // volts-per-octave relationship to preserve.
        float cv[2] = {
            mapCvInput(inputs[CV1_INPUT].getVoltage()),
            mapCvInput(inputs[CV2_INPUT].getVoltage())};
        // 1 V threshold, the Eurorack convention for a clock. The engine does its
        // own edge detection from this level, exactly as the ISR does on hardware.
        bool clock = inputs[CLOCK_INPUT].getVoltage() > 1.f;
        stepEngine(args.sampleTime, cv, 2, clock, 4);

        // Enum order IS jack order here, so the loop is honest — unlike
        // ChaosForge, whose port ids predate its column layout.
        for (int i = 0; i < 4; i++)
            outputs[A1_OUTPUT + i].setVoltage(outHold[i]);
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

// ── Generic slider for the context menu ──────────────────────────────────────
// Reads and writes a firmware parameter straight through the engine bridge.
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

struct WeaveForgeWidget : ModuleWidget {
    forgevcv::EncoderKnob *encoder = nullptr; // for the keyboard shortcuts (onHoverKey)

    WeaveForgeWidget(WeaveForge *module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/WeaveForge.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Jack positions match the shared Forge Series hardware, so these are the
        // same coordinates every other module in the series uses.
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.2405, 66.795)), module, WeaveForge::CLOCK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.153, 80.797)), module, WeaveForge::CV1_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.647, 80.797)), module, WeaveForge::CV2_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.412, 95.068)), module, WeaveForge::A1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.652, 95.068)), module, WeaveForge::B1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.407, 109.34)), module, WeaveForge::A2_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.647, 109.34)), module, WeaveForge::B2_OUTPUT));

        // Emulated OLED over the display cutout.
        forgevcv::FramebufferDisplay *disp = new forgevcv::FramebufferDisplay();
        disp->module = module;
        disp->box.size = mm2px(Vec(25.500, 14.000));
        disp->box.pos = mm2px(Vec(2.310, 19.800));
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

    // One register's submenu, mirroring the firmware's own REG A / REG B page.
    void appendRegisterMenu(Menu *menu, WeaveForge *m, int r) {
        wvengine::Engine *e = m->wf->raw();
        const char *name = (r == 0) ? "Register A" : "Register B";

        menu->addChild(createSubmenuItem(
            name,
            string::f("%d steps, %d%%", wvengine::lengthGet(e, r), wvengine::chanceGet(e, r)),
            [=](Menu *menu) {
                menu->addChild(createSubmenuItem(
                    "Length", string::f("%d", wvengine::lengthGet(e, r)), [=](Menu *menu) {
                        addIntSlider(menu, "Length", "", (float)wvengine::lengthMin(), (float)wvengine::lengthMax(), 16.f, [=]() { return wvengine::lengthGet(e, r); }, [=](int v) { wvengine::lengthSet(e, r, v); });
                    }));

                // 0 locks the pattern; 100 locks it inverted, at twice the
                // length. Both ends are settings, not extremes to avoid.
                menu->addChild(createSubmenuItem(
                    "Chance", string::f("%d%%", wvengine::chanceGet(e, r)), [=](Menu *menu) {
                        addIntSlider(menu, "Chance", "%", 0.f, 100.f, 25.f, [=]() { return wvengine::chanceGet(e, r); }, [=](int v) { wvengine::chanceSet(e, r, v); });
                    }));

                menu->addChild(new MenuSeparator);
                menu->addChild(createMenuItem("Randomize pattern", "",
                                              [=]() { wvengine::regRandomize(e, r); }));
                menu->addChild(createMenuItem("Invert pattern", "",
                                              [=]() { wvengine::regInvert(e, r); }));
                menu->addChild(createMenuItem("Clear pattern", "",
                                              [=]() { wvengine::regClear(e, r); }));
                menu->addChild(createMenuItem("Fill pattern", "",
                                              [=]() { wvengine::regFill(e, r); }));
            }));
    }

    // One jack's submenu — the six fields of its slot, with the two contextual
    // ones named and bounded by the engine because what they mean follows TYPE.
    void appendJackMenu(Menu *menu, WeaveForge *m, int j) {
        wvengine::Engine *e = m->wf->raw();

        const std::string summary =
            wvengine::outTypeName(wvengine::outTypeGet(e, j)) + " " +
            wvengine::outSourceName(wvengine::outSourceGet(e, j));

        menu->addChild(createSubmenuItem("Out " + wvengine::jackName(j), summary, [=](Menu *menu) {
            std::vector<std::string> sources;
            for (int s = 0; s < wvengine::outSourceCount(); s++)
                sources.push_back(wvengine::outSourceName(s));
            menu->addChild(createIndexSubmenuItem(
                "Source", sources,
                [=]() { return (size_t)wvengine::outSourceGet(e, j); },
                [=](size_t s) { wvengine::outSourceSet(e, j, (int)s); }));

            std::vector<std::string> types;
            for (int t = 0; t < wvengine::outTypeCount(); t++)
                types.push_back(wvengine::outTypeName(t));
            menu->addChild(createIndexSubmenuItem(
                "Type", types,
                [=]() { return (size_t)wvengine::outTypeGet(e, j); },
                [=](size_t t) { wvengine::outTypeSet(e, j, (int)t); }));

            menu->addChild(createSubmenuItem(
                "Depth", string::f("%d bits", wvengine::outDepthGet(e, j)), [=](Menu *menu) {
                    addIntSlider(menu, "Depth", " bits", 1.f, 8.f, 5.f, [=]() { return wvengine::outDepthGet(e, j); }, [=](int v) { wvengine::outDepthSet(e, j, v); });
                }));

            // Four jacks tapping one register at different offsets are four
            // phase-shifted copies of the same pattern — the canon trick.
            menu->addChild(createSubmenuItem(
                "Rotate", string::f("%d", wvengine::outRotateGet(e, j)), [=](Menu *menu) {
                    addIntSlider(menu, "Rotate", "", 0.f, (float)(wvengine::outRotateSpan(e, j) - 1), 0.f, [=]() { return wvengine::outRotateGet(e, j); }, [=](int v) { wvengine::outRotateSet(e, j, v); });
                }));

            const std::string l1 = wvengine::outParamLabel(e, j);
            const float lo1 = (float)wvengine::outParamMin(e, j);
            const float hi1 = (float)wvengine::outParamMax(e, j);
            menu->addChild(createSubmenuItem(
                l1, string::f("%d", wvengine::outParamGet(e, j)), [=](Menu *menu) {
                    addIntSlider(menu, l1, "", lo1, hi1, lo1, [=]() { return wvengine::outParamGet(e, j); }, [=](int v) { wvengine::outParamSet(e, j, v); });
                }));

            // A GATE has no second field, so the row is simply absent rather
            // than present and inert.
            const std::string l2 = wvengine::outParam2Label(e, j);
            if (!l2.empty()) {
                const float lo2 = (float)wvengine::outParam2Min(e, j);
                const float hi2 = (float)wvengine::outParam2Max(e, j);
                menu->addChild(createSubmenuItem(
                    l2, string::f("%d", wvengine::outParam2Get(e, j)), [=](Menu *menu) {
                        addIntSlider(menu, l2, "", lo2, hi2, lo2, [=]() { return wvengine::outParam2Get(e, j); }, [=](int v) { wvengine::outParam2Set(e, j, v); });
                    }));
            }
        }));
    }

    void appendContextMenu(Menu *menu) override {
        WeaveForge *m = dynamic_cast<WeaveForge *>(module);
        if (!m)
            return;
        wvengine::Engine *e = m->wf->raw();

        // ── Hardware: the host-side settings ─────────────────────────────────
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("WeaveForge Settings"));
        menu->addChild(createSubmenuItem("Hardware", "", [=](Menu *menu) {
            menu->addChild(createIndexPtrSubmenuItem(
                "Input CV Range", {"0..5V", "-5..5V", "0..10V"}, &m->cvRange));
            menu->addChild(createIndexPtrSubmenuItem(
                "Encoder Sensitivity", {"Low", "Medium", "High"}, &m->encoderSensitivity));
        }));

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Module Parameters"));

        // ── WEAVE: the module's signature control ────────────────────────────
        // At 0 the two registers are unrelated Turing Machines. Turned up they
        // trade bits; at 100 they are one ring as long as both together.
        menu->addChild(createSubmenuItem(
            "Weave", string::f("%d%%", wvengine::weaveGet(e)), [=](Menu *menu) {
                menu->addChild(createSubmenuItem(
                    "Amount", string::f("%d%%", wvengine::weaveGet(e)), [=](Menu *menu) {
                        addIntSlider(menu, "Weave", "%", 0.f, 100.f, 0.f, [=]() { return wvengine::weaveGet(e); }, [=](int v) { wvengine::weaveSet(e, v); });
                    }));

                std::vector<std::string> dirs;
                for (int d = 0; d < wvengine::weaveDirCount(); d++)
                    dirs.push_back(wvengine::weaveDirName(d));
                menu->addChild(createIndexSubmenuItem(
                    "Direction", dirs,
                    [=]() { return (size_t)wvengine::weaveDirGet(e); },
                    [=](size_t d) { wvengine::weaveDirSet(e, (int)d); }));
            }));

        for (int r = 0; r < 2; r++)
            appendRegisterMenu(menu, m, r);

        // ── The output matrix ────────────────────────────────────────────────
        // ROUTING first, because it is what makes the module two voices, one
        // voice plus modulation, or four drum tracks. It stamps the four slots
        // and steps out of the way; editing any of them below reads as Custom.
        const int routing = wvengine::routingGet(e);
        menu->addChild(createSubmenuItem(
            "Outputs", routing < 0 ? "Custom" : wvengine::routingName(routing),
            [=](Menu *menu) {
                std::vector<std::string> routings;
                for (int r = 0; r < wvengine::routingCount(); r++)
                    routings.push_back(wvengine::routingName(r));
                menu->addChild(createSubmenuItem(
                    "Routing", routing < 0 ? "Custom" : wvengine::routingName(routing),
                    [=](Menu *menu) {
                        for (int r = 0; r < wvengine::routingCount(); r++) {
                            const int idx = r;
                            menu->addChild(createCheckMenuItem(
                                routings[r], "",
                                [=]() { return wvengine::routingGet(e) == idx; },
                                [=]() { wvengine::routingSet(e, idx); }));
                        }
                    }));

                menu->addChild(new MenuSeparator);
                // Listed DOWN THE COLUMNS — A1, A2, B1, B2 — not in DAC index
                // order, which is the rows (A1, B1, A2, B2). jackAt() is the
                // firmware's own ordering, so this menu and the module's OUT
                // pages cannot disagree about it.
                //
                // The panel is labelled by column and the module is built around
                // that: each register owns a column in the default routing, which
                // is where the jack names come from (Design.md §1). Reading
                // across the rows puts B1 between A1 and A2, interleaving the two
                // halves of the module.
                for (int k = 0; k < 4; k++)
                    appendJackMenu(menu, m, wvengine::jackAt(k));
            }));

        // ── Clock ────────────────────────────────────────────────────────────
        menu->addChild(createSubmenuItem(
            "Clock",
            wvengine::clockIsExternal(e) ? "External"
                                         : string::f("%d BPM", wvengine::bpmGet(e)),
            [=](Menu *menu) {
                menu->addChild(createSubmenuItem(
                    "Internal tempo", string::f("%d BPM", wvengine::bpmGet(e)),
                    [=](Menu *menu) {
                        addIntSlider(menu, "Tempo", " BPM", (float)wvengine::bpmMin(), (float)wvengine::bpmMax(), 120.f, [=]() { return wvengine::bpmGet(e); }, [=](int v) { wvengine::bpmSet(e, v); });
                    }));

                std::vector<std::string> ppqns;
                for (int p = 0; p < wvengine::ppqnCount(); p++)
                    ppqns.push_back(wvengine::ppqnName(p));
                menu->addChild(createIndexSubmenuItem(
                    "Input PPQN", ppqns,
                    [=]() { return (size_t)wvengine::ppqnGet(e); },
                    [=](size_t p) { wvengine::ppqnSet(e, (int)p); }));

                // Steps per beat — "/4" one step every four beats, "x4" four
                // steps a beat — on the internal clock and an external one alike.
                std::vector<std::string> rates;
                for (int r = 0; r < wvengine::rateCount(); r++)
                    rates.push_back(wvengine::rateName(r));
                menu->addChild(createIndexSubmenuItem(
                    "Rate", rates,
                    [=]() { return (size_t)wvengine::rateGet(e); },
                    [=](size_t r) { wvengine::rateSet(e, (int)r); }));
            }));

        // ── Pitch ────────────────────────────────────────────────────────────
        menu->addChild(createSubmenuItem(
            "Scale",
            wvengine::noteName(wvengine::rootGet(e)) + " " +
                wvengine::scaleName(wvengine::scaleGet(e)),
            [=](Menu *menu) {
                std::vector<std::string> roots;
                for (int n = 0; n < 12; n++)
                    roots.push_back(wvengine::noteName(n));
                menu->addChild(createIndexSubmenuItem(
                    "Root", roots,
                    [=]() { return (size_t)wvengine::rootGet(e); },
                    [=](size_t n) { wvengine::rootSet(e, (int)n); }));

                std::vector<std::string> scales;
                for (int s = 0; s < wvengine::scaleCount(); s++)
                    scales.push_back(wvengine::scaleName(s));
                menu->addChild(createIndexSubmenuItem(
                    "Scale", scales,
                    [=]() { return (size_t)wvengine::scaleGet(e); },
                    [=](size_t s) { wvengine::scaleSet(e, (int)s); }));

                menu->addChild(createSubmenuItem(
                    "Transpose", string::f("%d st", wvengine::transposeGet(e)),
                    [=](Menu *menu) {
                        addIntSlider(menu, "Transpose", " st", -24.f, 24.f, 0.f, [=]() { return wvengine::transposeGet(e); }, [=](int v) { wvengine::transposeSet(e, v); });
                    }));
            }));

        // ── CV modulation matrix ─────────────────────────────────────────────
        menu->addChild(createSubmenuItem("CV Inputs", "", [=](Menu *menu) {
            std::vector<std::string> targets;
            for (int t = 0; t < wvengine::cvTargetCount(); t++)
                targets.push_back(wvengine::cvTargetName(t));

            for (int in = 0; in < 2; in++) {
                const int i = in;
                menu->addChild(createSubmenuItem(
                    string::f("CV %d", i + 1),
                    wvengine::cvTargetName(wvengine::cvTargetGet(e, i)), [=](Menu *menu) {
                        menu->addChild(createIndexSubmenuItem(
                            "Destination", targets,
                            [=]() { return (size_t)wvengine::cvTargetGet(e, i); },
                            [=](size_t t) { wvengine::cvTargetSet(e, i, (int)t); }));
                        menu->addChild(createSubmenuItem(
                            "Depth", string::f("%d%%", wvengine::cvDepthGet(e, i)),
                            [=](Menu *menu) {
                                addIntSlider(menu, "Depth", "%", 0.f, 100.f, 0.f, [=]() { return wvengine::cvDepthGet(e, i); }, [=](int v) { wvengine::cvDepthSet(e, i, v); });
                            }));
                    }));
            }
        }));
    }
};

Model *modelWeaveForge = createModel<WeaveForge, WeaveForgeWidget>("WeaveForge");
