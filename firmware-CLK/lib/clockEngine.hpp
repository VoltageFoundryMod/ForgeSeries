#pragma once

// clockEngine.hpp — Clock generation, BPM control, external clock sync
//
// Owns: PPQN, BPM, tickCounter, external-clock state, timer init.
// Depends on: outputs[] and metrics (extern, defined in main.cpp).
// Calling convention: ClockPulse() is the timer ISR entry point.
//   RP2040 : add_repeating_timer_us lambda → ClockPulse()
//   SAMD21 : TimerTcc0.attachInterrupt(ClockPulse)

#include <Arduino.h>

#if defined(ARDUINO_ARCH_RP2040)
#include <hardware/timer.h>
static repeating_timer_t _clockTimer;
#else
#include <TimerTCC0.h>
#endif

#include "boardIO.hpp"
#include "metrics.hpp"
#include "outputs.hpp"
#include "pinouts.hpp"

// ── PPQN ─────────────────────────────────────────────────────────────────────
// RP2040 @ 133 MHz: 960 PPQN → 208 µs/tick at 300 BPM — plenty of ISR headroom.
// SAMD21 @ 48 MHz:  192 PPQN → 1042 µs/tick at 300 BPM — unchanged.
// 5× higher resolution = 5× smoother waveform steps at all BPMs.
#if defined(ARDUINO_ARCH_RP2040)
#define PPQN 960
#else
#define PPQN 192
#endif

// ── BPM & timer globals ───────────────────────────────────────────────────────
unsigned int BPM            = 120;
unsigned int lastInternalBPM = 120;
unsigned int const minBPM   = 10;
unsigned int const maxBPM   = 300;

// ── Tick counter (incremented every PPQN tick in ClockPulse ISR) ─────────────
volatile unsigned long tickCounter = 0;

// ── External clock state ──────────────────────────────────────────────────────
volatile unsigned long clockInterval          = 0;
volatile unsigned long lastClockInterruptTime = 0;
volatile bool          usingExternalClock     = false;
volatile unsigned long externalTickCounter    = 0;

static int const dividerAmount = 7;
int    externalClockDividers[dividerAmount]    = {1, 2, 4, 8, 16, 24, 48};
String externalDividerDescription[dividerAmount] = {"x1", "/2 ", "/4", "/8", "/16", "24PPQN", "48PPQN"};
int    externalDividerIndex                    = 0;

// ── extern refs to objects defined in main.cpp ───────────────────────────────
extern Output            outputs[NUM_OUTPUTS];
extern PerformanceMetrics metrics;
extern bool              displayRefresh;
extern bool              unsavedChanges;

// Set by encoder handler; applied by loop() so SDK timer calls never block encoder polling.
bool bpmNeedsUpdate = false;

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations (functions reference each other)
// ─────────────────────────────────────────────────────────────────────────────
void ClockPulse();
void UpdateBPM(unsigned int newBPM);
void SetTapTempo();

// ─────────────────────────────────────────────────────────────────────────────
// External clock interrupt service routine
// Called on every rising edge of the CLK IN jack.
// ─────────────────────────────────────────────────────────────────────────────
void ClockReceived() {
    unsigned long currentTime = millis();
    // Debounce: ignore interrupts closer than 1 ms
    if (currentTime - lastClockInterruptTime < 1) {
        return;
    }

    unsigned long interval = currentTime - lastClockInterruptTime;
    lastClockInterruptTime = currentTime;

    // Ignore obviously wrong intervals (too short or too long)
    if (interval < 10 || interval > 2000) {
        return;
    }

    static unsigned long intervals[3] = {0, 0, 0};
    static int intervalIndex = 0;

    intervals[intervalIndex] = interval;
    intervalIndex = (intervalIndex + 1) % 3;

    // Calculate average interval, excluding outliers
    unsigned long averageInterval = 0;
    int validIntervals = 0;
    for (int i = 0; i < 3; i++) {
        if (intervals[i] > 0 && abs((long)intervals[i] - (long)interval) < interval / 2) {
            averageInterval += intervals[i];
            validIntervals++;
        }
    }

    if (validIntervals > 0) {
        averageInterval /= validIntervals;
    } else {
        return;
    }

    // Divide the external clock signal by the selected divider
    if (externalTickCounter % externalClockDividers[externalDividerIndex] == 0) {
        if (averageInterval > 0) {
            clockInterval = averageInterval;
            unsigned int newBPM = 60000 / (averageInterval * externalClockDividers[externalDividerIndex]);
            // Add hysteresis to BPM changes
            if (abs((int)newBPM - (int)BPM) > 3) {
                UpdateBPM(newBPM);
                displayRefresh = 1;
                DEBUG_PRINT("External clock connected");
            }
        }

        // Update outputs
        noInterrupts(); // Critical section
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            outputs[i].SetExternalClock(true);
            outputs[i].IncrementInternalCounter();
        }
        usingExternalClock = true;
        tickCounter = 0;
        interrupts();
    }
    externalTickCounter++;
}

