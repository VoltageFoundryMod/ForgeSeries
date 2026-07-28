#pragma once

// fsStore.hpp — persistent storage for the ForgeSeries platform.
//
// ── Why not the emulated EEPROM ─────────────────────────────────────────────
// arduino-pico emulates EEPROM in ONE 4096-byte flash sector, and its begin()
// silently clamps any larger request; past the clamp every get/put is a no-op
// that still reports success. ClockForge alone wanted 5776 bytes, which is how
// its calibration came to be permanently broken without any error (see the
// `clk: fix silently-broken calibration` commit).
//
// Four apps sharing one image cannot fit that sector at all, and every app
// writing its presets from offset 0 means switching apps silently overwrites
// the previous one's data. A filesystem removes both problems: each app owns a
// named file, sizes are independent, and adding a fifth app costs nothing.
//
// Flash is not scarce here — the four-app image is ~10 % of 2 MB — so the
// filesystem region is cheap. Set it in platformio.ini:
//
//     board_build.filesystem_size = 256k
//
// ── Layout ──────────────────────────────────────────────────────────────────
//     /cal.bin        CalibrationData, shared by every app
//     /boot           BootRecord: which app to start
//     /<app>.pre      one app's preset slots, written as a single blob
//
// ── Durability ──────────────────────────────────────────────────────────────
// LittleFS is power-fail safe for its own metadata, but a file rewritten in
// place is not atomic: losing power mid-write can leave a truncated file. Every
// write here therefore goes to a temporary file and is renamed over the target,
// and every read validates a magic and a length before believing the contents.
// A blob that fails either check reads as "absent", so the caller falls back to
// defaults exactly as it would on a fresh module.

#include <Arduino.h>
#include <LittleFS.h>

#include "calibrationData.hpp"

