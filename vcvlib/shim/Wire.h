#pragma once
// I2C (TwoWire) shim. The display bus carries no real traffic (the SSD1306 shim
// is a framebuffer). The DAC bus carries MCP4728 multi-write frames from
// DACWriteAll(); this shim parses those frames back into per-output values and
// stores them in the active HostBridge.
#include "Arduino.h"

class TwoWire {
    uint8_t _addr = 0;
    uint8_t _buf[64];
    int _len = 0;

  public:
    void setSDA(int) {}
    void setSCL(int) {}
    void begin() {}
    void setClock(uint32_t) {}

    void beginTransmission(uint8_t addr) { _addr = addr; _len = 0; }
    size_t write(uint8_t b) {
        if (_len < (int)sizeof(_buf)) _buf[_len++] = b;
        return 1;
    }
    uint8_t endTransmission() {
        // MCP4728 Multi-Write: repeating 3-byte blocks
        //   [0x40 | (hwCh<<1)] [vref/pd/gain | D11..8] [D7..0]
        // Hardware channels A,B,C,D drive outputs in order (see core/boardIO.hpp).
        //
        // Two devices share this bus: the base board at 0x60 driving outputs
        // 0-3, and the expander at 0x61 driving 4-7. The address picks the
        // half of dac[] a frame lands in.
        int base = -1;
        if (_addr == 0x60) base = 0;
        else if (_addr == 0x61) base = 4;
        if (base >= 0 && g_host) {
            // Two frame shapes reach here. Fast Write is what the firmware's
            // hot path sends: eight bytes, two per channel, A-D in order, and
            // the leading bits of each pair are 00. Multi-Write is three bytes
            // per channel led by 01xxxxxx, and is still what init writes to set
            // VREF and gain. The top two bits of the first byte tell them apart.
            if (_len == 8 && (_buf[0] & 0xC0) == 0x00) {
                for (int ch = 0; ch < 4; ch++) {
                    uint16_t v = (uint16_t)((_buf[ch * 2] & 0x0F) << 8) | _buf[ch * 2 + 1];
                    g_host->dac[base + ch] = v;
                }
            } else {
                for (int i = 0; i + 2 < _len; i += 3) {
                    if ((_buf[i] & 0xC0) != 0x40) continue; // not a multi-write block
                    int hwCh = (_buf[i] >> 1) & 0x03;
                    uint16_t v = (uint16_t)((_buf[i + 1] & 0x0F) << 8) | _buf[i + 2];
                    g_host->dac[base + hwCh] = v;
                }
            }
        }
        _len = 0;
        // Every address ACKs. That is why core/boardIO.hpp's ProbeExpander()
        // is useless here and the Rack port decides an expander is present
        // from module adjacency instead.
        return 0; // success
    }
    uint8_t endTransmission(bool) { return endTransmission(); }
    uint8_t requestFrom(uint8_t, uint8_t) { return 0; }
    int available() { return 0; }
    int read() { return 0; }
};

extern TwoWire Wire;  // display bus (unused traffic)
extern TwoWire Wire1; // DAC bus
