#pragma once

// clockEngine.hpp — Clock generation, BPM control, external clock sync
//
// Owns: PPQN, BPM, tickCounter, external-clock state, timer init.
// Depends on: outputs[] and metrics (extern, defined in main.cpp).
// Calling convention: ClockPulse() is the timer ISR entry point.
//   RP2040 : add_repeating_timer_us lambda → ClockPulse()

#include "boardIO.hpp"
#include "metrics.hpp"
#include "outputs.hpp"
#include "pinouts.hpp"
#include <Arduino.h>
#include <hardware/timer.h>

static repeating_timer_t _clockTimer;

// ── PPQN ─────────────────────────────────────────────────────────────────────
// RP2040 @ 133 MHz: 960 PPQN → 208 µs/tick at 300 BPM — plenty of ISR headroom.
// 5× higher resolution = 5× smoother waveform steps at all BPMs.
#define PPQN 960

// ── BPM & timer globals
// ───────────────────────────────────────────────────────
unsigned int BPM = 120;
unsigned int lastInternalBPM = 120;
unsigned int const minBPM = 10;
unsigned int const maxBPM = 300;

// ── Tick counter (incremented every PPQN tick in ClockPulse ISR) ─────────────
volatile unsigned long tickCounter = 0;

// ── External clock state
// ────────────────────────────────────────────────────── clockInterval and
// lastClockInterruptTime are in MICROSECONDS for better BPM precision.
// HandleExternalClock() also uses micros().
volatile unsigned long clockInterval = 0; // µs between processed QN pulses
volatile unsigned long lastClockInterruptTime =
    0; // µs timestamp of last valid edge
volatile bool usingExternalClock = false;
volatile unsigned long externalTickCounter = 0;
bool extClockBlinkState = false; // toggled each QN for the blink indicator

static int const dividerAmount = 7;
int externalClockDividers[dividerAmount] = {1, 2, 4, 8, 16, 24, 48};
// Labels reflect the number of pulses-per-quarter-note the attached clock
// sends. Example: Pamela's Pro Workout default output → 24PPQN.
String externalDividerDescription[dividerAmount] = {
    "1PPQN", "2PPQN", "4PPQN", "8PPQN", "16PPQN", "24PPQN", "48PPQN"};
int externalDividerIndex = 0;

// ── extern refs to objects defined in main.cpp ───────────────────────────────
extern Output outputs[NUM_OUTPUTS];
extern PerformanceMetrics metrics;
extern bool displayRefresh;
extern bool unsavedChanges;

// Set by encoder handler; applied by loop() so SDK timer calls never block
// encoder polling.
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
    // All time-keeping uses microseconds for higher BPM precision.
    // millis()-based calculation loses ~4% accuracy at typical PPQN/BPM combos.
    unsigned long currentTime = micros();

    // Debounce: ignore edges closer than 1 ms (protects against switch bounce
    // and shields the fast 48PPQN path, whose minimum interval at 300 BPM is ~4
    // ms)
    if (currentTime - lastClockInterruptTime < 1000UL) {
        return;
    }

    unsigned long interval = currentTime - lastClockInterruptTime;
    lastClockInterruptTime = currentTime;

    // Reject intervals that are too short (noise) or unreasonably long.
    // Upper bound: the longest possible valid interval = 1PPQN at minBPM.
    // Using the selected divider: max_interval_µs = 60,000,000 / (minBPM * 1)
    // We use divider=1 as the reference so the filter accepts any configured
    // PPQN.
    const unsigned long maxValidInterval =
        60000000UL / minBPM; // µs, conservatively for 1PPQN
    if (interval < 2000UL || interval > maxValidInterval) {
        return;
    }

    // Ring-buffer of the last 3 inter-pulse intervals for averaging.
    // New intervals that differ from the current one by more than 50% are treated
    // as outliers (e.g. cable reconnect, first pulse after a pause).
    static unsigned long intervals[3] = {0, 0, 0};
    static int intervalIndex = 0;

    intervals[intervalIndex] = interval;
    intervalIndex = (intervalIndex + 1) % 3;

    unsigned long averageInterval = 0;
    int validIntervals = 0;
    for (int i = 0; i < 3; i++) {
        if (intervals[i] > 0 &&
            abs((long)intervals[i] - (long)interval) < (long)(interval / 2)) {
            averageInterval += intervals[i];
            validIntervals++;
        }
    }
    if (validIntervals == 0)
        return;
    averageInterval /= validIntervals;

    // Only act on every N-th pulse where N = divider (= pulses per quarter note)
    if (externalTickCounter % externalClockDividers[externalDividerIndex] == 0) {
        // QN period in µs = inter-pulse interval × pulses-per-QN
        unsigned long qnPeriodUs =
            averageInterval *
            (unsigned long)externalClockDividers[externalDividerIndex];
        clockInterval = qnPeriodUs;

        unsigned int newBPM = (unsigned int)roundf(60000000.0f / (float)qnPeriodUs);

        noInterrupts();
        bool wasExternal = usingExternalClock;
        // Capture the current internal BPM before it is replaced, so we can
        // restore it when the external clock disconnects. Do this once, on the
        // first pulse of each new sync session (mode transition).
        if (!wasExternal)
            lastInternalBPM = BPM;
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            outputs[i].SetExternalClock(true);
            outputs[i].IncrementInternalCounter();
        }
        usingExternalClock = true;
        // Reset the internal tick counter only on the FIRST sync (mode transition).
        // Resetting it every QN causes waveform phase glitches for dividers slower
        // than x1, where the output period spans multiple QNs.
        if (!wasExternal)
            tickCounter = 0;
        interrupts();

        // Toggle the blink indicator on every QN pulse (drives the E blink on main
        // screen).
        extClockBlinkState = !extClockBlinkState;
        displayRefresh = 1;

        // Hysteresis: only restart the hardware timer if BPM changed meaningfully.
        // Avoids jitter-driven timer restarts every QN for a stable clock source.
        if (abs((int)newBPM - (int)BPM) > 2) {
            UpdateBPM(newBPM);
            DEBUG_PRINT("External clock connected");
        }
    }
    externalTickCounter++;
}

