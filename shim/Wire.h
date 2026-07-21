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
        // MCP4728 (0x60) Multi-Write: repeating 3-byte blocks
        //   [0x40 | (hwCh<<1)] [vref/pd/gain | D11..8] [D7..0]
        // Hardware channels A,B,C,D map to outputs 0,2,1,3 (B/C swapped on board).
        static const int hwToOut[4] = {0, 2, 1, 3};
        if (_addr == 0x60 && g_host) {
            for (int i = 0; i + 2 < _len; i += 3) {
                if ((_buf[i] & 0xC0) != 0x40) continue; // not a multi-write block
                int hwCh = (_buf[i] >> 1) & 0x03;
                uint16_t v = (uint16_t)((_buf[i + 1] & 0x0F) << 8) | _buf[i + 2];
                g_host->dac[hwToOut[hwCh]] = v;
            }
        }
        _len = 0;
        return 0; // success
    }
    uint8_t endTransmission(bool) { return endTransmission(); }
    uint8_t requestFrom(uint8_t, uint8_t) { return 0; }
    int available() { return 0; }
    int read() { return 0; }
};

extern TwoWire Wire;  // display bus (unused traffic)
extern TwoWire Wire1; // DAC bus
