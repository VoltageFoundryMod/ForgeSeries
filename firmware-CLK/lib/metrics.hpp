#ifndef METRICS_HPP
#define METRICS_HPP

#include <Arduino.h>
#if !defined(ARDUINO_ARCH_RP2040)
#include <malloc.h>
#endif

/**
 * Performance Metrics System
 *
 * Lightweight monitoring for timing and resource usage.
 * Designed for minimal overhead (~5us per measurement).
 *
 * Usage:
 *   PerformanceMetrics metrics;
 *   metrics.BeginLoopMeasurement();
 *   // ... your code ...
 *   metrics.EndLoopMeasurement();
 *   metrics.PrintStats(); // Call periodically to see results
 */

class PerformanceMetrics {
  private:
    // Loop timing statistics
    uint32_t loopTimeMin = 0xFFFFFFFF;
    uint32_t loopTimeMax = 0;
    uint32_t loopTimeSum = 0;
    uint32_t loopTimeCount = 0;
    uint32_t loopStartTime = 0;

    // ISR timing statistics
    volatile uint32_t isrTimeMin = 0xFFFFFFFF;
    volatile uint32_t isrTimeMax = 0;
    volatile uint32_t isrTimeSum = 0;
    volatile uint32_t isrTimeCount = 0;
    volatile uint32_t isrStartTime = 0; // Dedicated start time (was incorrectly sharing loopStartTime)

    // Display timing statistics (Core 0 GFX prep only)
    uint32_t displayTimeMin = 0xFFFFFFFF;
    uint32_t displayTimeMax = 0;
    uint32_t displayTimeSum = 0;
    uint32_t displayTimeCount = 0;
    uint32_t displayStartTime = 0;

    // Core 1 display flush timing (Wire I2C, volatile — written from Core 1)
    volatile uint32_t core1FlushMin = 0xFFFFFFFF;
    volatile uint32_t core1FlushMax = 0;
    volatile uint32_t core1FlushSum = 0;
    volatile uint32_t core1FlushCount = 0;
    volatile uint32_t core1FlushStart = 0;

    // DAC write timing statistics
    uint32_t dacTimeMin = 0xFFFFFFFF;
    uint32_t dacTimeMax = 0;
    uint32_t dacTimeSum = 0;
    uint32_t dacTimeCount = 0;
    uint32_t dacStartTime = 0;

    // Encoder timing statistics
    uint32_t encoderTimeMin = 0xFFFFFFFF;
    uint32_t encoderTimeMax = 0;
    uint32_t encoderTimeSum = 0;
    uint32_t encoderTimeCount = 0;
    uint32_t encoderStartTime = 0;

    // Sample tracking for periodic stats reset
    uint32_t lastResetTime = 0;
    static const uint32_t RESET_INTERVAL_MS = 5000; // Reset stats every 5 seconds

    // Memory tracking
    uint32_t GetFreeRAM() {
#if defined(ARDUINO_ARCH_RP2040)
        // RP2040: use heap_end / stack pointer estimate
        extern char __StackLimit, __bss_end__;
        return &__StackLimit - &__bss_end__;
#else
        // SAMD21: mallinfo-based estimate
        struct mallinfo mi = mallinfo();
        return 32768 - mi.uordblks - 4096;
#endif
    }

  public:
    PerformanceMetrics() {
        lastResetTime = millis();
    }

    // Main loop timing
    void BeginLoopMeasurement() {
        loopStartTime = micros();
    }

    void EndLoopMeasurement() {
        uint32_t duration = micros() - loopStartTime;
        if (duration < loopTimeMin)
            loopTimeMin = duration;
        if (duration > loopTimeMax)
            loopTimeMax = duration;
        loopTimeSum += duration;
        loopTimeCount++;
    }

    // ISR timing (call from within timer interrupt)
    void BeginISRMeasurement() {
        isrStartTime = micros();
    }

    void EndISRMeasurement() {
        uint32_t duration = micros() - isrStartTime;
        if (duration < isrTimeMin)
            isrTimeMin = duration;
        if (duration > isrTimeMax)
            isrTimeMax = duration;
        isrTimeSum += duration;
        isrTimeCount++;
    }

    // DAC write timing (I2C multi-write transaction)
    void BeginDACMeasurement() {
        dacStartTime = micros();
    }

    void EndDACMeasurement() {
        uint32_t duration = micros() - dacStartTime;
        if (duration < dacTimeMin)
            dacTimeMin = duration;
        if (duration > dacTimeMax)
            dacTimeMax = duration;
        dacTimeSum += duration;
        dacTimeCount++;
    }