// ─────────────────────────────────────────────────────────────────────────────
// Called every loop() to detect external clock timeout and revert to internal
// ─────────────────────────────────────────────────────────────────────────────
void HandleExternalClock() {
    if (!usingExternalClock)
        return;
    unsigned long currentTime = micros();
    // Adaptive timeout: wait at least 2 full QN periods before declaring the
    // clock lost, but clamp the result to 1.5 s – 3 s so slow sources (1PPQN
    // at low BPM) still feel responsive without false-triggering on fast ones.
    unsigned long timeoutUs =
        constrain(2UL * clockInterval, 1500000UL, 3000000UL);
    if ((currentTime - lastClockInterruptTime) > timeoutUs) {
        usingExternalClock = false;
        BPM = lastInternalBPM;
        UpdateBPM(BPM);
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            outputs[i].SetExternalClock(false);
        }
        // Reset the ring buffer so stale intervals don't affect the next sync
        externalTickCounter = 0;
        extClockBlinkState = false; // reset so next sync starts at a known state
        displayRefresh = 1;
        DEBUG_PRINT("External clock disconnected");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Set the hardware timer period to match newBPM
// ─────────────────────────────────────────────────────────────────────────────
void UpdateBPM(unsigned int newBPM) {
    BPM = constrain(newBPM, minBPM, maxBPM);
    cancel_repeating_timer(&_clockTimer);
    int64_t intervalUs = 60000000LL / BPM / PPQN;
    add_repeating_timer_us(
        -intervalUs,
        [](repeating_timer_t *) -> bool {
            ClockPulse();
            return true;
        },
        nullptr, &_clockTimer);
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
    // Signal the display to refresh so blinking output boxes stay in sync
    // with the actual pulse state. The display manager's 50ms time gate limits
    // the real rendering rate regardless of how often this is set.
    displayRefresh = 1;
    metrics.EndISRMeasurement();
}

// ─────────────────────────────────────────────────────────────────────────────
// Start the hardware timer at the current BPM
// ─────────────────────────────────────────────────────────────────────────────
void InitializeTimer() {
    int64_t intervalUs = 60000000LL / BPM / PPQN;
    if (intervalUs <= 0)
        intervalUs = 60000000LL / 120 / PPQN; // fallback 120 BPM
    add_repeating_timer_us(
        -intervalUs,
        [](repeating_timer_t *) -> bool {
            ClockPulse();
            return true;
        },
        nullptr, &_clockTimer);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tap tempo — compute BPM from the last 3 taps
// ─────────────────────────────────────────────────────────────────────────────
static unsigned long _tapLastTime = 0;
static unsigned long _tapTimes[3] = {0, 0, 0};
static int _tapIndex = 0;
void SetTapTempo() {
    if (usingExternalClock)
        return;
    unsigned long now = millis();
    if (now - _tapLastTime > 2000)
        _tapIndex = 0;
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
