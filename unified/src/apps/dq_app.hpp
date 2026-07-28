#pragma once

// Factory for the NoteForge (dual quantizer) app.
// Only this declaration crosses the namespace boundary — see scp_app.hpp.

#include "IApp.hpp"

namespace forge {
IApp *DqApp();
}
