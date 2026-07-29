#pragma once

// engine.hpp — NoteForge's per-iteration engine step.
//
// This lived in src/main.cpp, which meant it was copied into the VCV Rack
// engine and the unified firmware's app TU as well: three translation units
// compile this module, and only one of them is main.cpp. Three copies that each
// compiled independently, so a change to one silently left the others behind.
// GravityForge's Rack port lost LOOP>NAP muting exactly that way.
//
// Include AFTER the globals below are defined — same convention menuHandlers.hpp
// already uses.

#include "channel.hpp"
#include "cvInputs.hpp"
#include "jacks.hpp"

// Defined by whichever TU is hosting the module.
extern QuantizerChannel channels[NUM_CHANNELS];
extern bool displayRefresh;

// Advance both quantizer voices and push all four DAC outputs.
// Jack map: 1 = CV 1, 2 = CV 2, 3 = GATE 1, 4 = GATE 2.
inline void HandleOutputs() {
    const unsigned long now = micros();
    const bool trigEdge = ConsumeTrigger();
    HandleTriggerLevel();
    HandleTransposeInput();

    for (int i = 0; i < NUM_CHANNELS; i++) {
        // With IN 2 handed to the transpose CV, channel 2 has no pitch input of
        // its own, so it quantizes IN 1 alongside channel 1: two voicings of the
        // same melody rather than a dead channel.
        const float pitchCv = (in2Role == In2Transpose) ? channelCv[0] : channelCv[i];
        channels[i].SetTransposeDegrees(transposeDegrees);
        channels[i].Process(CvSemitones(pitchCv), now, trigEdge, trigLevel);
    }

    DACWriteAll(channels[0].GetCVOutput(), channels[1].GetCVOutput(),
                channels[0].GetGateOutput(), channels[1].GetGateOutput());

    // A note change is the one thing that makes the keyboard screen stale.
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (channels[i].ConsumeNoteChanged())
            displayRefresh = 1;
    }
}
