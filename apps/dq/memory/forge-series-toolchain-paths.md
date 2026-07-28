---
name: forge-series-toolchain-paths
description: Neither PlatformIO nor MSYS2 g++/make are on PATH — export them before building
metadata:
  type: project
---

On this machine the build tools are installed but not on the default PATH:

- PlatformIO CLI: `~/.platformio/penv/Scripts/pio.exe`
- MSYS2 MinGW64 toolchain: `/c/msys64/mingw64/bin` (g++), `/c/msys64/usr/bin` (make)
- VCV Rack SDK: `C:/Users/carlosedp/projects/Rack-SDK`
- Inkscape (for panel text→path conversion): `C:/Program Files/Inkscape/bin/inkscape.exe`

**Why:** `pio test -e native` fails with "'g++' is not recognized" without the
MSYS2 path, which looks like a code error but is not.

**How to apply:** Prefix build commands with
`export PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$HOME/.platformio/penv/Scripts:$PATH"`.
See also [[forge-series-shared-vcv-layer]].
