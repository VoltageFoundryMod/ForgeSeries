# Put the toolchain on PATH for this PowerShell session, then build normally:
#
#     . .\tools\env.ps1
#     make            # unified firmware
#     make plugins    # every VCV Rack plugin
#     make everything # both
#
# No msys2 shell required. Rack's plugin.mk shells out to POSIX tools, but GNU
# make picks up msys2's sh.exe as SHELL automatically once it is on PATH, so the
# recipes run fine with PowerShell as the *parent* shell.
#
# What each entry is for:
#   msys64\usr\bin      make, sh, and the coreutils plugin.mk expects
#   msys64\mingw64\bin  g++ (the Rack SDK for Windows is mingw-w64 built —
#                       MSVC will not link against it) and jq, which
#                       plugin.mk uses to read SLUG out of plugin.json
#   .platformio\penv\Scripts  pio, for the firmware

$msys = "C:\msys64"
if (-not (Test-Path $msys)) { Write-Warning "msys2 not found at $msys - VCV plugin builds will fail" }

$env:PATH = "$msys\usr\bin;$msys\mingw64\bin;$env:USERPROFILE\.platformio\penv\Scripts;$env:PATH"

foreach ($t in @("make", "sh", "g++", "jq", "pio")) {
    $p = (Get-Command $t -ErrorAction SilentlyContinue).Source
    if ($p) { Write-Host ("  {0,-5} {1}" -f $t, $p) }
    else    { Write-Warning "$t not found on PATH" }
}