// ─────────────────────────────────────────────────────────────────────────────
// Called every loop() to detect external clock timeout and revert to internal
// ─────────────────────────────────────────────────────────────────────────────
void HandleExternalClock() {
    unsigned long currentTime = millis();
    if (usingExternalClock && (currentTime - lastClockInterruptTime) > 2000) {
        usingExternalClock = false;
        BPM = lastInternalBPM;
        UpdateBPM(BPM);
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            outputs[i].SetExternalClock(false);
        }
        displayRefresh = 1;
        DEBUG_PRINT("External clock disconnected");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Set the hardware timer period to match newBPM
// ─────────────────────────────────────────────────────────────────────────────
void UpdateBPM(unsigned int newBPM) {
    BPM = constrain(newBPM, minBPM, maxBPM);
#if defined(ARDUINO_ARCH_RP2040)
    cancel_repeating_timer(&_clockTimer);
    int64_t intervalUs = 60000000LL / BPM / PPQN;
    add_repeating_timer_us(-intervalUs, [](repeating_timer_t *) -> bool { ClockPulse(); return true; }, nullptr, &_clockTimer);
#else
    TimerTcc0.setPeriod(60L * 1000 * 1000 / BPM / PPQN / 4);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Timer ISR — called every 1/PPQN of a beat
// ─────────────────────────────────────────────────────────────────────────────
void ClockPulse() {
    // CRITICAL: This ISR must not be delayed by encoder interrupts.
    metrics.BeginISRMeasurement();

    for (int i = 0; i < NUM_OUTPUTS; i++) {
        outputs[i].Pulse(PPQN, tickCounter);
    }
    tickCounter++;

#if !defined(ARDUINO_ARCH_RP2040)
    // SAMD21: outputs 0-1 are DigitalOut — safe to drive from ISR
    // DAC outputs 2-3 are updated in main loop HandleOutputs()
    SetPin(0, outputs[0].GetOutputLevel());
    SetPin(1, outputs[1].GetOutputLevel());
#endif
    // RP2040: ALL outputs are MCP4728 I2C DAC — I2C must NOT be called from
    // an interrupt context (deadlock). All 4 outputs flushed in HandleOutputs().

    metrics.EndISRMeasurement();
}

// ─────────────────────────────────────────────────────────────────────────────
// Start the hardware timer at the current BPM
// ─────────────────────────────────────────────────────────────────────────────
void InitializeTimer() {
#if defined(ARDUINO_ARCH_RP2040)
    int64_t intervalUs = 60000000LL / BPM / PPQN;
    if (intervalUs <= 0) intervalUs = 60000000LL / 120 / PPQN;  // fallback 120 BPM
    add_repeating_timer_us(-intervalUs, [](repeating_timer_t *) -> bool {
        ClockPulse();
        return true;
    }, nullptr, &_clockTimer);
#else
    TimerTcc0.initialize();
    TimerTcc0.attachInterrupt(ClockPulse);
    NVIC_SetPriority(TCC0_IRQn, 0); // Highest priority (0)
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Tap tempo — compute BPM from the last 3 taps
// ─────────────────────────────────────────────────────────────────────────────
static unsigned long _tapLastTime = 0;
static unsigned long _tapTimes[3]  = {0, 0, 0};
static int           _tapIndex     = 0;
void SetTapTempo() {
    if (usingExternalClock) return;
    unsigned long now = millis();
    if (now - _tapLastTime > 2000) _tapIndex = 0;
    if (_tapIndex < 3) {
        _tapTimes[_tapIndex] = now;
        _tapIndex++;
        _tapLastTime = now;
    }
    if (_tapIndex == 3) {
        unsigned long averageTime = (_tapTimes[2] - _tapTimes[0]) / 2;
        UpdateBPM(60000 / averageTime);
        _tapIndex++;
        unsavedChanges = true;
    }
}