namespace forge {
namespace fs {

// Bumped only if the envelope below changes — not when a payload does. Payload
// versioning is the caller's business (apps already have VALID_MAGIC).
#define FORGE_FS_MAGIC 0x46534731UL // 'FSG1'

struct BlobHeader {
    uint32_t magic;
    uint32_t length; // payload bytes following this header
};

inline bool g_mounted = false;

// Mount the filesystem, formatting it once if it has never been used.
// Returns false if even the format fails, in which case every call below
// fails cleanly and apps run on defaults.
inline bool Begin() {
    if (g_mounted)
        return true;
    if (LittleFS.begin()) {
        g_mounted = true;
        return true;
    }
    Serial.println("LittleFS mount failed — formatting (first boot?)");
    if (!LittleFS.format() || !LittleFS.begin()) {
        Serial.println("LittleFS format FAILED — running without persistence");
        return false;
    }
    g_mounted = true;
    return true;
}

// ── Raw blob access ─────────────────────────────────────────────────────────
inline bool WriteBlob(const char *path, const void *data, size_t len) {
    if (!g_mounted)
        return false;

    // Write to a temp file, then rename: a rewrite in place that loses power
    // halfway leaves a truncated file, and rename is the atomic operation
    // LittleFS gives us.
    char tmp[40];
    snprintf(tmp, sizeof(tmp), "%s.t", path);

    File f = LittleFS.open(tmp, "w");
    if (!f)
        return false;

    BlobHeader h{FORGE_FS_MAGIC, (uint32_t)len};
    const bool ok = f.write((const uint8_t *)&h, sizeof(h)) == sizeof(h) &&
                    f.write((const uint8_t *)data, len) == len;
    f.close();

    if (!ok) {
        LittleFS.remove(tmp);
        return false;
    }
    LittleFS.remove(path); // rename() will not clobber an existing file
    return LittleFS.rename(tmp, path);
}

// Reads only if the blob is present, well-formed, and exactly `len` long.
// A size mismatch means the payload layout changed under us, so it is rejected
// rather than reinterpreted — the same reasoning as the preset VALID_MAGIC.
inline bool ReadBlob(const char *path, void *out, size_t len) {
    if (!g_mounted)
        return false;

    File f = LittleFS.open(path, "r");
    if (!f)
        return false;

    BlobHeader h{};
    bool ok = f.read((uint8_t *)&h, sizeof(h)) == (int)sizeof(h) &&
              h.magic == FORGE_FS_MAGIC && h.length == len;
    if (ok)
        ok = f.read((uint8_t *)out, len) == (int)len;
    f.close();
    return ok;
}

template <typename T> inline bool Write(const char *path, const T &v) {
    return WriteBlob(path, &v, sizeof(T));
}
template <typename T> inline bool Read(const char *path, T &v) {
    return ReadBlob(path, &v, sizeof(T));
}

// ── Calibration: one file, every app ────────────────────────────────────────
// Calibration describes the board's analog front and back ends, so it is shared
// rather than duplicated per app. This is what lets the shell calibrate once.
#define FORGE_CAL_PATH "/cal.bin"

inline bool SaveCalibrationFs(const CalibrationData &c) {
    return Write(FORGE_CAL_PATH, c);
}

// Returns stored calibration, or nominal coefficients when none is valid.
//
// The guards matter: an unwritten or truncated read leaves `c` holding stack
// garbage, and 0xFF-filled flash decodes to a non-zero `valid` flag with NaN
// coefficients — which would slip past a bare `if (!valid)` and make every CV
// reading NaN, i.e. inputs that look completely dead. NaN compares unequal to
// itself, which is how the finite check works.
inline CalibrationData LoadCalibrationFs() {
    CalibrationData c{};
    bool ok = Read(FORGE_CAL_PATH, c) && c.magic == CAL_MAGIC && c.valid;

    if (ok) {
        for (int i = 0; i < NUM_CV_INS && ok; i++)
            ok = (c.cvScale[i] == c.cvScale[i]) && (c.cvOffset[i] == c.cvOffset[i]);
        for (int i = 0; i < NUM_OUTPUTS && ok; i++)
            ok = (c.dacScale[i] == c.dacScale[i]) && (c.dacOffset[i] == c.dacOffset[i]);
    }

    if (!ok) {
        // Nominal: CV spans the jack's full range over MAXADC counts, and the
        // output correction is identity. valid=false keeps every consumer on
        // its uncalibrated path.
        for (int i = 0; i < NUM_CV_INS; i++) {
            c.cvScale[i] = 5000.0f / (float)MAXADC;
            c.cvOffset[i] = 0.0f;
        }
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            c.dacScale[i] = 1.0f;
            c.dacOffset[i] = 0.0f;
        }
        c.magic = CAL_MAGIC;
        c.valid = false;
    }
    return c;
}

// ── Boot record: which app to start ─────────────────────────────────────────
#define FORGE_BOOT_PATH "/boot"

struct BootRecord {
    uint32_t magic;   // FORGE_FS_MAGIC when written by us
    uint8_t appIndex;
    // Set when an app asks to return to the selector. The shell reboots to get
    // there rather than unwinding in place — apps attach interrupts, start
    // hardware timers and hand work to Core 1, and tearing that down live is a
    // far larger source of bugs than a one-second restart. This flag survives
    // the reset and is cleared as soon as the menu opens.
    uint8_t showMenu;
    uint8_t _pad[2];
};

inline bool SaveBootApp(uint8_t index, bool showMenu = false) {
    BootRecord b{FORGE_FS_MAGIC, index, (uint8_t)(showMenu ? 1 : 0), {0, 0}};
    return Write(FORGE_BOOT_PATH, b);
}

// True if a reboot-into-the-menu was requested. Reading it does not clear it;
// the shell rewrites the record once the menu is up.
inline bool BootMenuRequested() {
    BootRecord b{};
    return Read(FORGE_BOOT_PATH, b) && b.magic == FORGE_FS_MAGIC && b.showMenu;
}

// Returns the stored index, or `fallback` if nothing valid is stored.
// The caller still range-checks: an app can be removed from a build, and a
// stale index must not walk off the registry.
inline uint8_t LoadBootApp(uint8_t fallback) {
    BootRecord b{};
    if (!Read(FORGE_BOOT_PATH, b) || b.magic != FORGE_FS_MAGIC)
        return fallback;
    return b.appIndex;
}

// ── Per-app preset slots ────────────────────────────────────────────────────
// One file per slot, e.g. /clk3.pre.
//
// Per slot rather than one file holding an array, for two reasons: writing a
// slot stays a whole-file atomic replace (no seeking into a file that a partial
// write could tear), and no slot has to be read into RAM to update a
// neighbour — ClockForge's seven slots are 4 KB in total, which is not
// something to put on the stack.
//
// It also removes the slot-count ceiling entirely. NUM_SLOTS was pinned at 7 by
// the 4096-byte EEPROM sector; here each slot is an independent file, so an app
// can have as many as it likes.
inline void PresetPath(char *out, size_t n, const char *app, int slot) {
    snprintf(out, n, "/%s%d.pre", app, slot);
}

template <typename T>
inline bool SavePreset(const char *app, int slot, const T &v) {
    char path[32];
    PresetPath(path, sizeof(path), app, slot);
    return Write(path, v);
}

// False when the slot has never been written (or fails validation), which the
// caller turns into factory defaults.
template <typename T> inline bool LoadPreset(const char *app, int slot, T &v) {
    char path[32];
    PresetPath(path, sizeof(path), app, slot);
    return Read(path, v);
}

} // namespace fs
} // namespace forge
