// Rotary encoder setting
// ENCODER_OPTIMIZE_INTERRUPTS disabled - interrupts interfere with waveform timing
// Using polling mode instead (called in main loop at ~745Hz, fast enough)
// #define ENCODER_OPTIMIZE_INTERRUPTS
#if defined(ARDUINO_ARCH_RP2040)
// RP2040: PJRC Encoder uses architecture-specific macros not available on RP2040.
// Use a minimal polling-based shim with the same read() API.
// IMPORTANT: constructor must NOT call pinMode/digitalRead — pins are not ready
// at global-object-construction time. Call begin() from setupZ() before first read().
class Encoder {
    int _pin1, _pin2;
    long _pos = 0;
    bool _last1 = false, _last2 = false;

  public:
    Encoder(int p1, int p2) : _pin1(p1), _pin2(p2) {}
    void begin() {
        pinMode(_pin1, INPUT_PULLUP);
        pinMode(_pin2, INPUT_PULLUP);
        _last1 = digitalRead(_pin1);
        _last2 = digitalRead(_pin2);
    }
    long read() {
        bool cur1 = digitalRead(_pin1);
        bool cur2 = digitalRead(_pin2);
        // Full 4-state quadrature decoder — identical in principle to PJRC Encoder.
        // Counts all 4 transitions per detent (±1 each) → ±4 per detent total,
        // matching the divisor and hysteresis in HandleEncoderPosition().
        // Invalid/bounce transitions produce 0 — no spurious counts.
        // _pos is negated (CW = negative) to match hardware wiring convention.
        static const int8_t table[16] = {
            0, -1, 1, 0,
            1, 0, 0, -1,
            -1, 0, 0, 1,
            0, 1, -1, 0};
        uint8_t idx = (_last1 << 3) | (_last2 << 2) | (cur1 << 1) | (uint8_t)cur2;
        _pos -= table[idx]; // Negate: CW produces negative counts
        _last1 = cur1;
        _last2 = cur2;
        return _pos;
    }
};
#else
#include <Encoder.h>
#endif
