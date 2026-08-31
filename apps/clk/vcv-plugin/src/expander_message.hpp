#pragma once
// The message ClockForge and its expander pass across the gap between them.
//
// On the hardware these two boards are joined by an eight-way ribbon: power, the
// DAC's I2C bus, and one analog line back to the host. In Rack the equivalent is
// Rack's expander message channel, which is double-buffered and flipped once per
// block, so neither module reads a buffer the other is mid-write on. Reaching
// into the neighbour's ports directly would work most of the time and would be
// order-dependent, which is exactly the sort of bug that only shows up in a
// large patch.
//
// ONE struct for both directions, rather than one per direction. Each module
// writes only its own half, and a module allocates a buffer on BOTH sides
// because the expander is allowed to sit on either side of its parent — the
// ribbon does not care which way the case is arranged either. The cost is a
// handful of unused bytes per side.

struct ForgeExpanderMessage {
    // ── written by ClockForge, read by the expander ──────────────────────────
    // False when no parent is adjacent, or when its EXPANDER setting is NONE:
    // the expander's jacks then sit at 0 V, which is what an unplugged ribbon
    // gives you.
    bool parentActive = false;
    float out[4] = {0.f, 0.f, 0.f, 0.f}; // volts for OUT 5-8

    // ── written by the expander, read by ClockForge ──────────────────────────
    bool expanderPresent = false;
    float in = 0.f; // IN 4, the expander's CV jack
};
