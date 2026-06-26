#pragma once
// Adafruit_MCP4728 shim. The firmware's main output path is DACWriteAll() over
// raw Wire1 (parsed in Wire.h); this class covers the InitDAC()/DACWrite() path
// via setChannelValue(), routing channel values into the active HostBridge.
#include "Arduino.h"
#include "Wire.h"

typedef enum {
    MCP4728_CHANNEL_A = 0,
    MCP4728_CHANNEL_B,
    MCP4728_CHANNEL_C,
    MCP4728_CHANNEL_D,
} MCP4728_channel_t;

typedef enum { MCP4728_VREF_VDD = 0, MCP4728_VREF_INTERNAL } MCP4728_vref_t;
typedef enum { MCP4728_GAIN_1X = 0, MCP4728_GAIN_2X } MCP4728_gain_t;
typedef enum { MCP4728_PD_MODE_NORMAL = 0 } MCP4728_pd_mode_t;

class Adafruit_MCP4728 {
  public:
    bool begin(uint8_t = 0x60, TwoWire * = nullptr) { return true; }
    bool setChannelValue(MCP4728_channel_t ch, uint16_t value,
                         MCP4728_vref_t = MCP4728_VREF_VDD,
                         MCP4728_gain_t = MCP4728_GAIN_1X,
                         MCP4728_pd_mode_t = MCP4728_PD_MODE_NORMAL,
                         bool = false) {
        // Hardware channels A,B,C,D map to outputs 0,2,1,3 (B/C swapped on board).
        static const int hwToOut[4] = {0, 2, 1, 3};
        if (g_host) g_host->dac[hwToOut[(int)ch]] = value;
        return true;
    }
};
