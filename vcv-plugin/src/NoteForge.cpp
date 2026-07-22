#include "engine/fw_engine.hpp"
#include "plugin.hpp"

#include "forgevcv/ForgeModule.hpp"
#include "forgevcv/widgets.hpp"

#include <functional>

struct NoteForge : forgevcv::ForgeModule {
    enum ParamId {
        PARAMS_LEN
    };
    enum InputId {
        TRIG_INPUT,
        CV1_INPUT,
        CV2_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        CV1_OUTPUT,
        CV2_OUTPUT,
        GATE1_OUTPUT,
        GATE2_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        LIGHTS_LEN
    };

    // The concrete firmware engine. Held as a typed pointer for the curated param
    // bridge (context menu); the base owns and deletes it via ForgeModule::engine.
    nfengine::VcvEngine *nf = nullptr;

    // Pitch CV is 1V/oct, so the host may only *shift* the incoming voltage, never
    // rescale it — the base class's CvRange remap would halve volts-per-octave and
    // an octave would stop being an octave. This offset slides a bipolar
    // sequencer's output up into the hardware's 0–5V window instead.
    int cvShiftVolts = 0; // 0..3

    NoteForge() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configInput(TRIG_INPUT, "Trigger");
        configInput(CV1_INPUT, "Pitch CV 1");
        configInput(CV2_INPUT, "Pitch CV 2");
        configOutput(CV1_OUTPUT, "Quantized CV 1");
        configOutput(CV2_OUTPUT, "Quantized CV 2");
        configOutput(GATE1_OUTPUT, "Gate / envelope 1");
        configOutput(GATE2_OUTPUT, "Gate / envelope 2");
        nf = new nfengine::VcvEngine();
        engine = nf; // base takes ownership
    }

    float mapPitchInput(float v) const {
        return clamp(v + (float)cvShiftVolts, 0.f, 5.f);
    }

    void process(const ProcessArgs &args) override {
        float cv[2] = {
            mapPitchInput(inputs[CV1_INPUT].getVoltage()),
            mapPitchInput(inputs[CV2_INPUT].getVoltage())};
        bool trig = inputs[TRIG_INPUT].getVoltage() > 1.f;
        stepEngine(args.sampleTime, cv, 2, trig, 4);

        for (int i = 0; i < 4; i++)
            outputs[CV1_OUTPUT + i].setVoltage(outHold[i]);
    }

    json_t *dataToJson() override {
        json_t *root = json_object();
        baseToJson(root); // eeprom blob + cvRange + encoderSensitivity
        json_object_set_new(root, "cvShiftVolts", json_integer(cvShiftVolts));
        return root;
    }

    void dataFromJson(json_t *root) override {
        baseFromJson(root);
        if (json_t *j = json_object_get(root, "cvShiftVolts"))
            cvShiftVolts = (int)json_integer_value(j);
    }
};

// ── Generic slider quantity for the context menu ─────────────────────────────
// Reads and writes an integer firmware parameter straight through the engine
// bridge. The slider widget itself is forgevcv's shared wide slider.
struct EngineIntQuantity : Quantity {
    std::function<int()> getFn;
    std::function<void(int)> setFn;
    float minV = 0.f, maxV = 100.f, defV = 0.f;
    std::string label, unit;

    void setValue(float v) override {
        if (setFn)
            setFn((int)std::round(clamp(v, minV, maxV)));
    }
    float getValue() override { return getFn ? (float)getFn() : defV; }
    float getMinValue() override { return minV; }
    float getMaxValue() override { return maxV; }
    float getDefaultValue() override { return defV; }
    float getDisplayValue() override { return getValue(); }
    void setDisplayValue(float v) override { setValue(v); }
    int getDisplayPrecision() override { return 4; }
    std::string getLabel() override { return label; }
    std::string getUnit() override { return unit; }
};

static void addSlider(Menu *menu, const std::string &label, const std::string &unit,
                      float minV, float maxV, float defV,
                      std::function<int()> get, std::function<void(int)> set) {
    EngineIntQuantity *q = new EngineIntQuantity;
    q->label = label;
    q->unit = unit;
    q->minV = minV;
    q->maxV = maxV;
    q->defV = defV;
    q->getFn = get;
    q->setFn = set;
    forgevcv::BpmSlider *slider = new forgevcv::BpmSlider; // shared wide slider
    slider->quantity = q;                                  // Slider takes ownership
    menu->addChild(slider);
}

struct NoteForgeWidget : ModuleWidget {
    forgevcv::EncoderKnob *encoder = nullptr; // for the keyboard shortcuts (see onHoverKey)

