# NoteForge - A Dual Quantizer Eurorack Module

![Logo](./Logo-DQ.png)

## Introduction

NoteForge is a versatile tool designed to quantize analog control voltages (CV) into musical notes. It features two independent quantizers, each capable of processing input CV signals and outputting quantized CV signals corresponding to musical notes. The module supports various scales and allows for customization of attack and decay envelopes, sync modes, octave shifts, and sensitivity settings.

Part of the **Forge** series of modules which share a single hardware platform.

## Features

- Two independent quantizers
- Support for multiple scales
- Customizable attack and decay envelopes
- Sync modes on trigger and note change
- 3 Octave shift and sensitivity settings
- OLED display for easy configuration
- Rotary encoder for menu navigation and parameter adjustment

## Getting Started

### Powering the Module

1. Connect the module to a suitable power source.
2. Ensure that the power supply provides the correct voltage and current requirements.

### Initial Setup

1. Power on the module.
2. The OLED display will show a splash screen followed by the module name and version number.
3. Module will load default parameters for scale, root note and envelope/octave settings.

### Interface

- TRIG: Trigger input (0-5V)
- IN1, IN2: CV input to be quantized (0-5V)
- GATE 1 / 2: Gate with envelope curve output for each channel (0-5V)
- CV 1 / 2: CV output with quantized scale output for each channel (CH1: 10bit, CH2: 12bit, 0-5V)

## Menu Navigation

### Rotary Encoder

- **Rotate Clockwise**: Increase the selected parameter or navigate to the next menu item.
- **Rotate Counter-Clockwise**: Decrease the selected parameter or navigate to the previous menu item.
- **Press**: Enter item edit mode or confirm changes.

### Menu Structure

1. **Scale Selection**
   - Select the desired musical scale for each quantizer.
2. **Root Note Selection**
   - Select the root note for the chosen scale.
3. **Attack Envelope**
   - Adjust the attack time for each quantizer.
4. **Decay Envelope**
   - Adjust the decay time for each quantizer.
5. **Sync Mode**
   - Choose between trigger sync and note change sync for each quantizer.
6. **Octave Shift**
   - Adjust the octave shift for each quantizer.
7. **Sensitivity**
   - Adjust the sensitivity of the input CV for each quantizer.
8. **Save Settings**
   - Save the current settings to flash memory.

## Detailed Operation

Most parameters can be either clicked to toggle enable/disable or clicked to be selected and rotated to adjust the value. The OLED display will show the current value and the parameter name. An empty arrow will indicate that the parameter is selected and a filled arrow will indicate that the parameter is being edited.

### Scale and Root Note Selection

1. Navigate to the "Scale" menu item.
2. Rotate the encoder to select the desired scale.
3. Press the encoder to confirm the selection.
4. Navigate to the "Root Note" menu item.
5. Rotate the encoder to select the desired root note.
6. Press the encoder to confirm the selection.
7. Load the chosen scale and root note to the 1 or 2 by using the "LOAD IN CH1" or "LOAD IN CH2" menu items.

### Attack and Decay Envelopes

1. Navigate to the "ATK" or "DCY" menu items.
2. Press the encoder to enter edit mode.
3. Rotate the encoder to adjust the attack time.
4. Press the encoder to confirm the selection.

### Sync Mode

1. Navigate to the "Sync Mode" menu item.
2. Click the encoder to choose between trigger sync and note change sync.

### Octave Shift

1. Navigate to the "Octave Shift" menu item.
2. Click to enter edit mode.
3. Rotate the encoder to adjust the octave shift.
4. Press the encoder to confirm the selection.

### Sensitivity

1. Navigate to the "Sensitivity" menu item.
2. Click to enter edit mode.
3. Rotate the encoder to adjust the sensitivity of the input CV.
4. Press the encoder to confirm the selection.

### Saving Settings

1. Navigate to the "Save Settings" menu item.
2. Press the encoder to save the current settings to flash memory.
3. The display will show a "SAVED" message to confirm the operation.

## Calibration

The module requires calibration to properly quantize the input CV signals. The calibration process involves setting the minimum and maximum values for the input CV range.

To enter calibration mode, power on the module while holding the encoder button. The display will guide you through the calibration process. Input 0V (C1) and 5V(C6) from a CV source like a keyboard or sequencer and press the encoder to confirm the values. The module will save the calibration settings to flash memory.

It's required to re-calibrate the module everytime the firmware is updated.

## Firmware Update

1. Download the latest firmware from the Releases section of the [GitHub repository](https://github.com/VoltageFoundryMod/ForgeSeries/releases). The firmware file is named `CURRENT.UF2`.
2. Connect the module to your computer using a USB-C cable. The CPU can be removed from the module as it's socketed to the main board. Firmware loading can be done with the CPU removed.
3. Use tweezers or a jumper wire to quickly short TWICE the two pads labeled `RESET` on the back of the module CPU. The orange LED will flicker and light up.
4. Copy and overwrite the `CURRENT.UF2` file to the module USB drive named "Seeed XIAO" that will appear. After copy is finished, the module will reboot and the new firmware will be loaded.

![Module bootloader mode](../images/XIAO-reset.gif)

## Troubleshooting

- **No Power**: Ensure the module is properly connected to the power supply and the power jumper is set correctly.
- **No Output**: Verify the board connections and output settings and ensure the module is not stopped.
- **Inconsistent BPM**: Ensure the external clock signal is stable and properly connected.

### No Output CV

- Ensure that the input CV is within the acceptable range.
- Verify that the correct scale and root note are selected.

## Powering

The module can be powered by either 12V or 5V if your power supply supports it. The supply can be selected from an on-board jumper where closing the the SEL with REG jumper, will take power from eurorack 12V supply and closing the SEL with BOARD jumper, will take power from 5V (requires 16 pin cable). It can also be powered by the USB-C jack on the Seeeduino Xiao.

## Specifications

- **Power Supply**: 12V or 5V jumper selectable
- **Input CV Range**: 5V
- **Output CV Range**: 5V
- **Dimensions**: 6HP
- **Depth**: 40mm
- **Current Draw**: 60mA @ +12V or +5V

## Contact

For support and inquiries, please open an issue on the [GitHub repository](https://github.com/VoltageFoundryMod/ForgeSeries).

## Development

If you want to build and developt for the module, check [this file](Building-Developing.md) for more information.

## License

This project is licensed under the MIT License. See the `LICENSE` file for more information.

---

Thank you for choosing the Dual Quantizer module. We hope it enhances your musical creativity and performance.
