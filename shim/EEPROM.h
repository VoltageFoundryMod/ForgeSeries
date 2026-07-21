#pragma once
// Arduino EEPROM emulation shim, backed by a flat byte buffer.
// The engine exposes this buffer so the host module can persist it via Rack's
// dataToJson / dataFromJson (presets + calibration survive patch save/load).
#include <cstdint>
#include <cstring>
#include <vector>

class EEPROMClass {
  public:
    std::vector<uint8_t> data;

    void begin(int size) {
        if ((int)data.size() < size) data.resize(size, 0xFF);
    }
    bool commit() { return true; }
    void end() {}

    uint8_t read(int idx) { return (idx >= 0 && idx < (int)data.size()) ? data[idx] : 0xFF; }
    void write(int idx, uint8_t v) { if (idx >= 0 && idx < (int)data.size()) data[idx] = v; }

    template <typename T> T &get(int idx, T &t) {
        if (idx >= 0 && idx + (int)sizeof(T) <= (int)data.size())
            std::memcpy(&t, &data[idx], sizeof(T));
        return t;
    }
    template <typename T> const T &put(int idx, const T &t) {
        if (idx + (int)sizeof(T) > (int)data.size()) data.resize(idx + sizeof(T), 0xFF);
        std::memcpy(&data[idx], &t, sizeof(T));
        return t;
    }
    uint8_t &operator[](int idx) {
        if (idx >= (int)data.size()) data.resize(idx + 1, 0xFF);
        return data[idx];
    }
};

extern EEPROMClass EEPROM;