    NoteForgeWidget(NoteForge *module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/NoteForge.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.2405, 66.795)), module, NoteForge::TRIG_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.153, 80.797)), module, NoteForge::CV1_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.647, 80.797)), module, NoteForge::CV2_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.412, 95.068)), module, NoteForge::CV1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.652, 95.068)), module, NoteForge::CV2_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.407, 109.34)), module, NoteForge::GATE1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.647, 109.34)), module, NoteForge::GATE2_OUTPUT));

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

    // Per-channel submenu, mirroring the firmware's own menu pages.
    void appendChannelMenu(Menu *menu, NoteForge *m, int ch) {
        nfengine::Engine *e = m->nf->raw();

        menu->addChild(createSubmenuItem(string::f("Channel %d", ch + 1),
                                         nfengine::currentNote(e, ch), [=](Menu *menu) {
            // ── Notes: the live 12-note mask, the real source of truth ────────
            menu->addChild(createSubmenuItem("Notes", "", [=](Menu *menu) {
                for (int n = 0; n < 12; n++) {
                    menu->addChild(createBoolMenuItem(
                        nfengine::noteName(n), "",
                        [=]() { return nfengine::noteEnabled(e, ch, n); },
                        [=](bool v) { nfengine::setNoteEnabled(e, ch, n, v); }));
                }
            }));

            // ── Scale: applied to the note mask as soon as it is chosen ──────
            std::vector<std::string> scales;
            for (int s = 0; s < nfengine::scaleCount(); s++)
                scales.push_back(nfengine::scaleName(s));
            menu->addChild(createIndexSubmenuItem(
                "Scale", scales,
                [=]() { return (size_t)nfengine::channelScale(e, ch); },
                [=](size_t s) { nfengine::setChannelScale(e, ch, (int)s); }));

            std::vector<std::string> roots;
            for (int n = 0; n < 12; n++)
                roots.push_back(nfengine::noteName(n));
            menu->addChild(createIndexSubmenuItem(
                "Root", roots,
                [=]() { return (size_t)nfengine::channelRoot(e, ch); },
                [=](size_t r) { nfengine::setChannelRoot(e, ch, (int)r); }));

            menu->addChild(new MenuSeparator);

            // ── Pitch ─────────────────────────────────────────────────────────
            // TRACK follows the input; S&H latches the note on a TRIG edge and
            // holds it, so nothing the input does between triggers is heard.
            std::vector<std::string> pitchModes;
            for (int pm = 0; pm < nfengine::pitchModeCount(); pm++)
                pitchModes.push_back(nfengine::pitchModeName(pm));
            menu->addChild(createIndexSubmenuItem(
                "Pitch mode", pitchModes,
                [=]() { return (size_t)nfengine::channelPitchMode(e, ch); },
                [=](size_t pm) { nfengine::setChannelPitchMode(e, ch, (int)pm); }));

            std::vector<std::string> octaves;
            for (int o = nfengine::octaveMin(); o <= nfengine::octaveMax(); o++)
                octaves.push_back(o > 0 ? string::f("+%d", o) : string::f("%d", o));
            int octBase = nfengine::octaveMin();
            menu->addChild(createIndexSubmenuItem(
                "Octave", octaves,
                [=]() { return (size_t)(nfengine::channelOctave(e, ch) - octBase); },
                [=](size_t o) { nfengine::setChannelOctave(e, ch, (int)o + octBase); }));

            menu->addChild(createSubmenuItem("Glide", string::f("%d%%", nfengine::channelGlide(e, ch)),
                                             [=](Menu *menu) {
                addSlider(menu, "Glide", "%", 0.f, 100.f, 0.f,
                          [=]() { return nfengine::channelGlide(e, ch); },
                          [=](int v) { nfengine::setChannelGlide(e, ch, v); });
            }));

            // How long a new note must hold before the output follows. Suppresses
            // the notes an input sweeps through on its way between two pitches.
            menu->addChild(createSubmenuItem("Settle", string::f("%d ms", nfengine::channelSettle(e, ch)),
                                             [=](Menu *menu) {
                addSlider(menu, "Settle", " ms", 0.f, (float)nfengine::settleMax(), 5.f,
                          [=]() { return nfengine::channelSettle(e, ch); },
                          [=](int v) { nfengine::setChannelSettle(e, ch, v); });
            }));

            menu->addChild(createBoolMenuItem(
                "Follow transpose CV", "",
                [=]() { return nfengine::channelTranspose(e, ch); },
                [=](bool v) { nfengine::setChannelTranspose(e, ch, v); }));

            menu->addChild(new MenuSeparator);

            // ── Gate / envelope ───────────────────────────────────────────────
            std::vector<std::string> gateModes;
            for (int g = 0; g < nfengine::gateModeCount(); g++)
                gateModes.push_back(nfengine::gateModeName(g));
            menu->addChild(createIndexSubmenuItem(
                "Gate mode", gateModes,
                [=]() { return (size_t)nfengine::channelGateMode(e, ch); },
                [=](size_t g) { nfengine::setChannelGateMode(e, ch, (int)g); }));

            std::vector<std::string> syncModes;
            for (int s = 0; s < nfengine::syncModeCount(); s++)
                syncModes.push_back(nfengine::syncModeName(s));
            menu->addChild(createIndexSubmenuItem(
                "Sync", syncModes,
                [=]() { return (size_t)nfengine::channelSyncMode(e, ch); },
                [=](size_t s) { nfengine::setChannelSyncMode(e, ch, (int)s); }));

            menu->addChild(createSubmenuItem("Attack", string::f("%d ms", nfengine::channelAttack(e, ch)),
                                             [=](Menu *menu) {
                addSlider(menu, "Attack", " ms", 0.f, (float)nfengine::attackMax(), 0.f,
                          [=]() { return nfengine::channelAttack(e, ch); },
                          [=](int v) { nfengine::setChannelAttack(e, ch, v); });
            }));

            menu->addChild(createSubmenuItem("Decay", string::f("%d ms", nfengine::channelDecay(e, ch)),
                                             [=](Menu *menu) {
                addSlider(menu, "Decay", " ms", 0.f, (float)nfengine::decayMax(), 360.f,
                          [=]() { return nfengine::channelDecay(e, ch); },
                          [=](int v) { nfengine::setChannelDecay(e, ch, v); });
            }));
        }));
    }

    void appendContextMenu(Menu *menu) override {
        NoteForge *m = dynamic_cast<NoteForge *>(module);
        if (!m)
            return;

        // ── Hardware: the host-side settings ──────────────────────────────────
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("NoteForge Settings"));
        menu->addChild(createSubmenuItem("Hardware", "", [=](Menu *menu) {
            // Shift, not scale: pitch CV is 1V/oct and the hardware window is
            // 0–5V, so a bipolar source is slid up rather than compressed.
            menu->addChild(createIndexPtrSubmenuItem(
                "Input CV Shift", {"0V", "+1V", "+2V", "+3V"}, &m->cvShiftVolts));
            menu->addChild(createIndexPtrSubmenuItem(
                "Encoder Sensitivity", {"Low", "Medium", "High"}, &m->encoderSensitivity));
        }));

        // ── Module parameters, mirrored from the firmware menu ────────────────
        // These drive the engine's live state directly (the same values the
        // on-panel encoder menu edits), so changes show on the emulated OLED and
        // are persisted with the patch.
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Module Parameters"));

        // ── Input routing ─────────────────────────────────────────────────────
        // Handing IN 2 to a transposition CV costs channel 2 its pitch input, so
        // it is an explicit choice. In return channel 2 quantizes IN 1 alongside
        // channel 1: two voicings of the same melody, transposed together.
        nfengine::Engine *eng = m->nf->raw();
        menu->addChild(createSubmenuItem("Input routing",
                                         nfengine::in2RoleName(nfengine::in2RoleGet(eng)),
                                         [=](Menu *menu) {
            std::vector<std::string> roles;
            for (int r = 0; r < nfengine::in2RoleCount(); r++)
                roles.push_back(nfengine::in2RoleName(r));
            menu->addChild(createIndexSubmenuItem(
                "IN 2", roles,
                [=]() { return (size_t)nfengine::in2RoleGet(eng); },
                [=](size_t r) { nfengine::in2RoleSet(eng, (int)r); }));

            std::vector<std::string> ranges;
            for (int r = 0; r < nfengine::transposeRangeCount(); r++)
                ranges.push_back(nfengine::transposeRangeName(r));
            menu->addChild(createIndexSubmenuItem(
                "Transpose range", ranges,
                [=]() { return (size_t)nfengine::transposeRangeGet(eng); },
                [=](size_t r) { nfengine::transposeRangeSet(eng, (int)r); }));
        }));

        for (int ch = 0; ch < 2; ch++)
            appendChannelMenu(menu, m, ch);
    }
};

Model *modelNoteForge = createModel<NoteForge, NoteForgeWidget>("NoteForge");
