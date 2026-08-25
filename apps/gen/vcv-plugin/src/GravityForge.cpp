#include "engine/fw_engine.hpp"
#include "plugin.hpp"

#include "forgevcv/ForgeModule.hpp"
#include "forgevcv/widgets.hpp"

#include <functional>

struct GravityForge : forgevcv::ForgeModule {
    enum ParamId {
        PARAMS_LEN
    };
    enum InputId {
        TRIG_INPUT, // IN 1 — role is menu-selectable (clock/reset/kick/spawn)
        CV1_INPUT,  // IN 2 — assignable modulation
        CV2_INPUT,  // IN 3 — assignable modulation
        INPUTS_LEN
    };
    enum OutputId {
        CVA_OUTPUT,
        CVB_OUTPUT,
        GATEA_OUTPUT,
        GATEB_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        LIGHTS_LEN
    };

    // The concrete firmware engine. Held as a typed pointer for the curated param
    // bridge (context menu); the base owns and deletes it via ForgeModule::engine.
    gfengine::VcvEngine *gf = nullptr;

    GravityForge() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configInput(TRIG_INPUT, "Clock / reset / kick / spawn");
        configInput(CV1_INPUT, "Modulation CV 1");
        configInput(CV2_INPUT, "Modulation CV 2");
        configOutput(CVA_OUTPUT, "Pitch CV A");
        configOutput(CVB_OUTPUT, "Pitch CV B");
        configOutput(GATEA_OUTPUT, "Gate / envelope A");
        configOutput(GATEB_OUTPUT, "Gate / envelope B");
        gf = new gfengine::VcvEngine();
        engine = gf; // base takes ownership
    }

    void process(const ProcessArgs &args) override {
        // Unlike NoteForge, these inputs are modulation rather than 1V/oct pitch,
        // so the base class's linear CvRange remap is exactly right here — there
        // is no volts-per-octave relationship to preserve.
        float cv[2] = {
            mapCvInput(inputs[CV1_INPUT].getVoltage()),
            mapCvInput(inputs[CV2_INPUT].getVoltage())};
        bool trig = inputs[TRIG_INPUT].getVoltage() > 1.f;
        stepEngine(args.sampleTime, cv, 2, trig, 4);

        for (int i = 0; i < 4; i++)
            outputs[CVA_OUTPUT + i].setVoltage(outHold[i]);
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
// Reads and writes an integer firmware parameter straight through the engine
// bridge. Both the quantity and the slider are forgevcv's shared ones.
static void addSlider(Menu *menu, const std::string &label, const std::string &unit,
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
    forgevcv::IntSlider *slider = new forgevcv::IntSlider; // shared wide slider
    slider->quantity = q;                                  // Slider takes ownership
    menu->addChild(slider);
}

struct GravityForgeWidget : ModuleWidget {
    forgevcv::EncoderKnob *encoder = nullptr; // for the keyboard shortcuts (see onHoverKey)

    GravityForgeWidget(GravityForge *module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/GravityForge.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Jack positions match the shared Forge Series hardware, so these are the
        // same coordinates NoteForge and ClockForge use.
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.2405, 66.795)), module, GravityForge::TRIG_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.153, 80.797)), module, GravityForge::CV1_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.647, 80.797)), module, GravityForge::CV2_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.412, 95.068)), module, GravityForge::CVA_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.652, 95.068)), module, GravityForge::CVB_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.407, 109.34)), module, GravityForge::GATEA_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.647, 109.34)), module, GravityForge::GATEB_OUTPUT));

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

    // Per-container submenu, mirroring the firmware's own menu pages.
    void appendContainerMenu(Menu *menu, GravityForge *m, int c) {
        gfengine::Engine *e = m->gf->raw();
        const char *name = (c == 0) ? "Container A" : "Container B";

        menu->addChild(createSubmenuItem(name, gfengine::currentNote(e, c), [=](Menu *menu) {
            // ── Physics ───────────────────────────────────────────────────────
            menu->addChild(createMenuLabel("Physics"));

            menu->addChild(createSubmenuItem("Gravity", string::f("%d", gfengine::gravityGet(e, c)),
                                             [=](Menu *menu) {
                                                 addSlider(menu, "Gravity", "", (float)gfengine::gravityMin(), (float)gfengine::gravityMax(), 220.f, [=]() { return gfengine::gravityGet(e, c); }, [=](int v) { gfengine::gravitySet(e, c, v); });
                                             }));

            menu->addChild(createSubmenuItem("Bounce", string::f("%d%%", gfengine::bounceGet(e, c)),
                                             [=](Menu *menu) {
                                                 addSlider(menu, "Bounce", "%", 10.f, 98.f, 72.f, [=]() { return gfengine::bounceGet(e, c); }, [=](int v) { gfengine::bounceSet(e, c, v); });
                                             }));

            // How strongly the moving wall drags a ball along with it — this is
            // what turns rotation into rhythm rather than decoration.
            menu->addChild(createSubmenuItem("Grip", string::f("%d%%", gfengine::gripGet(e, c)),
                                             [=](Menu *menu) {
                                                 addSlider(menu, "Grip", "%", 0.f, 100.f, 30.f, [=]() { return gfengine::gripGet(e, c); }, [=](int v) { gfengine::gripSet(e, c, v); });
                                             }));

            std::vector<std::string> spins;
            for (int s = 0; s < gfengine::spinCount(); s++)
                spins.push_back(gfengine::spinName(s) + " beats/rev");
            menu->addChild(createIndexSubmenuItem(
                "Spin", spins,
                [=]() { return (size_t)gfengine::spinGet(e, c); },
                [=](size_t s) { gfengine::spinSet(e, c, (int)s); }));

            menu->addChild(createBoolMenuItem(
                "Reverse", "",
                [=]() { return gfengine::reverseGet(e, c); },
                [=](bool v) { gfengine::reverseSet(e, c, v); }));

            menu->addChild(createSubmenuItem("Balls", string::f("%d", gfengine::ballsGet(e, c)),
                                             [=](Menu *menu) {
                                                 addSlider(menu, "Balls", "", (float)gfengine::ballsMin(), (float)gfengine::ballsMax(), 3.f, [=]() { return gfengine::ballsGet(e, c); }, [=](int v) { gfengine::ballsSet(e, c, v); });
                                             }));

            menu->addChild(new MenuSeparator);

            // ── Notes ─────────────────────────────────────────────────────────
            menu->addChild(createMenuLabel("Notes"));

            menu->addChild(createSubmenuItem("Pegs", string::f("%d", gfengine::pegsGet(e, c)),
                                             [=](Menu *menu) {
                                                 addSlider(menu, "Pegs", "", (float)gfengine::pegsMin(), (float)gfengine::pegsMax(), 8.f, [=]() { return gfengine::pegsGet(e, c); }, [=](int v) { gfengine::pegsSet(e, c, v); });
                                             }));

            // Muting individual pegs is how the rhythm is opened up — a muted peg
            // is a silent bounce, not a missing one.
            menu->addChild(createSubmenuItem("Active pegs", "", [=](Menu *menu) {
                int n = gfengine::pegsGet(e, c);
                for (int p = 0; p < n; p++) {
                    menu->addChild(createBoolMenuItem(
                        string::f("Peg %d", p + 1), "",
                        [=]() { return gfengine::pegEnabledGet(e, c, p); },
                        [=](bool v) { gfengine::pegEnabledSet(e, c, p, v); }));
                }
            }));

            std::vector<std::string> scales;
            for (int s = 0; s < gfengine::scaleCount(); s++)
                scales.push_back(gfengine::scaleName(s));
            menu->addChild(createIndexSubmenuItem(
                "Scale", scales,
                [=]() { return (size_t)gfengine::scaleGet(e, c); },
                [=](size_t s) { gfengine::scaleSet(e, c, (int)s); }));

            std::vector<std::string> roots;
            for (int n = 0; n < 12; n++)
                roots.push_back(gfengine::noteName(n));
            menu->addChild(createIndexSubmenuItem(
                "Root", roots,
                [=]() { return (size_t)gfengine::rootGet(e, c); },
                [=](size_t r) { gfengine::rootSet(e, c, (int)r); }));

            // ...and which octave that root sits in — the peg ring starts there
            // and opens upward, so this is the container's register. Named with
            // the 0V reference applied, so it matches the panel and the VCO.
            std::vector<std::string> rootOcts;
            for (int o = 0; o <= gfengine::rootOctaveMax(e, c); o++)
                rootOcts.push_back(string::f("%s%d", gfengine::noteName(gfengine::rootGet(e, c)).c_str(),
                                             gfengine::cvZeroOctaveGet(e) + o));
            menu->addChild(createIndexSubmenuItem(
                "Root octave", rootOcts,
                [=]() { return (size_t)gfengine::rootOctaveGet(e, c); },
                [=](size_t o) { gfengine::rootOctaveSet(e, c, (int)o); }));

            // How many octaves the peg ring covers, counted up from the root.
            // Widening it past the ceiling walks the root octave back down.
            std::vector<std::string> spreads;
            for (int o = gfengine::spreadMin(); o <= gfengine::spreadMax(); o++)
                spreads.push_back(string::f("%d octave%s", o, o == 1 ? "" : "s"));
            int spreadBase = gfengine::spreadMin();
            menu->addChild(createIndexSubmenuItem(
                "Spread", spreads,
                [=]() { return (size_t)(gfengine::spreadGet(e, c) - spreadBase); },
                [=](size_t o) { gfengine::spreadSet(e, c, (int)o + spreadBase); }));

            // Where the notes crowd inside that span. The lowest and highest peg
            // never move, so this is independent of Spread.
            menu->addChild(createSubmenuItem("Bias", string::f("%d", gfengine::biasGet(e, c)),
                                             [=](Menu *menu) {
                                                 addSlider(menu, "Bias (low <-> high)", "", (float)gfengine::biasMin(), (float)gfengine::biasMax(), 0.f, [=]() { return gfengine::biasGet(e, c); }, [=](int v) { gfengine::biasSet(e, c, v); });
                                             }));

            // The two note-thinning controls. They sit here, at the end of Notes,
            // because neither touches the physics: the balls move exactly as they
            // did and these decide which of their strikes you hear. That is what
            // separates them from every other way of getting fewer notes.
            menu->addChild(createSubmenuItem("Density", string::f("%d%%", gfengine::densityGet(e, c)),
                                             [=](Menu *menu) {
                                                 addSlider(menu, "Density", "%", 0.f, 100.f, 100.f, [=]() { return gfengine::densityGet(e, c); }, [=](int v) { gfengine::densitySet(e, c, v); });
                                             }));

            // Minimum gap between notes, in beats. A rate ceiling rather than a
            // grid — Clock ▸ Quantize is the one that moves notes onto a grid.
            std::vector<std::string> spaces;
            for (int s = 0; s < gfengine::spaceCount(); s++)
                spaces.push_back(gfengine::spaceName(s));
            menu->addChild(createIndexSubmenuItem(
                "Space (min gap, beats)", spaces,
                [=]() { return (size_t)gfengine::spaceGet(e, c); },
                [=](size_t s) { gfengine::spaceSet(e, c, (int)s); }));

            menu->addChild(new MenuSeparator);

            // ── Gate ──────────────────────────────────────────────────────────
            menu->addChild(createMenuLabel("Gate"));

            std::vector<std::string> gateModes;
            for (int g = 0; g < gfengine::gateModeCount(); g++)
                gateModes.push_back(gfengine::gateModeName(g));
            menu->addChild(createIndexSubmenuItem(
                "Gate mode", gateModes,
                [=]() { return (size_t)gfengine::gateModeGet(e, c); },
                [=](size_t g) { gfengine::gateModeSet(e, c, (int)g); }));

            menu->addChild(createSubmenuItem("Attack", string::f("%d ms", gfengine::attackGet(e, c)),
                                             [=](Menu *menu) {
                                                 addSlider(menu, "Attack", " ms", 0.f, (float)gfengine::attackMax(), 0.f, [=]() { return gfengine::attackGet(e, c); }, [=](int v) { gfengine::attackSet(e, c, v); });
                                             }));

            menu->addChild(createSubmenuItem("Decay", string::f("%d ms", gfengine::decayGet(e, c)),
                                             [=](Menu *menu) {
                                                 addSlider(menu, "Decay", " ms", 0.f, (float)gfengine::decayMax(), 320.f, [=]() { return gfengine::decayGet(e, c); }, [=](int v) { gfengine::decaySet(e, c, v); });
                                             }));

            menu->addChild(createSubmenuItem("Level", string::f("%d%%", gfengine::levelGet(e, c)),
                                             [=](Menu *menu) {
                                                 addSlider(menu, "Level", "%", 0.f, 100.f, 100.f, [=]() { return gfengine::levelGet(e, c); }, [=](int v) { gfengine::levelSet(e, c, v); });
                                             }));

            // How much a ball's impact speed scales the gate level.
            menu->addChild(createSubmenuItem("Accent", string::f("%d%%", gfengine::accentGet(e, c)),
                                             [=](Menu *menu) {
                                                 addSlider(menu, "Accent", "%", 0.f, 100.f, 0.f, [=]() { return gfengine::accentGet(e, c); }, [=](int v) { gfengine::accentSet(e, c, v); });
                                             }));
        }));
    }

    void appendContextMenu(Menu *menu) override {
        GravityForge *m = dynamic_cast<GravityForge *>(module);
        if (!m)
            return;
        gfengine::Engine *e = m->gf->raw();

        // ── Hardware: the host-side settings ──────────────────────────────────
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("GravityForge Settings"));
        menu->addChild(createSubmenuItem("Hardware", "", [=](Menu *menu) {
            // These inputs are modulation, not 1V/oct pitch, so a linear remap of
            // the incoming range is correct — unlike NoteForge, which may only
            // shift its pitch inputs.
            menu->addChild(createIndexPtrSubmenuItem(
                "Input CV Range", {"0..5V", "-5..5V", "0..10V"}, &m->cvRange));
            menu->addChild(createIndexPtrSubmenuItem(
                "Encoder Sensitivity", {"Low", "Medium", "High"}, &m->encoderSensitivity));

            // The note this module's 0 V stands for. Rack's own convention is C4
            // and that is the default; the entry exists because a rack full of
            // hardware emulations may not agree. The jacks are 0..5 V either
            // way — this names those volts, so the notes on the screen are the
            // ones the oscillator plays: a note shown as C5 leaves at (5 - n) V.
            const int refMin = gfengine::cvZeroOctaveMin();
            std::vector<std::string> refs;
            for (int o = refMin; o <= gfengine::cvZeroOctaveMax(); o++)
                refs.push_back(string::f("C%d", o));
            menu->addChild(createIndexSubmenuItem(
                "Note names: 0 V is", refs,
                [=]() { return (size_t)(gfengine::cvZeroOctaveGet(e) - refMin); },
                [=](size_t i) { gfengine::cvZeroOctaveSet(e, (int)i + refMin); }));
        }));

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Module Parameters"));

        // ── Coupling: the module's signature control ──────────────────────────
        // PROXIMITY slides the containers together. Apart they are independent;
        // overlapping, a wall strike in one shoves the balls in the other;
        // merged, they share one space.
        menu->addChild(createSubmenuItem("Coupling",
                                         string::f("%d%%", gfengine::proximityGet(e)),
                                         [=](Menu *menu) {
                                             menu->addChild(createSubmenuItem("Proximity", string::f("%d%%", gfengine::proximityGet(e)),
                                                                              [=](Menu *menu) {
                                                                                  addSlider(menu, "Proximity", "%", 0.f, 100.f, 0.f, [=]() { return gfengine::proximityGet(e); }, [=](int v) { gfengine::proximitySet(e, v); });
                                                                              }));
                                             menu->addChild(createSubmenuItem("Couple amount", string::f("%d%%", gfengine::couplingGet(e)),
                                                                              [=](Menu *menu) {
                                                                                  addSlider(menu, "Couple", "%", 0.f, 100.f, 60.f, [=]() { return gfengine::couplingGet(e); }, [=](int v) { gfengine::couplingSet(e, v); });
                                                                              }));
                                             menu->addChild(new MenuSeparator);
                                             menu->addChild(createMenuItem("Reset balls", "", [=]() { gfengine::resetBalls(e); }));
                                             menu->addChild(createMenuItem("Kick", "", [=]() { gfengine::kickBalls(e); }));
                                         }));

        // ── Clock ─────────────────────────────────────────────────────────────
        menu->addChild(createSubmenuItem("Clock", string::f("%d BPM", gfengine::effectiveBpm(e)),
                                         [=](Menu *menu) {
                                             menu->addChild(createSubmenuItem("Tempo", string::f("%d BPM", gfengine::bpmGet(e)),
                                                                              [=](Menu *menu) {
                                                                                  addSlider(menu, "Tempo", " BPM", (float)gfengine::bpmMin(), (float)gfengine::bpmMax(), 120.f, [=]() { return gfengine::bpmGet(e); }, [=](int v) { gfengine::bpmSet(e, v); });
                                                                              }));

                                             // The physics always free-runs; this only defers the resulting note
                                             // events onto a grid.
                                             std::vector<std::string> quants;
                                             for (int q = 0; q < gfengine::quantizeCount(); q++)
                                                 quants.push_back(gfengine::quantizeName(q));
                                             menu->addChild(createIndexSubmenuItem(
                                                 "Quantize hits", quants,
                                                 [=]() { return (size_t)gfengine::quantizeGet(e); },
                                                 [=](size_t q) { gfengine::quantizeSet(e, (int)q); }));

                                             std::vector<std::string> ppqns;
                                             for (int p = 0; p < gfengine::ppqnCount(); p++)
                                                 ppqns.push_back(gfengine::ppqnName(p) + " ppqn");
                                             menu->addChild(createIndexSubmenuItem(
                                                 "External clock", ppqns,
                                                 [=]() { return (size_t)gfengine::ppqnGet(e); },
                                                 [=](size_t p) { gfengine::ppqnSet(e, (int)p); }));

                                             std::vector<std::string> roles;
                                             for (int r = 0; r < gfengine::in1RoleCount(); r++)
                                                 roles.push_back(gfengine::in1RoleName(r));
                                             menu->addChild(createIndexSubmenuItem(
                                                 "IN 1 role", roles,
                                                 [=]() { return (size_t)gfengine::in1RoleGet(e); },
                                                 [=](size_t r) { gfengine::in1RoleSet(e, (int)r); }));
                                         }));

        // ── Loop / phrase mode ────────────────────────────────────────────────
        // The simulation is deterministic, so a phrase is just a snapshot plus a
        // step count. This is the one control that lets a patch you like stay.
        menu->addChild(createSubmenuItem(
            "Loop",
            gfengine::loopBeatsGet(e) == 0
                ? "Off"
                : string::f("%d beats", gfengine::loopBeatsGet(e)),
            [=](Menu *menu) {
                menu->addChild(createSubmenuItem(
                    "Length",
                    gfengine::loopBeatsGet(e) == 0
                        ? "Off"
                        : string::f("%d beats", gfengine::loopBeatsGet(e)),
                    [=](Menu *menu) {
                        addSlider(menu, "Length (0 = off)", " beats", 0.f, (float)gfengine::loopBeatsMax(), 0.f, [=]() { return gfengine::loopBeatsGet(e); }, [=](int v) { gfengine::loopBeatsSet(e, v); });
                    }));

                menu->addChild(createMenuItem("New phrase", "", [=]() { gfengine::loopNewPhrase(e); }));

                menu->addChild(new MenuSeparator);
                // Nap/wake mutes whole loops; shifting one container against the
                // other turns that into call-and-response.
                menu->addChild(createMenuLabel("Nap / wake"));

                menu->addChild(createSubmenuItem("Wake", string::f("%d loops", gfengine::loopWakeGet(e)),
                                                 [=](Menu *menu) {
                                                     addSlider(menu, "Wake", " loops", (float)gfengine::loopWakeMin(), (float)gfengine::loopWakeMax(), 1.f, [=]() { return gfengine::loopWakeGet(e); }, [=](int v) { gfengine::loopWakeSet(e, v); });
                                                 }));

                menu->addChild(createSubmenuItem(
                    "Nap",
                    gfengine::loopNapGet(e) == 0 ? "Off"
                                                 : string::f("%d loops", gfengine::loopNapGet(e)),
                    [=](Menu *menu) {
                        addSlider(menu, "Nap (0 = never)", " loops", 0.f, (float)gfengine::loopNapMax(), 0.f, [=]() { return gfengine::loopNapGet(e); }, [=](int v) { gfengine::loopNapSet(e, v); });
                    }));

                for (int c = 0; c < 2; c++) {
                    const char *label = (c == 0) ? "Shift A" : "Shift B";
                    menu->addChild(createSubmenuItem(label, string::f("%d loops", gfengine::loopShiftGet(e, c)),
                                                     [=](Menu *menu) {
                                                         addSlider(menu, label, " loops", 0.f, (float)gfengine::loopShiftMax(), 0.f, [=]() { return gfengine::loopShiftGet(e, c); }, [=](int v) { gfengine::loopShiftSet(e, c, v); });
                                                     }));
                }
            }));

        // ── CV modulation matrix ──────────────────────────────────────────────
        menu->addChild(createSubmenuItem("CV modulation", "", [=](Menu *menu) {
            for (int in = 0; in < 2; in++) {
                const char *jack = (in == 0) ? "IN 2" : "IN 3";
                menu->addChild(createSubmenuItem(
                    jack, gfengine::cvTargetName(gfengine::cvTargetGet(e, in)), [=](Menu *menu) {
                        std::vector<std::string> targets;
                        for (int t = 0; t < gfengine::cvTargetCount(); t++)
                            targets.push_back(gfengine::cvTargetName(t));
                        menu->addChild(createIndexSubmenuItem(
                            "Destination", targets,
                            [=]() { return (size_t)gfengine::cvTargetGet(e, in); },
                            [=](size_t t) { gfengine::cvTargetSet(e, in, (int)t); }));
                        menu->addChild(createSubmenuItem(
                            "Depth", string::f("%d%%", gfengine::cvDepthGet(e, in)), [=](Menu *menu) {
                                addSlider(menu, "Depth", "%", 0.f, 100.f, 0.f, [=]() { return gfengine::cvDepthGet(e, in); }, [=](int v) { gfengine::cvDepthSet(e, in, v); });
                            }));
                    }));
            }
        }));

        for (int c = 0; c < 2; c++)
            appendContainerMenu(menu, m, c);
    }
};

Model *modelGravityForge = createModel<GravityForge, GravityForgeWidget>("GravityForge");
