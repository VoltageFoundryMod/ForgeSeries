#pragma once

// storage.hpp — this module's binding to the shared persistence layer.
//
// Everything that used to be per-module lives in core/appStorage.hpp; all that
// ever differs between the copies is the slug and the preset schema.

#define FORGE_APP_SLUG "wea"

#include "presetManager.hpp" // LoadSaveParams, NUM_SLOTS, VALID_MAGIC, defaults

#include "appStorage.hpp" // core
