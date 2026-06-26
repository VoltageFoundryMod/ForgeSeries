#pragma once

#include <Adafruit_MCP4725.h>
#include <Arduino.h>
#include <Wire.h>

#include "pinouts.hpp"

// Add prototypes for functions defined in this file
void InitIO();
void InternalDAC(int intDAC_OUT);
void MCP(int MCP_OUT);
void DACWrite(int pin, int value);
void PWM1(int duty1);
void PWM2(int duty2);
void PWMWrite(int pin, int value);
void SetPin(int pin, int value);

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
    dac.begin(0x60);
}

// Handle DAC Outputs
void InternalDAC(int intDAC_OUT) {
    analogWrite(DAC_INTERNAL_PIN, intDAC_OUT / 4); // "/4" -> 12bit to 10bit
}

void MCP(int MCP_OUT) {
    dac.setVoltage(MCP_OUT, false);
}

void DACWrite(int pin, int value) {
    value = constrain(value, 0, 4095);
    switch (pin) {
    case 1:
        InternalDAC(value);
        break;
    case 2:
        MCP(value);
        break;
    }
}

// Handle PWM Outputs
void PWM1(int duty1) {
    pwm(OUT_PIN_1, 46000, duty1);
}

void PWM2(int duty2) {
    pwm(OUT_PIN_2, 46000, duty2);
}

void PWMWrite(int pin, int value) {
    if (pin == 1) // PWM 1
    {
        PWM1(value);
    } else if (pin == 2) // PWM 2
    {
        PWM2(value);
    }
}

// Set the output pins to HIGH or LOW
void SetPin(int pin, int value) {
    switch (pin) {
    case 0:
        value ? digitalWrite(OUT_PIN_1, LOW) : digitalWrite(OUT_PIN_1, HIGH);
        break;
    case 1:
        value ? digitalWrite(OUT_PIN_2, LOW) : digitalWrite(OUT_PIN_2, HIGH);
        break;
    case 2:
        value ? InternalDAC(4095) : InternalDAC(0);
        break;
    case 3:
        value ? MCP(4095) : MCP(0);
        break;
    }
}
