#pragma once

// The version of the image as a whole.
//
// This is a different thing from each app's lib/version.hpp: those are
// per-module numbers, bumped when that module's behaviour changes, and the boot
// menu shows them beside each module name. The unified image is one indivisible
// artifact — you cannot flash just one module — so it carries one version of its
// own, and that is what the release tag names.
//
// CI passes the tag through as -DFORGE_FW_VERSION=v4.0.0, deliberately
// UNQUOTED: the flag travels the environment -> PlatformIO -> scons -> gcc, and
// an embedded \" does not survive that reliably. The token is stringified here
// instead, which needs no escaping at any step and handles a prerelease tag
// (v4.0.0-rc1) as well.
#define FORGE_STRINGIFY_(x) #x
#define FORGE_STRINGIFY(x) FORGE_STRINGIFY_(x)

// A local build has no tag to stamp.
#ifndef FORGE_FW_VERSION
#define FORGE_FW_VERSION dev
#endif

namespace forge {
inline constexpr const char *kFirmwareVersion = FORGE_STRINGIFY(FORGE_FW_VERSION);
}
