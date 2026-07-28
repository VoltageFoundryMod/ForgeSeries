#pragma once
// Persistent user settings (hardware only) — stored in the RP2040's flash-backed
// EEPROM emulation. Writes are debounced: a change is committed only after the
// settings have been stable for SETTINGS_SAVE_DELAY_MS, so scrolling through
// modes or riding a knob doesn't wear the flash sector (one erase per settle,
// not per detent).
//
// Include AFTER scope.hpp (this reads/writes its globals). The VCV plugin does
// not use this — Rack persists module state through its own JSON patch.
//
// Call ScopeSettingsInit() once after ScopeInit(), and ScopeSettingsPoll() from
// the Core 0 loop. EEPROM.commit() briefly stalls both cores (flash erase
// suspends XIP; the core idles Core 1 internally), which is fine 5 s after the
// last interaction.

#include <EEPROM.h>
#include <cstring>

#define SETTINGS_SAVE_DELAY_MS 5000
#define SETTINGS_MAGIC 0x46565332u // "FVS2" — bump when the blob layout changes

struct ScopeSettingsBlob {
    uint32_t magic;
    int8_t menuMode;
    int8_t param1;
    int8_t param2;
    float scale;
    int8_t offset0;
    int8_t offset1;
    int8_t trigMode;
    int8_t showLabels;
    int8_t xyPersist;
    int8_t tunerChan;
    int8_t specChan;
    uint8_t checksum; // XOR of every byte before this field
};

static ScopeSettingsBlob _settingsSaved;   // last blob committed to flash
static ScopeSettingsBlob _settingsPending; // last blob observed in RAM
static unsigned long _settingsChangedMs = 0;

static uint8_t _SettingsChecksum(const ScopeSettingsBlob &b) {
    const uint8_t *p = (const uint8_t *)&b;
    uint8_t x = 0;
    for (size_t i = 0; i < offsetof(ScopeSettingsBlob, checksum); i++)
        x ^= p[i];
    return x;
}

// Snapshot the current scope globals. The struct is zeroed first so padding
// bytes compare deterministically with memcmp.
static void _SettingsCapture(ScopeSettingsBlob &b) {
    memset(&b, 0, sizeof(b));
    b.magic = SETTINGS_MAGIC;
    b.menuMode = (int8_t)menuMode;
    b.param1 = (int8_t)param1;
    b.param2 = (int8_t)param2;
    b.scale = scale;
    b.offset0 = (int8_t)offset0;
    b.offset1 = (int8_t)offset1;
    b.trigMode = (int8_t)trigMode;
    b.showLabels = showLabels ? 1 : 0;
    b.xyPersist = (int8_t)xyPersist;
    b.tunerChan = (int8_t)tunerChan;
    b.specChan = (int8_t)specChan;
    b.checksum = _SettingsChecksum(b);
}

// Restore a validated blob into the scope globals. Every field is clamped so a
// stale blob from an older firmware can never put the scope in an illegal state.
static void _SettingsApply(const ScopeSettingsBlob &b) {
    menuMode = constrain((int)b.menuMode, 1, SCOPE_MODE_COUNT);
    oldMenuMode = menuMode;
    ScopeApplyModeDefaults(); // per-mode transient resets (capture indices etc.)
    param1 = constrain((int)b.param1, 1, ScopeParam1Max());
    param2 = constrain((int)b.param2, 1, 8);
    scale = constrain(b.scale, 0.05f, 20.0f);
    offset0 = constrain((int)b.offset0, -24, 24);
    offset1 = constrain((int)b.offset1, -24, 24);
    trigMode = constrain((int)b.trigMode, 0, 3);
    showLabels = (b.showLabels != 0);
    xyPersist = constrain((int)b.xyPersist, 0, XY_PERSIST_COUNT - 1);
    tunerChan = constrain((int)b.tunerChan, 1, 2);
    specChan = constrain((int)b.specChan, 1, 2);
}

// Load saved settings (if any) into the scope. Call once after ScopeInit().
void ScopeSettingsInit() {
    EEPROM.begin(256);
    ScopeSettingsBlob b;
    EEPROM.get(0, b);
    if (b.magic == SETTINGS_MAGIC && b.checksum == _SettingsChecksum(b))
        _SettingsApply(b);
    // else: first boot / layout change — keep ScopeInit() defaults.
    _SettingsCapture(_settingsSaved); // post-apply state counts as "saved"
    _settingsPending = _settingsSaved;
}

// Debounced save. Call from the Core 0 loop; cheap when nothing changed.
void ScopeSettingsPoll() {
    static unsigned long lastCheckMs = 0;
    unsigned long now = millis();
    if (now - lastCheckMs < 200)
        return;
    lastCheckMs = now;

    ScopeSettingsBlob cur;
    _SettingsCapture(cur);
    if (memcmp(&cur, &_settingsPending, sizeof(cur)) != 0) {
        // Still changing — restart the settle timer.
        _settingsPending = cur;
        _settingsChangedMs = now;
        return;
    }
    if (memcmp(&_settingsPending, &_settingsSaved, sizeof(cur)) != 0 &&
        (now - _settingsChangedMs) >= SETTINGS_SAVE_DELAY_MS) {
        EEPROM.put(0, _settingsPending);
        EEPROM.commit(); // sector erase+write; idles Core 1 while flash is busy
        _settingsSaved = _settingsPending;
        Serial.println("Settings saved.");
    }
}
