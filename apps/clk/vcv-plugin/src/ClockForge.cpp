#include "engine/fw_engine.hpp"
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
        LIGHTS_LEN
    };

    // The concrete firmware engine. Held as a typed pointer for the curated param
    // bridge (context menu); the base owns and deletes it via ForgeModule::engine.
    cfengine::VcvEngine *cf = nullptr;

    ClockForge() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configInput(CLKIN_INPUT, "Clock (TRIG)");
        configInput(CV1IN_INPUT, "CV 1");
        configInput(CV2IN_INPUT, "CV 2");
        configOutput(OUT1_OUTPUT, "Out 1");
        configOutput(OUT2_OUTPUT, "Out 2");
        configOutput(OUT3_OUTPUT, "Out 3");
        configOutput(OUT4_OUTPUT, "Out 4");
        cf = new cfengine::VcvEngine();
        engine = cf; // base takes ownership
    }

    void process(const ProcessArgs &args) override {
        float cv[2] = {
            mapCvInput(inputs[CV1IN_INPUT].getVoltage()),
            mapCvInput(inputs[CV2IN_INPUT].getVoltage())};
        bool clk = inputs[CLKIN_INPUT].getVoltage() > 1.f;
        stepEngine(args.sampleTime, cv, 2, clk, 4);

        for (int i = 0; i < 4; i++)
            outputs[OUT1_OUTPUT + i].setVoltage(outHold[i]);
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
// A Quantity that reads/writes BPM straight through the engine bridge, driven by
// the shared forgevcv::BpmSlider inside a submenu.
struct BpmQuantity : Quantity {
    ClockForge *module = nullptr;
    void setValue(float v) override {
        if (module)
            cfengine::setBpm(module->cf->raw(), (int)std::round(v));
    }
    float getValue() override { return module ? cfengine::bpm(module->cf->raw()) : 120.f; }
    float getMinValue() override { return cfengine::bpmMin(); }
    float getMaxValue() override { return cfengine::bpmMax(); }
    float getDefaultValue() override { return 120.f; }
    float getDisplayValue() override { return getValue(); }
    void setDisplayValue(float v) override { setValue(v); }
    int getDisplayPrecision() override { return 3; }
    std::string getLabel() override { return "Tempo"; }
    std::string getUnit() override { return " BPM"; }
};

struct ClockForgeWidget : ModuleWidget {
    forgevcv::EncoderKnob *encoder = nullptr; // for the keyboard shortcuts (see onHoverKey)

    ClockForgeWidget(ClockForge *module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/ClockForge.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.2405, 66.795)), module, ClockForge::CLKIN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.153, 80.797)), module, ClockForge::CV1IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.647, 80.797)), module, ClockForge::CV2IN_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.412, 95.068)), module, ClockForge::OUT1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.652, 95.068)), module, ClockForge::OUT2_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.407, 109.34)), module, ClockForge::OUT3_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.647, 109.34)), module, ClockForge::OUT4_OUTPUT));

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
        }));

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
            BpmQuantity *q = new BpmQuantity;
            q->module = m;
            forgevcv::BpmSlider *slider = new forgevcv::BpmSlider;
            slider->quantity = q; // Slider takes ownership (deletes it on destruction)
            menu->addChild(slider);
        }));

        // Per-output: enable, waveform, and clock divider.
        for (int i = 0; i < 4; i++) {
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
