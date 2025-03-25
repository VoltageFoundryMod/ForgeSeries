#include <Arduino.h>
#include <math.h>

#include "boardIO.hpp"
#include "loadsave.hpp"
#include "pinouts.hpp"

CalibrationData calib; // Create a calibration data object

void WaitForEncoderPress() {
    while (digitalRead(ENCODER_SW) == LOW) {
        delay(100); // Wait for the encoder button to be released
    }
    while (digitalRead(ENCODER_SW) == HIGH) {
        delay(100); // Wait for the encoder button to be pressed
    }
}

void CalibrateDAC(Adafruit_SSD1306 &display) {
    // Calibrate Outputs
    // Divide the range of 12bit DAC (0-4095) into 0-5V range
    // 0V = 0, 1V = 819, 2V = 1638, 3V = 2457, 4V = 3276, 5V = 4095
    uint32_t dacValues[] = {0, 819, 1638, 2457, 3276, 4095};
    String outputNames[] = {"0V", "1V", "2V", "3V", "4V", "5V"};

    // Use the encoder to select the output voltages
    for (int i = 0; i < 6; i++) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.print("Calibrating outputs");
        display.setCursor(0, 20);
        display.print("Measure the outputs");
        display.setCursor(0, 30);
        display.print("adjust the trimmers");
        display.setCursor(0, 45);
        display.print("Set output to ");
        display.print(outputNames[i]);
        display.display();
        for (int j = 2; j < 2 + NUM_DAC_OUTS; j++) {
            SetPin(j, dacValues[i]);
        }
        WaitForEncoderPress(); // Wait for user to press the encoder
    }
    display.setCursor(0, 56);
    display.print("Press to continue");
    display.display();
    WaitForEncoderPress(); // Wait for user to press the encoder
}

void CalibrateADC(Adafruit_SSD1306 &display) {
    // Calibrate Inputs
    // Build a calibration table for the inputs
    // The calibration table is a 2D array with the following format:
    // [channel][correction value]
    // The correction value is the difference between the expected and measured values
    // in ranges like 0-1V, 1-2V, 2-3V, 3-4V, 4-5V
    uint32_t dacValues[] = {0, 819, 1638, 2457, 3276, 4095};
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.print("Calibrating inputs...");
    display.setCursor(0, 12);
    display.print("Make sure outputs are");
    display.setCursor(0, 22);
    display.print("well calibrated.");
    display.setCursor(0, 34);
    display.print("Connect out 3 to CV 1");
    display.setCursor(0, 44);
    display.print("Connect out 4 to CV 2");
    display.setCursor(0, 56);
    display.print("Press to start");
    display.display();
    WaitForEncoderPress(); // Wait for user to press the encoder
    for (int i = 0; i < 6; i++) {
        // Set the DAC
        for (int j = 2; j < 2 + NUM_DAC_OUTS; j++) {
            SetPin(j, dacValues[i]);
        }
        // Wait for the DAC to settle
        delay(500);
        // Read the input voltage
        calib.calibrationValues[0][i] = analogRead(CV_1_IN_PIN);
        calib.calibrationValues[1][i] = analogRead(CV_2_IN_PIN);

        // For calibration, record the difference between the correct and measured values
        calib.calibrationValues[0][i] = abs(calib.calibrationValues[0][i] - dacValues[i]);
        calib.calibrationValues[1][i] = abs(calib.calibrationValues[1][i] - dacValues[i]);
    }
    display.clearDisplay();
    display.setCursor(0, 30);
    display.print("Finished");
    display.setCursor(0, 40);
    display.print("press to continue...");
    display.display();
    WaitForEncoderPress(); // Wait for user to press the encoder
    SaveCalibrationData(calib);
}

// The calibration is applied from ranges like 0-1V, 1-2V, 2-3V, 3-4V, 4-5V
uint32_t ApplyCalibration(int channel, uint32_t value) {
    // Apply the calibration to the value
    // The calibration is applied from ranges like 0-1V, 1-2V, 2-3V, 3-4V, 4-5V
    // The correction value is the difference between the expected and measured values
    u_int32_t newValue;
    if (value <= 819) {
        // 0-1V
        newValue = value - calib.calibrationValues[channel][0];
    } else if (value > 819 && value <= 1638) {
        // 1-2V
        newValue = value - calib.calibrationValues[channel][1];
    } else if (value > 1638 && value <= 2457) {
        // 2-3V
        newValue = value - calib.calibrationValues[channel][2];
    } else if (value > 2457 && value <= 3276) {
        // 3-4V
        newValue = value - calib.calibrationValues[channel][3];
    } else if (value > 3276 && value <= 4095) {
        // 4-5V
        newValue = value - calib.calibrationValues[channel][4];
    } else {
        // 5V
        newValue = value - calib.calibrationValues[channel][5];
    }
    newValue = constrain(newValue, 0, 4095); // Constrain the value to the range of 0-4095
    // Return the corrected value
    DEBUG_PRINT("Channel: ");
    DEBUG_PRINT(channel);
    DEBUG_PRINT(" | ");
    DEBUG_PRINT("Received value: ");
    DEBUG_PRINT(value);
    DEBUG_PRINT(" | ");
    DEBUG_PRINT("Calibrated value: ");
    DEBUG_PRINT(newValue);
    DEBUG_PRINT("\n");
    return newValue;
}

void LoadCalibration() {
    // Load the calibration data from flash memory
    LoadCalibrationData(calib);
}
