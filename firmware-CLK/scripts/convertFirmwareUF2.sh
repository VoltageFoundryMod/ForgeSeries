#!/usr/bin/env bash

# This script is used to convert the firmware BIN file to UF2 format

# Reference:
# https://github.com/Seeed-Studio/Seeed_Arduino_USBDISP/issues/7
# Convert tool from Microsoft: https://github.com/microsoft/uf2

FIRMWARE=$1

if [ -f "$FIRMWARE" ]; then
  echo "Converting firmware to UF2 format..."
  ./scripts/uf2conv.py -f SAMD21 -b 0x2000 "$FIRMWARE" -o "${FIRMWARE%.*}.UF2"
  rm -rf CURRENT.UF2
  cp -f "${FIRMWARE%.*}.UF2" CURRENT.UF2
  echo "Firmware converted successfully!"
  exit 0
else
  echo "Error: Firmware file not found!"
  exit 1
fi
