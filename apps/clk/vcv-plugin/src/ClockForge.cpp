#include "engine/fw_engine.hpp"
#include "expander_message.hpp"
#include "plugin.hpp"

#include "forgevcv/ForgeModule.hpp"
#include "forgevcv/widgets.hpp"

struct ClockForge : forgevcv::ForgeModule {
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
        LED1_LIGHT,
        LED2_LIGHT,
        LED3_LIGHT,
        LED4_LIGHT,
        LIGHTS_LEN
    };

    // The concrete firmware engine. Held as a typed pointer for the curated param
    // bridge (context menu); the base owns and deletes it via ForgeModule::engine.
    cfengine::VcvEngine *cf = nullptr;

    // Expander link. Buffers on both sides because the expander may sit on
    // either — see expander_message.hpp.
    ForgeExpanderMessage leftMessages[2];
    ForgeExpanderMessage rightMessages[2];
    bool expanderAdjacent = false; // mirrored for the context menu

    // The firmware's EXPANDER setting, cached. Reading it through the bridge
    // takes the engine globals lock, which is not something to do per sample;
    // it only changes when someone turns the encoder, so a poll every few
    // hundred samples is imperceptible and costs nothing.
    bool engineExpanderOn = false;
    int expanderPoll = 0;

    ClockForge() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configInput(CLKIN_INPUT, "Clock (TRIG)");
        configInput(CV1IN_INPUT, "CV 1");
        configInput(CV2IN_INPUT, "CV 2");
        configOutput(OUT1_OUTPUT, "Out 1");
        configOutput(OUT2_OUTPUT, "Out 2");
        configOutput(OUT3_OUTPUT, "Out 3");
        configOutput(OUT4_OUTPUT, "Out 4");
        configLight(LED1_LIGHT, "Out 1 level");
        configLight(LED2_LIGHT, "Out 2 level");
        configLight(LED3_LIGHT, "Out 3 level");
        configLight(LED4_LIGHT, "Out 4 level");
        cf = new cfengine::VcvEngine();
        engine = cf; // base takes ownership

        leftExpander.producerMessage = &leftMessages[0];
        leftExpander.consumerMessage = &leftMessages[1];
        rightExpander.producerMessage = &rightMessages[0];
        rightExpander.consumerMessage = &rightMessages[1];
    }

    // Which side is the expander on, if any.
    Expander *expanderSide() {
        if (rightExpander.module && rightExpander.module->model == modelClockForgeExpander)
            return &rightExpander;
        if (leftExpander.module && leftExpander.module->model == modelClockForgeExpander)
            return &leftExpander;
        return nullptr;
    }

    void process(const ProcessArgs &args) override {
        Expander *side = expanderSide();
        expanderAdjacent = (side != nullptr);

        if (--expanderPoll <= 0) {
            expanderPoll = 512;
            engineExpanderOn = (cfengine::expanderType(cf->raw()) != 0);
        }

        // IN 4 lives on the expander; without one it reads 0 V, exactly as an
        // unpatched jack would.
        float in4 = 0.f;
        if (side) {
            if (const ForgeExpanderMessage *m =
                    static_cast<const ForgeExpanderMessage *>(side->consumerMessage))
                in4 = m->in;
        }

        float cv[3] = {
            mapCvInput(inputs[CV1IN_INPUT].getVoltage()),
            mapCvInput(inputs[CV2IN_INPUT].getVoltage()),
            mapCvInput(in4)};
        bool clk = inputs[CLKIN_INPUT].getVoltage() > 1.f;
        // Always ask for eight: the engine fills only what its EXPANDER
        // setting makes live, and outHold's upper half stays at zero otherwise.
        stepEngine(args.sampleTime, cv, 3, clk, forgevcv::FORGE_MAX_OUT);

        for (int i = 0; i < 4; i++)
            outputs[OUT1_OUTPUT + i].setVoltage(outHold[i]);

        // Panel LEDs sit on the output pins themselves — see updateOutputLights.
        updateOutputLights(LED1_LIGHT, 4, args.sampleTime);

        // Hand outputs 5-8 to the expander, writing into ITS near-side
        // producer buffer and flipping it there.
        if (side && side->module) {
            Expander &facing = (side == &rightExpander) ? side->module->leftExpander
                                                        : side->module->rightExpander;
            if (facing.producerMessage) {
                ForgeExpanderMessage *m =
                    static_cast<ForgeExpanderMessage *>(facing.producerMessage);
                m->parentActive = engineExpanderOn;
                for (int i = 0; i < 4; i++)
                    m->out[i] = outHold[4 + i];
                facing.requestMessageFlip();
            }
        }
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

// ── BPM slider for the context menu ──────────────────────────────────────────
// Reads and writes BPM straight through the engine bridge, driven by the shared
// forgevcv::IntSlider inside a submenu.
static forgevcv::EngineIntQuantity *bpmQuantity(ClockForge *m) {
    cfengine::Engine *e = m->cf->raw();
    forgevcv::EngineIntQuantity *q = new forgevcv::EngineIntQuantity;
    q->label = "Tempo";
    q->unit = " BPM";
    q->minV = (float)cfengine::bpmMin();
    q->maxV = (float)cfengine::bpmMax();
    q->defV = 120.f;
    q->precision = 3;
    q->getFn = [=]() { return cfengine::bpm(e); };
    q->setFn = [=](int v) { cfengine::setBpm(e, v); };
    return q;
}

// Create the expander, drop it immediately to the right of `m`, and switch the
// firmware setting on. Both halves go into one undo entry: the add, and the
// shove that setModulePosForce may give the modules already sitting there.
// Without the second, Ctrl+Z removes the module and leaves the rack rearranged.
static void addExpanderBeside(ClockForge *m) {
    ModuleWidget *mw = APP->scene->rack->getModule(m->id);
    if (!mw)
        return;

    engine::Module *em = modelClockForgeExpander->createModule();
    APP->engine->addModule(em);
    ModuleWidget *ew = modelClockForgeExpander->createModuleWidget(em);

    APP->scene->rack->updateModuleOldPositions();
    APP->scene->rack->addModule(ew);
    APP->scene->rack->setModulePosForce(
        ew, mw->box.pos.plus(math::Vec(mw->box.size.x, 0)));

    history::ComplexAction *h = new history::ComplexAction;
    h->name = "add ClockForge expander";
    history::ModuleAdd *ha = new history::ModuleAdd;
    ha->name = h->name;
    ha->setModule(ew);
    h->push(ha);
    h->push(APP->scene->rack->getModuleDragAction());
    APP->history->push(h);

    cfengine::setExpanderType(m->cf->raw(), 1);
}

struct ClockForgeWidget : ModuleWidget {
    forgevcv::EncoderKnob *encoder = nullptr; // for the keyboard shortcuts (see onHoverKey)

    ClockForgeWidget(ClockForge *module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/ClockForge.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.060, 66.795)), module, ClockForge::CLKIN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.547, 80.797)), module, ClockForge::CV1IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.787, 80.797)), module, ClockForge::CV2IN_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.595f, 95.199f)), module, ClockForge::OUT1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.866f, 95.199f)), module, ClockForge::OUT2_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.595f, 109.399f)), module, ClockForge::OUT3_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.866f, 109.399f)), module, ClockForge::OUT4_OUTPUT));

        addChild(createLightCentered<SmallSimpleLight<RedLight>>(mm2px(Vec(7.595f, 89.383f)), module, ClockForge::LED1_LIGHT));
        addChild(createLightCentered<SmallSimpleLight<RedLight>>(mm2px(Vec(22.866f, 89.383f)), module, ClockForge::LED2_LIGHT));
        addChild(createLightCentered<SmallSimpleLight<RedLight>>(mm2px(Vec(7.595f, 103.604f)), module, ClockForge::LED3_LIGHT));
        addChild(createLightCentered<SmallSimpleLight<RedLight>>(mm2px(Vec(22.866f, 103.604f)), module, ClockForge::LED4_LIGHT));

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
        enc->box.pos = mm2px(Vec(15.060, 50.918)).minus(enc->box.size.div(2));
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

    void appendContextMenu(Menu *menu) override {
        ClockForge *m = dynamic_cast<ClockForge *>(module);
        if (!m)
            return;
        cfengine::Engine *e = m->cf->raw(); // raw handle for the curated param bridge

        // ── Hardware: the two host-side settings, grouped under a submenu ──────
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("ClockForge Settings"));
        menu->addChild(createSubmenuItem("Hardware", "", [=](Menu *menu) {
            menu->addChild(createIndexPtrSubmenuItem(
                "Input CV Range", {"0V – 5V", "-5V – +5V", "0V – 10V"}, &m->cvRange));
            menu->addChild(createIndexPtrSubmenuItem(
                "Encoder Sensitivity", {"Low", "Medium", "High"}, &m->encoderSensitivity));

            // Same setting as the module's EXPANDER menu row, and the same
            // authority: with it on NONE the expander's jacks sit at 0 V even
            // if the widget is right there, which is what pulling the ribbon
            // out of the real thing does.
            menu->addChild(createIndexSubmenuItem(
                "Expander", {"None", "Expander 1 (4 out / 1 in)"},
                [=]() { return (size_t)cfengine::expanderType(e); },
                [=](size_t v) { cfengine::setExpanderType(e, (int)v); }));
        }));

        // Offered only when there is not already one alongside. Adds the module
        // and switches the firmware setting on in one action, so the two can
        // never disagree by accident.
        if (!m->expanderAdjacent) {
            menu->addChild(createMenuItem("Add Expander 1", "", [=]() {
                addExpanderBeside(m);
            }));
        }

        // ── Module parameters, mirrored from the firmware menu ────────────────
        // These drive the engine's live state directly (same values the on-panel
        // encoder menu edits), so changes are reflected on the emulated OLED and
        // persisted with the patch.
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Module Parameters"));

        // Transport: master play/stop.
        menu->addChild(createBoolMenuItem(
            "Running", "",
            [=]() { return cfengine::isRunning(e); },
            [=](bool v) { cfengine::setRunning(e, v); }));

        // BPM: horizontal slider in a submenu (current value shown at right).
        menu->addChild(createSubmenuItem("BPM", string::f("%d", cfengine::bpm(e)), [=](Menu *menu) {
            forgevcv::IntSlider *slider = new forgevcv::IntSlider;
            slider->quantity = bpmQuantity(m); // Slider takes ownership (deletes it on destruction)
            menu->addChild(slider);
        }));

        // Per-output: enable, waveform, and clock divider. Follows the same
        // count the on-screen menu does, so an expander's outputs 5-8 appear
        // here too rather than only on the module's own display.
        const int nOut = cfengine::outputCount(e);
        for (int i = 0; i < nOut; i++) {
            menu->addChild(createSubmenuItem(string::f("Output %d", i + 1), "", [=](Menu *menu) {
                menu->addChild(createBoolMenuItem(
                    "Enabled", "",
                    [=]() { return cfengine::outputEnabled(e, i); },
                    [=](bool v) { cfengine::setOutputEnabled(e, i, v); }));

                std::vector<std::string> waves;
                for (int w = 0; w < cfengine::waveformCount(); w++)
                    waves.push_back(cfengine::waveformName(w));
                menu->addChild(createIndexSubmenuItem(
                    "Waveform", waves,
                    [=]() { return (size_t)cfengine::outputWaveform(e, i); },
                    [=](size_t w) { cfengine::setOutputWaveform(e, i, (int)w); }));

                // Divider is locked to "Env" while the output is an envelope type.
                if (cfengine::outputIsEnvelope(e, i)) {
                    menu->addChild(createMenuLabel("Divider: Env (locked)"));
                } else {
                    std::vector<std::string> divs;
                    for (int d = 0; d < cfengine::dividerCount(e); d++)
                        divs.push_back(cfengine::dividerName(e, d));
                    menu->addChild(createIndexSubmenuItem(
                        "Divider", divs,
                        [=]() { return (size_t)cfengine::outputDivider(e, i); },
                        [=](size_t d) { cfengine::setOutputDivider(e, i, (int)d); }));
                }
            }));
        }
    }
};

Model *modelClockForge = createModel<ClockForge, ClockForgeWidget>("ClockForge");
