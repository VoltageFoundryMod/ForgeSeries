#include "engine/fw_engine.hpp"
#include "plugin.hpp"

#include "forgevcv/ForgeModule.hpp"
#include "forgevcv/widgets.hpp"

// Feed the scope engine every N audio samples. N=2 keeps ~22 kHz acquisition
// bandwidth while halving per-sample lock/swap traffic. The engine's own
// timebase knob decimates further.
static const int ENGINE_DECIM = 2;

// Subclasses forgevcv::ForgeModule for the shared host-side pieces (framebuffer,
// CV-range + encoder-sensitivity settings, and the UI->audio encoder queue). The
// scope firmware itself has its own feed/render cadence and buffered pass-through
// outputs, so it keeps a module-specific fvengine handle instead of the base's
// IEngine/stepEngine path (base `engine` stays null).
struct ForgeView : forgevcv::ForgeModule {
    enum ParamId { PARAMS_LEN };
    enum InputId {
        CLKIN_INPUT,
        CV1IN_INPUT,
        CV2IN_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        OUT1_OUTPUT, // buffered pass-through of CV1
        OUT2_OUTPUT, // buffered pass-through of CV2
        OUT3_OUTPUT, // additional CV 1 through
        OUT4_OUTPUT, // additional CV 2 through
        OUTPUTS_LEN
    };
    enum LightId { LIGHTS_LEN };

    fvengine::Engine *fv = nullptr; // scope firmware engine (module-specific API)
    int feedDecim = 0;
    int renderDecim = 0;

