Features:

- [ ] Implement internal modulators for parameters
- [x] Add an operation to invert the output signal for each channel

Project Improvements:

- [x] Better resolution to generate cleaner waveforms
- [x] Better detection of input clock with different PPQNs (dividers)
- [x] Update menus to be able to change settings to all 4 outputs and not the previous outputs 3 and 4 which had DAC.
- [x] Add outputs 1 and 2 to the CV target options in the menu
- [x] Add outputs 1 and 2 to the quantization options in the menu
- [x] Timeout to main screen off or set seconds
- [ ] Organize code to find the defines and configurations easier also centralize default configs
- [x] Make the screen output indicators follow the outputs with probability, euclidean and things that affect it
- [x] Probability doesn't affect outputs with envelopes
- [ ] Improve user manual and documentation
- [x] Check the slight clipping in the outputs... could be adjustment but also firmware calibration issue
- [x] Add hatchet x2 and x4 to the output waveform where x2 generates 2 square pulses in one up clock cycle and x4 generates 4 square pulses in one up clock cycle, this will be useful for people who want to generate faster clock divisions without having to change the master clock BPM
- [x] Review overlapping text in the menu items
- [x] Error on envelope generation when outut is off, small pulse is generated.
- [x] Check if there are general improvements to the clock generation and engine
- [x] Cleanup SAMD21 code to avoid clutter
- [x] Review the calibration process since we capped the max output voltage to make sure in quantizer mode it still quantizes correctly to the notes and not to the voltage steps of the DAC
- [x] Implement mixer functions for the outputs like sum, multiply, average
- [x] Add operation for Seed which is pending
- [x] Add loop mode
- [ ] Add automated tests for the firmware functions
- [ ] Create expansion modules (auto-detect it?). Initially one with 4 outputs and 1 CV Input.

Fixes to Board

- [x] Change the pins from MCP main I2C to the new available pins 0 and 1.
- [x] Fix the voltage output from the R2R OPAMP
- [x] Channels 2 and 3 of the DAC are swapped
- [x] Push 6V into the output OpAmp so we can properly get the 0-5V range

Test:

- [ ] All 4 outputs
- [ ] CV input calibration
- [ ] Quantizer
- [ ] CV Inputs and routing
- [ ] Waveforms and Envelopes
- [ ] Test envelopes with external clock

Misc:

- [ ] Create a VCV Rack plugin for the module
