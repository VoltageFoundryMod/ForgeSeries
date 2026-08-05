#pragma once
#include <rack.hpp>


using namespace rack;

// Declare the Plugin, defined in plugin.cpp
extern Plugin* pluginInstance;

// Declare each Model, defined in each module's source under
// ../apps/<module>/vcv-plugin/src/. Every module also ships its own plugin.hpp
// declaring only its own Model; those keep the standalone single-module builds
// working and are what the module sources include.
extern Model* modelClockForge;   // ../apps/clk
extern Model* modelNoteForge;    // ../apps/dq
extern Model* modelForgeView;    // ../apps/scp
extern Model* modelGravityForge; // ../apps/gen
extern Model* modelChaosForge;   // ../apps/att