    ForgeView() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configInput(CLKIN_INPUT, "Clock / Trigger (Shot mode)");
        configInput(CV1IN_INPUT, "CV 1 (main trace)");
        configInput(CV2IN_INPUT, "CV 2 (second trace / X-Y Y-axis)");
        configOutput(OUT1_OUTPUT, "CV 1 through");
        configOutput(OUT2_OUTPUT, "CV 2 through");
        configOutput(OUT3_OUTPUT, "CV 1 through");
        configOutput(OUT4_OUTPUT, "CV 2 through");
        cvRange = CV_UNIPOLAR; // Use the 0–5V range by default (matches the hardware's input scaling). User can change it in the context menu.
        fv = fvengine::createEngine();
    }

    ~ForgeView() override {
        if (fv)
            fvengine::destroyEngine(fv);
    }

    void process(const ProcessArgs &args) override {
        if (!fv)
            return;

        // Drain encoder UI events (queued by the shared forgevcv widgets).
        int d = encDelta.exchange(0);
        if (d)
            fvengine::encoderTurn(fv, d);
        int c = encClick.exchange(0);
        for (int i = 0; i < c; i++) {
            fvengine::encoderButton(fv, true);
            fvengine::encoderButton(fv, false);
        }

        // Buffered oscilloscope through: raw inputs straight to the outputs
        // (independent of the display CV-range mapping), every sample.
        outputs[OUT1_OUTPUT].setVoltage(inputs[CV1IN_INPUT].getVoltage());
        outputs[OUT2_OUTPUT].setVoltage(inputs[CV2IN_INPUT].getVoltage());
        outputs[OUT3_OUTPUT].setVoltage(inputs[CV1IN_INPUT].getVoltage());
        outputs[OUT4_OUTPUT].setVoltage(inputs[CV2IN_INPUT].getVoltage());

        // Feed the acquisition engine at control rate.
        if (++feedDecim >= ENGINE_DECIM) {
            feedDecim = 0;
            float dt = ENGINE_DECIM * args.sampleTime;
            float cv1 = mapCvInput(inputs[CV1IN_INPUT].getVoltage());
            float cv2 = mapCvInput(inputs[CV2IN_INPUT].getVoltage());
            bool clk = inputs[CLKIN_INPUT].getVoltage() > 1.f;
            fvengine::feedSample(fv, dt, cv1, cv2, clk);
        }

        // Re-render the emulated OLED at ~display rate (not every sample).
        int renderPeriod = (int)(args.sampleRate / 60.f);
        if (renderPeriod < 1)
            renderPeriod = 1;
        if (++renderDecim >= renderPeriod) {
            renderDecim = 0;
            // Tell the engine how many volts full-scale spans, so its V/div label
            // matches the selected input range (0–5V => 5V span, else 10V).
            fvengine::setInputSpanVolts(fv, cvRange == CV_UNIPOLAR ? 5.f : 10.f);
            fvengine::getFramebuffer(fv, fb);
        }
    }

    json_t *dataToJson() override {
        json_t *root = json_object();
        baseToJson(root); // cvRange + encoderSensitivity (base engine unused: no eeprom)
        json_object_set_new(root, "mode", json_integer(fvengine::mode(fv)));
        json_object_set_new(root, "param1", json_integer(fvengine::param1(fv)));
        json_object_set_new(root, "param2", json_integer(fvengine::param2(fv)));
        json_object_set_new(root, "vscale", json_real(fvengine::verticalScale(fv)));
        json_object_set_new(root, "trigMode", json_integer(fvengine::trigMode(fv)));
        json_object_set_new(root, "offset1", json_integer(fvengine::offsetCh1(fv)));
        json_object_set_new(root, "offset2", json_integer(fvengine::offsetCh2(fv)));
        json_object_set_new(root, "showLabels", json_boolean(fvengine::showLabels(fv)));
        json_object_set_new(root, "tunerChan", json_integer(fvengine::tunerChannel(fv)));
        json_object_set_new(root, "xyPersist", json_integer(fvengine::xyPersist(fv)));
        return root;
    }

    void dataFromJson(json_t *root) override {
        baseFromJson(root); // cvRange + encoderSensitivity
        if (json_t *j = json_object_get(root, "mode"))
            fvengine::setMode(fv, (int)json_integer_value(j));
        if (json_t *j = json_object_get(root, "param1"))
            fvengine::setParam1(fv, (int)json_integer_value(j));
        if (json_t *j = json_object_get(root, "param2"))
            fvengine::setParam2(fv, (int)json_integer_value(j));
        if (json_t *j = json_object_get(root, "vscale"))
            fvengine::setVerticalScale(fv, (float)json_number_value(j));
        if (json_t *j = json_object_get(root, "trigMode"))
            fvengine::setTrigMode(fv, (int)json_integer_value(j));
        if (json_t *j = json_object_get(root, "offset1"))
            fvengine::setOffsetCh1(fv, (int)json_integer_value(j));
        if (json_t *j = json_object_get(root, "offset2"))
            fvengine::setOffsetCh2(fv, (int)json_integer_value(j));
        if (json_t *j = json_object_get(root, "showLabels"))
            fvengine::setShowLabels(fv, json_boolean_value(j));
        if (json_t *j = json_object_get(root, "tunerChan"))
            fvengine::setTunerChannel(fv, (int)json_integer_value(j));
        if (json_t *j = json_object_get(root, "xyPersist"))
            fvengine::setXyPersist(fv, (int)json_integer_value(j));
    }

    // The base's onReset drives `engine`, which this module leaves null (it holds
    // its own fvengine handle instead), so the scope reset is dispatched here.
    // There is deliberately no onRandomize: randomizing a measurement instrument
    // would just hide the signal you patched it in to look at.
    void onReset(const ResetEvent &e) override {
        forgevcv::ForgeModule::onReset(e); // params + cvRange/encoderSensitivity
        if (fv)
            fvengine::reset(fv);
    }
};

struct ForgeViewWidget : ModuleWidget {
    forgevcv::EncoderKnob *encoder = nullptr; // for the keyboard shortcuts (see onHoverKey)

