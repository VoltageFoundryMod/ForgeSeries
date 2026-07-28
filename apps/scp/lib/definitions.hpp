#pragma once
#include <string>

// Define the interruption guard
#define ATOMIC(X)   \
    noInterrupts(); \
    X;              \
    interrupts();

// Define a debug flag and a debug print function
#define DEBUG 1
#define DEBUG_PRINT(X)   \
    if (DEBUG) {         \
        Serial.print(X); \
    }

// Define a one pole filter
// Recommended coefficients:
// Plaits note: 0.0001f
// Stages CV reader: 0.7f
// Fast moving CV: 0.5f
#define ONE_POLE(out, in, coefficient) out += (coefficient) * ((in) - out);
