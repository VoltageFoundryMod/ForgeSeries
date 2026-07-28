#pragma once

// Factory for the ClockForge (clock generator) app.
// Only this declaration crosses the namespace boundary — see scp_app.hpp.

#include "IApp.hpp"

namespace forge {
IApp *ClkApp();
}
