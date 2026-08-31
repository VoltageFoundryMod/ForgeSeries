// ClockForgeExpander.cpp — Expander 1 for ClockForge: four more outputs, one
// more CV input.
//
// This module has NO engine. On the hardware the expander is a second MCP4728
// and an input stage on the far end of a ribbon; all the sequencing happens in
// ClockForge, which drives the expander's DAC over the same I2C bus it drives
// its own. The Rack port keeps that shape: ClockForge runs the firmware and
// hands four voltages across, and this widget puts them on jacks.
//
// Which means: with nothing adjacent, or with the parent's EXPANDER setting on
// NONE, these jacks read 0 V. That is not a failure mode to guard against — it
// is what an unplugged ribbon does.

#include "expander_message.hpp"
#include "plugin.hpp"

#include "forgevcv/ForgeModule.hpp"

struct ClockForgeExpander : Module {
    enum ParamId { PARAMS_LEN };
    enum InputId {
        IN4_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        OUT5_OUTPUT,
        OUT6_OUTPUT,
        OUT7_OUTPUT,
        OUT8_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        LED5_LIGHT,
        LED6_LIGHT,
        LED7_LIGHT,
        LED8_LIGHT,
        LIGHTS_LEN
    };

    // One buffer pair per side: the parent may be on either.
    ForgeExpanderMessage leftMessages[2];
    ForgeExpanderMessage rightMessages[2];

    // Mirrored for the panel widget, which draws on the UI thread.
    bool linked = false;

    ClockForgeExpander() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configInput(IN4_INPUT, "CV 3 (IN 4)");
        configOutput(OUT5_OUTPUT, "Out 5");
        configOutput(OUT6_OUTPUT, "Out 6");
        configOutput(OUT7_OUTPUT, "Out 7");
        configOutput(OUT8_OUTPUT, "Out 8");
        configLight(LED5_LIGHT, "Out 5 level");
        configLight(LED6_LIGHT, "Out 6 level");
        configLight(LED7_LIGHT, "Out 7 level");
        configLight(LED8_LIGHT, "Out 8 level");

        leftExpander.producerMessage = &leftMessages[0];
        leftExpander.consumerMessage = &leftMessages[1];
        rightExpander.producerMessage = &rightMessages[0];
        rightExpander.consumerMessage = &rightMessages[1];
    }

    // Is the neighbour on `side` a ClockForge? Compares the Model rather than
    // dynamic_cast so this file needs nothing from ClockForge.cpp.
    static bool isParent(const Expander &side) {
        return side.module && side.module->model == modelClockForge;
    }

    void process(const ProcessArgs &args) override {
        const bool onLeft = isParent(leftExpander);
        const bool onRight = !onLeft && isParent(rightExpander);
        linked = onLeft || onRight;

        // What the parent sent us last block, from the side it is on.
        const ForgeExpanderMessage *in = nullptr;
        if (onLeft)
            in = static_cast<const ForgeExpanderMessage *>(leftExpander.consumerMessage);
        else if (onRight)
            in = static_cast<const ForgeExpanderMessage *>(rightExpander.consumerMessage);

        const bool driven = in && in->parentActive;
        for (int i = 0; i < 4; i++) {
            const float v = driven ? in->out[i] : 0.f;
            outputs[OUT5_OUTPUT + i].setVoltage(v);
            // Same LED transfer curve as the base board — the driver circuit on
            // the expander is the same one, down to the resistor values.
            lights[LED5_LIGHT + i].setBrightnessSmooth(
                forgevcv::ForgeModule::outputLedBrightness(v), args.sampleTime);
        }

        // Send IN 4 back the other way. Writing into the PARENT's near-side
        // producer buffer and flipping it there is how Rack expects a module to
        // talk to its neighbour.
        Expander *parentSide = nullptr;
        if (onLeft)
            parentSide = &leftExpander.module->rightExpander; // its right faces us
        else if (onRight)
            parentSide = &rightExpander.module->leftExpander; // its left faces us

        if (parentSide && parentSide->producerMessage) {
            ForgeExpanderMessage *out =
                static_cast<ForgeExpanderMessage *>(parentSide->producerMessage);
            out->expanderPresent = true;
            out->in = inputs[IN4_INPUT].getVoltage();
            parentSide->requestMessageFlip();
        }
    }
};

struct ClockForgeExpanderWidget : ModuleWidget {
    ClockForgeExpanderWidget(ClockForgeExpander *module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/ClockForge_Exp1.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Positions read off panel-src/ClockForge_Exp1.svg's components
        // layer — `make panel-coords-clk`. Not generated; keep them in step by
        // re-running that after moving anything on the drawing.
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.719f, 22.681f)), module, ClockForgeExpander::IN4_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(9.757f, 41.356f)), module, ClockForgeExpander::OUT5_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(9.757f, 55.956f)), module, ClockForgeExpander::OUT6_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(9.757f, 70.556f)), module, ClockForgeExpander::OUT7_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(9.757f, 85.156f)), module, ClockForgeExpander::OUT8_OUTPUT));

        addChild(createLightCentered<SmallSimpleLight<RedLight>>(mm2px(Vec(9.757f, 35.368f)), module, ClockForgeExpander::LED5_LIGHT));
        addChild(createLightCentered<SmallSimpleLight<RedLight>>(mm2px(Vec(9.757f, 49.997f)), module, ClockForgeExpander::LED6_LIGHT));
        addChild(createLightCentered<SmallSimpleLight<RedLight>>(mm2px(Vec(9.757f, 64.676f)), module, ClockForgeExpander::LED7_LIGHT));
        addChild(createLightCentered<SmallSimpleLight<RedLight>>(mm2px(Vec(9.757f, 79.417f)), module, ClockForgeExpander::LED8_LIGHT));
    }
};

Model *modelClockForgeExpander =
    createModel<ClockForgeExpander, ClockForgeExpanderWidget>("ClockForgeExpander");
