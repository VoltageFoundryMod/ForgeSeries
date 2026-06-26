#pragma once
#include <Arduino.h>

// Maximum number of steps in the Euclidean pattern. This can be adjusted based on memory constraints and desired pattern complexity.
#define MAX_STEPS 64

typedef struct {
    bool enabled;
    int steps;    // Number of steps in the pattern
    int triggers; // Number of triggers in the pattern
    int rotation; // Rotation of the pattern
    int pad;      // No trigger steps added to the end of the pattern
} EuclideanParams;

// Helper function to distribute the pattern based on counts and remainders
void distributePattern(int level, int counts[], int remainders[], int pattern[], int &index) {
    if (level == -1) {
        pattern[index++] = 0; // Add a rest
    } else if (level == -2) {
        pattern[index++] = 1; // Add a trigger
    } else {
        for (int i = 0; i < counts[level]; i++) {
            distributePattern(level - 1, counts, remainders, pattern, index);
        }
        if (remainders[level] != 0) {
            distributePattern(level - 2, counts, remainders, pattern, index);
        }
    }
}

// Euclidean pattern generation based on Bjorklund's algorithm
void GeneratePattern(EuclideanParams &params, int *rhythm) {
    // Temporary arrays for computation
    int counts[MAX_STEPS] = {0};
    int remainders[MAX_STEPS] = {0};
    int pattern[MAX_STEPS] = {0}; // Temporary pattern storage
    int divisor = params.steps - params.triggers;
    int level = 0;

    // Step 1: Initialize counts and remainders
    remainders[0] = params.triggers;

    while (true) {
        counts[level] = divisor / remainders[level];
        remainders[level + 1] = divisor % remainders[level];
        divisor = remainders[level];
        level++;
        if (remainders[level] <= 1)
            break;
    }

    counts[level] = divisor;

    // Step 2: Distribute triggers and rests
    int index = 0;
    distributePattern(level, counts, remainders, pattern, index);

    // Step 3: Rotate the pattern
    for (int i = 0; i < params.steps; i++) {
        rhythm[(i + params.rotation) % params.steps] = pattern[i];
    }

    // Step 4: Add padding to the end of the pattern
    for (int i = params.steps; i < params.steps + params.pad; i++) {
        rhythm[i] = 0;
    }
}
