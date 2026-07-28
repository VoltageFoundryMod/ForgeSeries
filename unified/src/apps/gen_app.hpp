#pragma once

// Factory for the GravityForge (physics sequencer) app.
// Only this declaration crosses the namespace boundary — see scp_app.hpp.

#include "IApp.hpp"

namespace forge {
IApp *GenApp();
}
