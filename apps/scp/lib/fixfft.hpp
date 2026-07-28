#pragma once
#include <cstdint>
// Vendored fixed-point integer FFT (8-bit) — self-contained, no Arduino/AVR deps.
//
// Adapted from the public-domain fix_fft (Tom Roberts 1989; portable by Malcolm
// Slaney 1994; enhanced by Dimitrios Bouras 2006; 8-bit by David Keller 2010).
// The PROGMEM / pgm_read_byte access has been dropped so this single source
// compiles unchanged both on the RP2040 firmware and inside the VCV Rack engine
// (mirroring how quantizer/scales are vendored in the ClockForge project).
//
//   fix_fft(fr, fi, m, inverse): in-place FFT of 2**m points.
//     fr[], fi[] are the real/imaginary arrays (INPUT and RESULT).
//     inverse = 0 -> forward FFT, 1 -> inverse. Returns the scale shift.

// int8_t (not plain char): char is unsigned on ARM EABI targets like the
// RP2040, which silently breaks all the signed fixed-point math here.
int fix_fft(int8_t fr[], int8_t fi[], int m, int inverse);
int fix_fftr(int8_t f[], int m, int inverse);
