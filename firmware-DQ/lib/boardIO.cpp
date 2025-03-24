#pragma once

#include <Adafruit_MCP4725.h>
#include <Arduino.h>
#include <Wire.h>

#include "pinouts.hpp"

// Add prototypes for functions defined in this file
void InitIO();
void InternalDAC(uint32_t intDAC_OUT);
void MCP(uint32_t MCP_OUT);
void DACWrite(int pin, uint32_t value);
void PWM1(uint32_t duty1);
void PWM2(uint32_t duty2);
void PWMWrite(int pin, uint32_t value);
void SetPin(int pin, uint32_t value);

// Create the MCP4725 object
Adafruit_MCP4725 dac;
#define DAC_RESOLUTION (12)

// Handle IO devices initialization
void InitIO() {
    // ADC settings. These increase ADC reading stability but at the cost of cycle time. Takes around 0.7ms for one reading with these
    REG_ADC_AVGCTRL |= ADC_AVGCTRL_SAMPLENUM_1;
    ADC->AVGCTRL.reg = ADC_AVGCTRL_SAMPLENUM_128 | ADC_AVGCTRL_ADJRES(4);

    analogWriteResolution(10);
    analogReadResolution(12);

    pinMode(LED_BUILTIN, OUTPUT);        // LED
    pinMode(CLK_IN_PIN, INPUT_PULLDOWN); // CLK in
    pinMode(CV_1_IN_PIN, INPUT);         // IN1
    pinMode(CV_2_IN_PIN, INPUT);         // IN2
    pinMode(ENCODER_SW, INPUT_PULLUP);   // push sw
    pinMode(OUT_PIN_1, OUTPUT);          // CH1 EG out
    pinMode(OUT_PIN_2, OUTPUT);          // CH2 EG out

    // Initialize the DAC
    if (!dac.begin(0x60)) { // 0x60 is the default I2C address for MCP4725
        Serial.println("MCP4725 not found!");
        while (1)
            ;
    }
    Serial.println("MCP4725 initialized.");
    MCP(0);                       // Set the DAC output to 0
    InternalDAC(0);               // Set the internal DAC output to 0
    digitalWrite(OUT_PIN_1, LOW); // Initialize the output pins to low
    digitalWrite(OUT_PIN_2, LOW); // Initialize the output pins to low
}

// Handle DAC Outputs
void InternalDAC(uint32_t value) {
    analogWrite(DAC_INTERNAL_PIN, value / 4); // "/4" -> 12bit to 10bit
}

void MCP(uint32_t value) {
    dac.setVoltage(value, false);
}

// Write to DAC pins indexed by 0
void DACWrite(int pin, uint32_t value) {
    switch (pin) {
    case 0: // Internal DAC
        InternalDAC(value);
        break;
    case 1: // MCP DAC
        MCP(value);
        break;
    default:
        // Handle invalid pin case if necessary
        break;
    }
}

// Write to PWM pins indexed by 0
void PWMWrite(int pin, uint32_t value) {
    switch (pin) {
    case 0: // PWM 1
        pwm(OUT_PINS[0], 46000, value);
        break;
    case 1: // PWM 2
        pwm(OUT_PINS[1], 46000, value);
        break;
    default:
        // Handle invalid pin case if necessary
        break;
    }
}

// Set the output pins. Value can be from 0 to 4095 (12bit).
// For pins 0 and 1, 0 is low and anything else is high
void SetPin(int pin, uint32_t value) {
    switch (pin) {
    case 0: // Gate Output 1
        digitalWrite(OUT_PINS[0], value > 0 ? LOW : HIGH);
        break;
    case 1: // Gate Output 2
        digitalWrite(OUT_PINS[1], value > 0 ? LOW : HIGH);
        break;
    case 2: // Internal DAC Output
        DACWrite(0, value);
        break;
    case 3: // MCP DAC Output
        DACWrite(1, value);
        break;
    default:
        // Handle invalid pin case if necessary
        break;
    }
}
