# Hardware Design

This document provides an overview of the hardware design for the ForgeSeries-CLK module. The module is designed to generate clocks, waveforms, and envelopes, and it is built on a powerful microcontroller platform.

## Microcontroller

The module uses the Seeed XIAO RP2040 microcontroller, which is based on the RP2040 chip. This microcontroller provides sufficient processing power and I/O capabilities to handle the module's functionality.

### Pinout

Looking from the front of the module, counted counter-clockwise starting from the top-left corner, the pinout is as follows:

| Pin | GPIO / Use    | Function       | In/Out | Notes              |
| --- | ------------- | -------------- | ------ | ------------------ |
| 1   | GP26 /A0 / D0 | Input 1        | In     | Clock/Trigger In   |
| 2   | GP27 /A1 / D1 | Input 2        | In     | CV In 1            |
| 3   | GP28 /A2 / D2 | Input 3        | In     | CV In 2            |
| 4   | GP29 /A3 / D3 | Input 4        | In     | CV In 3 (expander) |
| 5   | GP6 / D4      | SDA 1          | Out    | Display Data Line  |
| 6   | GP7 / D5      | SCL 1          | Out    | Display Clock Line |
| 7   | GP0 / D6      | SCL 2          | Out    | DAC Clock Line     |
| 8   | GP1 / D7      | SDA 2          | Out    | DAC Data Line      |
| 9   | GP2 / D8      | Encoder Pin 2  | In     | Encoder Input B    |
| 10  | GP4 / D9      | Encoder Pin 1  | In     | Encoder Input A    |
| 11  | GP3 / D10     | Encoder Switch | In     | Encoder Switch     |
| 12  | 3.3V Out      |                | Out    |                    |
| 13  | GND           | MODE button    | In     |                    |
| 14  | VBUS          | SHIFT button   | In     |                    |
