#pragma once
// RP2040 hardware/timer.h shim.
// On hardware, add_repeating_timer_us() schedules ClockPulse() at the PPQN rate.
// In the hosted engine the module drives ClockPulse() itself (control-rate
// accumulator keyed to BPM/PPQN), so these become inert bookkeeping stubs.
#include <cstdint>

typedef struct repeating_timer { int dummy; } repeating_timer_t;

typedef bool (*repeating_timer_callback_t)(repeating_timer_t *);

inline bool add_repeating_timer_us(int64_t /*delay_us*/,
                                   repeating_timer_callback_t /*cb*/,
                                   void * /*user_data*/,
                                   repeating_timer_t * /*out*/) {
    return true;
}
inline bool cancel_repeating_timer(repeating_timer_t * /*timer*/) { return true; }