    // Core 1 display flush timing (call from loop1(), wraps display.display())
    void BeginCore1FlushMeasurement() {
        core1FlushStart = micros();
    }

    void EndCore1FlushMeasurement() {
        uint32_t duration = micros() - core1FlushStart;
        if (duration < core1FlushMin)
            core1FlushMin = duration;
        if (duration > core1FlushMax)
            core1FlushMax = duration;
        core1FlushSum += duration;
        core1FlushCount++;
    }

    // Display timing
    void BeginDisplayMeasurement() {
        displayStartTime = micros();
    }

    void EndDisplayMeasurement() {
        uint32_t duration = micros() - displayStartTime;
        if (duration < displayTimeMin)
            displayTimeMin = duration;
        if (duration > displayTimeMax)
            displayTimeMax = duration;
        displayTimeSum += duration;
        displayTimeCount++;
    }

    // Encoder timing
    void BeginEncoderMeasurement() {
        encoderStartTime = micros();
    }

    void EndEncoderMeasurement() {
        uint32_t duration = micros() - encoderStartTime;
        if (duration < encoderTimeMin)
            encoderTimeMin = duration;
        if (duration > encoderTimeMax)
            encoderTimeMax = duration;
        encoderTimeSum += duration;
        encoderTimeCount++;
    }

    // Get current statistics
    struct Stats {
        uint32_t min_us;
        uint32_t max_us;
        uint32_t avg_us;
        uint32_t count;
    };

    Stats GetLoopStats() {
        return {loopTimeCount > 0 ? loopTimeMin : 0,
                loopTimeMax,
                loopTimeCount > 0 ? loopTimeSum / loopTimeCount : 0,
                loopTimeCount};
    }

    Stats GetISRStats() {
        return {isrTimeCount > 0 ? isrTimeMin : 0,
                isrTimeMax,
                isrTimeCount > 0 ? isrTimeSum / isrTimeCount : 0,
                isrTimeCount};
    }

    Stats GetDisplayStats() {
        return {displayTimeCount > 0 ? displayTimeMin : 0,
                displayTimeMax,
                displayTimeCount > 0 ? displayTimeSum / displayTimeCount : 0,
                displayTimeCount};
    }

    Stats GetCore1FlushStats() {
        return {core1FlushCount > 0 ? (uint32_t)core1FlushMin : 0,
                (uint32_t)core1FlushMax,
                core1FlushCount > 0 ? (uint32_t)(core1FlushSum / core1FlushCount) : 0,
                (uint32_t)core1FlushCount};
    }

    Stats GetDACStats() {
        return {dacTimeCount > 0 ? dacTimeMin : 0,
                dacTimeMax,
                dacTimeCount > 0 ? dacTimeSum / dacTimeCount : 0,
                dacTimeCount};
    }

    Stats GetEncoderStats() {
        return {encoderTimeCount > 0 ? encoderTimeMin : 0,
                encoderTimeMax,
                encoderTimeCount > 0 ? encoderTimeSum / encoderTimeCount : 0,
                encoderTimeCount};
    }

