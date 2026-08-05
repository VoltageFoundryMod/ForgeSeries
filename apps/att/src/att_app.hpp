#pragma once

// Factory for the ChaosForge (chaotic attractor) app.
// Only this declaration crosses the namespace boundary — see scp_app.hpp.

#include "IApp.hpp"

namespace forge {
IApp *AttApp();
}
