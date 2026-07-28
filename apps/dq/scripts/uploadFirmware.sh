#!/usr/bin/env bash

# Copy a UF2 firmware image onto the XIAO RP2040's bootloader mass-storage volume.
#
# On the RP2040 there is nothing to convert: PlatformIO's build already emits
# .pio/build/xiao_rp2040/firmware.uf2 (the SAMD21 bin->uf2 conversion step this
# repo used to need is gone).
#
# Put the board into bootloader mode first: hold BOOT and tap RESET, or
# double-tap RESET. A drive named RPI-RP2 appears.
#
# Usage: scripts/uploadFirmware.sh .pio/build/xiao_rp2040/firmware.uf2

set -uo pipefail

FIRMWARE="${1:-.pio/build/xiao_rp2040/firmware.uf2}"

if [ ! -f "$FIRMWARE" ]; then
  echo "Error: firmware file not found: $FIRMWARE"
  exit 1
fi

# Candidate mount points for the RP2040 bootloader volume across platforms.
CANDIDATES=(
  /Volumes/RPI-RP2            # macOS
  "/media/$USER/RPI-RP2"      # Linux
  "/run/media/$USER/RPI-RP2"
)
# Windows (Git Bash / MSYS2): drives are mounted as /c, /d, ... so scan them and
# identify the board by the bootloader's INFO_UF2.TXT marker file.
for drive in /?; do
  CANDIDATES+=("$drive")
done

for target in "${CANDIDATES[@]}"; do
  if [ -d "$target" ] && [ -f "$target/INFO_UF2.TXT" ]; then
    echo "Uploading $FIRMWARE to $target ..."
    cp -f "$FIRMWARE" "$target/CURRENT.UF2"
    echo "Firmware uploaded successfully. The module will reboot."
    exit 0
  fi
done

echo "Error: RP2040 bootloader drive not found."
echo "Double-tap RESET on the XIAO (or hold BOOT and tap RESET) and try again."
exit 1
