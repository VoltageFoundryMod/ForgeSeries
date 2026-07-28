#pragma once

// storage.hpp — this module's binding to the shared persistence layer.
//
// Everything that used to be here lives in core/appStorage.hpp; all that ever
// differed between the three copies was the slug and the preset schema.

#define FORGE_APP_SLUG "dq"

#include "presetManager.hpp" // LoadSaveParams, NUM_SLOTS, VALID_MAGIC, defaults

#include "appStorage.hpp" // core