    // Print comprehensive stats to Serial
    void PrintStats() {
        uint32_t elapsed_ms = millis() - lastResetTime;
        if (elapsed_ms == 0)
            elapsed_ms = 1;

        Stats loop = GetLoopStats();
        Stats isr = GetISRStats();

        // Compute actual rates from sample count over elapsed window
        float loopHz = loop.count * 1000.0f / elapsed_ms;
        float isrHz = isr.count * 1000.0f / elapsed_ms;

        Serial.print(F("\n=== Performance Metrics ["));
        Serial.print(elapsed_ms);
        Serial.print(F("ms, "));
        Serial.print(loop.count);
        Serial.println(F(" loops] ==="));

        // Loop: avg / max / jitter (max-min) / actual rate
        Serial.print(F("Loop:     avg="));
        Serial.print(loop.avg_us);
        Serial.print(F("us  max="));
        Serial.print(loop.max_us);
        Serial.print(F("us  jitter="));
        Serial.print(loop.max_us > loop.min_us ? loop.max_us - loop.min_us : 0);
        Serial.print(F("us  "));
        Serial.print(loopHz, 0);
        Serial.println(F("Hz"));

        // ISR: avg / max / actual rate (should equal PPQN * BPM / 60)
        Serial.print(F("ISR:      avg="));
        Serial.print(isr.avg_us);
        Serial.print(F("us  max="));
        Serial.print(isr.max_us);
        Serial.print(F("us  "));
        Serial.print(isrHz, 0);
        Serial.println(F("Hz"));

        // DAC write (I2C multi-write transaction, RP2040 only)
        Stats dac = GetDACStats();
        if (dac.count > 0) {
            Serial.print(F("DAC:      avg="));
            Serial.print(dac.avg_us);
            Serial.print(F("us  max="));
            Serial.print(dac.max_us);
            if (loop.avg_us > 0) {
                Serial.print(F("us  ("));
                Serial.print((dac.avg_us * 100) / loop.avg_us);
                Serial.print(F("% of loop)"));
            }
            Serial.println();
        }

        // Display GFX prep (Core 0, CPU-only buffer rendering)
        Stats display = GetDisplayStats();
        if (display.count > 0) {
            Serial.print(F("Disp GFX: avg="));
            Serial.print(display.avg_us);
            Serial.print(F("us  max="));
            Serial.print(display.max_us);
            Serial.print(F("us  ("));
            Serial.print(display.count);
            Serial.println(F(" frames, Core0)"));
        }

        // Display Wire flush (Core 1, I2C)
        Stats flush = GetCore1FlushStats();
        if (flush.count > 0) {
            Serial.print(F("Disp I2C: avg="));
            Serial.print(flush.avg_us);
            Serial.print(F("us  max="));
            Serial.print(flush.max_us);
            Serial.print(F("us  ("));
            Serial.print(flush.count);
            Serial.println(F(" frames, Core1)"));
        }

        // Encoder (only when encoder is actively polled)
        Stats encoder = GetEncoderStats();
        if (encoder.count > 0) {
            Serial.print(F("Encoder:  avg="));
            Serial.print(encoder.avg_us);
            Serial.print(F("us  max="));
            Serial.print(encoder.max_us);
            Serial.println(F("us"));
        }

        Serial.print(F("Free RAM: "));
        Serial.print(GetFreeRAM());
        Serial.println(F(" bytes"));

        Serial.println(F("================================\n"));
    }

    // Print compact single-line stats (useful for continuous monitoring)
    void PrintCompact() {
        Stats loop = GetLoopStats();
        Stats isr = GetISRStats();
        Serial.print(F("L:"));
        Serial.print(loop.avg_us);
        Serial.print(F("/"));
        Serial.print(loop.max_us);
        Serial.print(F("us ISR:"));
        Serial.print(isr.avg_us);
        Serial.print(F("/"));
        Serial.print(isr.max_us);
        Serial.print(F("us RAM:"));
        Serial.print(GetFreeRAM());
        Serial.println(F("b"));
    }

    // Auto-reset stats periodically to track recent performance
    void AutoReset() {
        uint32_t now = millis();
        if (now - lastResetTime >= RESET_INTERVAL_MS) {
            Reset();
            lastResetTime = now;
        }
    }

    // Manual reset
    void Reset() {
        loopTimeMin = 0xFFFFFFFF;
        loopTimeMax = 0;
        loopTimeSum = 0;
        loopTimeCount = 0;

        isrTimeMin = 0xFFFFFFFF;
        isrTimeMax = 0;
        isrTimeSum = 0;
        isrTimeCount = 0;

        dacTimeMin = 0xFFFFFFFF;
        dacTimeMax = 0;
        dacTimeSum = 0;
        dacTimeCount = 0;

        displayTimeMin = 0xFFFFFFFF;
        displayTimeMax = 0;
        displayTimeSum = 0;
        displayTimeCount = 0;

        core1FlushMin = 0xFFFFFFFF;
        core1FlushMax = 0;
        core1FlushSum = 0;
        core1FlushCount = 0;

        encoderTimeMin = 0xFFFFFFFF;
        encoderTimeMax = 0;
        encoderTimeSum = 0;
        encoderTimeCount = 0;

        lastResetTime = millis();
    }

    // Quick check if timing is within acceptable bounds
    // Returns true if performance is good
    bool IsHealthy(uint32_t maxLoopUs = 5000, uint32_t maxISRUs = 200) {
        return (loopTimeMax < maxLoopUs) && (isrTimeMax < maxISRUs);
    }

    // Get warning flags
    struct HealthStatus {
        bool loopSlow;    // Loop time excessive
        bool isrSlow;     // ISR taking too long
        bool displaySlow; // Display updates slow
        bool lowMemory;   // RAM critically low
    };

    HealthStatus GetHealth() {
        return {
            loopTimeMax > 10000,    // Loop > 10ms is problematic
            isrTimeMax > 500,       // ISR > 500us could affect timing
            displayTimeMax > 50000, // Display > 50ms is very slow
            GetFreeRAM() < 1024     // Less than 1KB free is concerning
        };
    }
};

#endif // METRICS_HPP