    ForgeViewWidget(ForgeView *module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/ForgeView.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.2405, 66.795)), module, ForgeView::CLKIN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.153, 80.797)), module, ForgeView::CV1IN_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.647, 80.797)), module, ForgeView::CV2IN_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.412, 95.068)), module, ForgeView::OUT1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.652, 95.068)), module, ForgeView::OUT2_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.412, 109.34)), module, ForgeView::OUT3_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.652, 109.34)), module, ForgeView::OUT4_OUTPUT));

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

    // '[' / ']' turn the encoder, space pushes it (while hovering the module).
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
            if (e.action == GLFW_PRESS && e.key == GLFW_KEY_SPACE) {
                encoder->push();
                e.consume(this);
                return;
            }
        }
        ModuleWidget::onHoverKey(e);
    }

    void appendContextMenu(Menu *menu) override {
        ForgeView *m = dynamic_cast<ForgeView *>(module);
        if (!m)
            return;

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("ForgeView Settings"));
        menu->addChild(createSubmenuItem("Hardware", "", [=](Menu *menu) {
            menu->addChild(createIndexPtrSubmenuItem(
                "Input CV Range", {"0V – 5V", "-5V – +5V", "0V – 10V"}, &m->cvRange));
            menu->addChild(createIndexPtrSubmenuItem(
                "Encoder Sensitivity", {"Low", "Medium", "High"}, &m->encoderSensitivity));
        }));

        // Scope parameters, mirrored from the firmware menu (drive the engine
        // directly, so changes show on the emulated OLED and persist).
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Scope"));

        std::vector<std::string> modes;
        for (int i = 0; i < fvengine::modeCount(); i++)
            modes.push_back(fvengine::modeName(i));
        menu->addChild(createIndexSubmenuItem(
            "Mode", modes,
            [=]() { return (size_t)(fvengine::mode(m->fv) - 1); },
            [=](size_t v) { fvengine::setMode(m->fv, (int)v + 1); }));

        menu->addChild(createSubmenuItem("Timebase", string::f("%d", fvengine::param1(m->fv)), [=](Menu *menu) {
            for (int v = 1, vmax = fvengine::param1Max(m->fv); v <= vmax; v++)
                menu->addChild(createCheckMenuItem(
                    string::f("%d", v), "",
                    [=]() { return fvengine::param1(m->fv) == v; },
                    [=]() { fvengine::setParam1(m->fv, v); }));
        }));
        menu->addChild(createSubmenuItem("Param 2", string::f("%d", fvengine::param2(m->fv)), [=](Menu *menu) {
            for (int v = 1; v <= 8; v++)
                menu->addChild(createCheckMenuItem(
                    string::f("%d", v), "",
                    [=]() { return fvengine::param2(m->fv) == v; },
                    [=]() { fvengine::setParam2(m->fv, v); }));
        }));

        // Trigger mode (Shot). Shown always; only affects Shot mode.
        static const std::vector<std::string> trigNames = {"Off", "Rising", "Falling", "Auto"};
        menu->addChild(createIndexSubmenuItem(
            "Trigger", trigNames,
            [=]() { return (size_t)fvengine::trigMode(m->fv); },
            [=](size_t v) { fvengine::setTrigMode(m->fv, (int)v); }));

        // Tuner input channel (only affects Tuner mode).
        static const std::vector<std::string> tunerChans = {"CV 1", "CV 2"};
        menu->addChild(createIndexSubmenuItem(
            "Tuner input", tunerChans,
            [=]() { return (size_t)(fvengine::tunerChannel(m->fv) - 1); },
            [=](size_t v) { fvengine::setTunerChannel(m->fv, (int)v + 1); }));

        // X-Y phosphor persistence (only affects X-Y mode).
        std::vector<std::string> persist;
        for (int i = 0; i < fvengine::xyPersistCount(); i++)
            persist.push_back(fvengine::xyPersistName(i));
        menu->addChild(createIndexSubmenuItem(
            "X-Y persistence", persist,
            [=]() { return (size_t)fvengine::xyPersist(m->fv); },
            [=](size_t v) { fvengine::setXyPersist(m->fv, (int)v); }));

        // Per-channel vertical offsets.
        menu->addChild(createSubmenuItem("Offset CH1", string::f("%+d", fvengine::offsetCh1(m->fv)), [=](Menu *menu) {
            for (int v = 24; v >= -24; v -= 4)
                menu->addChild(createCheckMenuItem(
                    string::f("%+d", v), "",
                    [=]() { return fvengine::offsetCh1(m->fv) == v; },
                    [=]() { fvengine::setOffsetCh1(m->fv, v); }));
        }));
        menu->addChild(createSubmenuItem("Offset CH2", string::f("%+d", fvengine::offsetCh2(m->fv)), [=](Menu *menu) {
            for (int v = 24; v >= -24; v -= 4)
                menu->addChild(createCheckMenuItem(
                    string::f("%+d", v), "",
                    [=]() { return fvengine::offsetCh2(m->fv) == v; },
                    [=]() { fvengine::setOffsetCh2(m->fv, v); }));
        }));

        menu->addChild(createBoolMenuItem(
            "Axis labels (V/div, time/div)", "",
            [=]() { return fvengine::showLabels(m->fv); },
            [=](bool v) { fvengine::setShowLabels(m->fv, v); }));
        menu->addChild(createBoolMenuItem(
            "Freeze frame", "",
            [=]() { return fvengine::frozen(m->fv); },
            [=](bool v) { fvengine::setFrozen(m->fv, v); }));
    }
};

Model *modelForgeView = createModel<ForgeView, ForgeViewWidget>("ForgeView");
