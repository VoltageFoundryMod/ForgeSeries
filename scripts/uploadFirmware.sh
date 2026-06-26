#!/usr/bin/env bash

# This script is used to upload the firmware to the device

FIRMWARE=$1

if [ -d /Volumes/Arduino ]; then
  echo "Uploading firmware to device..."
  cp -rf "$FIRMWARE" /Volumes/Arduino/CURRENT.UF2
  echo "Firmware uploaded successfully!"
  exit 0
else
  echo "Error: Arduino device not found!"
  exit 1
fi
