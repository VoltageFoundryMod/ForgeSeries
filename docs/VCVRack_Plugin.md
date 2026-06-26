# VCV Rack Plugin

This is a reference document for the VCV Rack plugin for the ForgeSeries-CLK module. The plugin is a faithful emulation of the hardware module, and it can be used in VCV Rack to generate clocks, waveforms, and envelopes.

## Features

The module replicates the functionality of the hardware module, including:

- **Global BPM Control**: Set the global BPM for all outputs.
- **Multiple Clock Outputs**: Four clock outputs with individual settings.
- **Adjustable Clock Multiplication and Division**: Configure each output to multiply or divide the global BPM.
- **Output Waveform Generation**: Outputs can generate different waveforms for modulation.
- **Pulse Probability**: Set the probability of a pulse occurring.
- **Euclidean Rhythm Generation**: Generate complex rhythms using Euclidean algorithm.
- **Custom Swing Patterns**: Apply swing to each output individually.
- **Sync to External Clock Sources**: Automatically adjust BPM based on an external clock signal with adjustable clock divider.
- **Phase Shift**: Adjust the phase of the output in relation to the master clock.
- **Waveform Duty Cycle and Level/Offset**: Adjust the pulse width and level/offset of the clock signal.
- **External modulation**: Many parameters can be modulated by the CV inputs by assigning CV targets.
- **Tap Tempo Functionality**: Manually set the BPM by tapping a button.
- **Quantization**: Quantize the output wave or CV input to some scale and root note.
- **Envelopes**: Outputs can generate different types of envelopes (AD, AR, ADSR) triggered by CV inputs.
- **Cross Operations**: Modulate an output with another output or CV input using arithmetic, logic and sample/hold operations.
- **Loops**: Rewind an output's pattern every few beats to build repeating, structured random/Euclidean phrases, with nap/wake muting.
- **Save/Load Configuration**: Save and load up to 10 configurations.

## Hardware Differences

- VCV Version abstracts the hardware with a shim layer for some of the hardware features like
  - Display rendering abstraction of the GFX library
  - Outputs abstraction of the DAC
  - CV inputs abstraction of the ADC
  - Clock generation abstraction of the timers
  - Encoder control abstraction of the rotary encoder
  - Preset saving/loading abstraction of the EEPROM
- The VCV version have the ability to switch CV input range from 0-5V to -5V to +5V, which is not possible in the hardware version.

## Keyboard Shortcuts

Encoder Left: **[**
Encoder Right: **]**
Encoder Push: **Spacebar**

## Settings

These go into a context menu when right-clicking on the module in VCV Rack.

- Input CV Range: 0-5V or -5V to +5V
- Encoder Sensitivity: Low, Medium, High
