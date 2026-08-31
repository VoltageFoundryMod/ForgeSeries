#include "plugin.hpp"


Plugin* pluginInstance;


void init(Plugin* p) {
	pluginInstance = p;

	// Add modules here. This replaces the per-module plugin.cpp, which
	// registers only its own Model; the Makefile filters those out of the
	// aggregate build.
	p->addModel(modelClockForge);
	p->addModel(modelClockForgeExpander);
	p->addModel(modelNoteForge);
	p->addModel(modelForgeView);
	p->addModel(modelGravityForge);
	p->addModel(modelChaosForge);
	p->addModel(modelWeaveForge);

	// Any other plugin initialization may go here.
	// As an alternative, consider lazy-loading assets and lookup tables when your module is created to reduce startup times of Rack.
}
