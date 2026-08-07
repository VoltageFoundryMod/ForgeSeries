#pragma once

// Factory for the WeaveForge (dual shift-register sequencer) app.
// Only this declaration crosses the namespace boundary — see scp_app.hpp.

#include "IApp.hpp"

namespace forge {
IApp *WeaApp();
}
