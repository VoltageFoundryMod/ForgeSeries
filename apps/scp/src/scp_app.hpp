#pragma once

// Factory for the ForgeView (scope) app.
//
// Only this declaration crosses the namespace boundary — the shell never sees
// forge::scp's internals, which is the point: every app has globals with the
// same names and they must stay invisible to each other.

#include "IApp.hpp"

namespace forge {
IApp *ScpApp();
}
