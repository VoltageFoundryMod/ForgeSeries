#pragma once
// ============================================================
// menuHandlers.hpp — setter/getter functions and MENU_ITEMS[].
// Included once from src/main.cpp after all other headers.
// ============================================================
//
// ┌─────────────────────────────────────────────────────────┐
// │              MENU SYSTEM DEVELOPER GUIDE                │
// └─────────────────────────────────────────────────────────┘
//
// FILE CHAIN (in include order)
// ──────────────────────────────
//   menuDefinitions.hpp  — MenuItem struct, RowStyle/MenuItemType enums
//   menuHandlers.hpp     ← YOU ARE HERE — getters, setters, MENU_ITEMS[]
//   menuDisplay.hpp      — low-level draw primitives (MD_Row, MD_RenderGroup …)
//   menuRender.hpp       — HandleDisplay() and the keyboard (home) renderer
//
// HOW ITEMS AND GROUPS WORK
// ──────────────────────────
// • Every entry in MENU_ITEMS[] is a "menu item" numbered 1…N (1-based).
//   Item number  =  array index + 1.
// • The global  menuItem  tracks which item is selected.
// • The global  menuMode  is 0 when navigating, or equals the item number
//   currently being edited.
// • Items sharing a  group  render on the same page.  Turning the encoder walks
//   items in array order, so the page changes when you cross a group boundary.
// • MENU_ITEM_COUNT is computed automatically — there is no manual counter.
//
// GROUP MAP
// ──────────
//   0  KEYBOARD (home)  items  1–24  — 12 note toggles per channel
//   1  SCALES           items 25–30
//   2  CH1 PITCH        items 31–34
//   3  CH2 PITCH        items 35–38
//   4  CH1 GATE         items 39–43
//   5  CH2 GATE         items 44–48
//   6  ROUTING          items 49–52
//   7  SETTINGS         items 53–57
//
// Group 0 has a fully custom renderer (the two keyboards) in menuRender.hpp.
// Every other group is a plain list handled by the generic MD_RenderGroup(),
// so adding a row to one of them needs no rendering changes at all: write a
// getter + setter, append a MenuItem with that group, and add the page title to
// groupTitles[] in menuRender.hpp if the group is new.
//
// MENU_ITEMS[] COLUMN GUIDE
// ──────────────────────────
//   { label, valueFn, valueFn2, col1x, col2x, group, rowStyle, type, setter, action }
//
//   label    — left-side text (ignored for ROW_HIDDEN).
//   valueFn  — returns the displayed value; nullptr → no value shown.
//   col1x    — pixel X for valueFn.
//   rowStyle — ROW_SINGLE / ROW_ACTION / ROW_HIDDEN (see menuDefinitions.hpp).
//   type     — MENU_EDIT (rotate calls setter) / MENU_TOGGLE / MENU_ACTION.
//
// Per-channel handlers are templates parameterised on the channel index, so the
// two channels share one implementation instead of duplicated copy-paste pairs.

// ── Globals owned by src/main.cpp ────────────────────────────
extern QuantizerChannel channels[NUM_CHANNELS];
extern DisplayManager displayMgr;
extern bool unsavedChanges;
extern int menuMode;
extern int menuScreenTimeout;
extern void ShowTemporaryMessage(const char *msg, uint32_t durationMs);

// ── Shared helpers ───────────────────────────────────────────
static inline void MarkUnsaved() {
    unsavedChanges = true;
    displayMgr.SetUnsavedChanges(true);
}

// Reduce an encoder delta to a direction. Used where a single step per detent is
// the only sensible behaviour (enums, octaves) regardless of spin speed.
static inline int Dir(int delta) { return delta > 0 ? 1 : -1; }

// ── Scale / root ─────────────────────────────────────────────
template <int CH>
static String getScaleName() { return String(scaleNames[channels[CH].GetScaleIndex()]); }

template <int CH>
static void setScale(int d) {
    channels[CH].SetScaleIndex(channels[CH].GetScaleIndex() + Dir(d));
    MarkUnsaved();
}

template <int CH>
static String getRootName() { return String(noteNames[channels[CH].GetRootIndex()]); }

template <int CH>
static void setRoot(int d) {
    channels[CH].SetRootIndex(channels[CH].GetRootIndex() + Dir(d));
    MarkUnsaved();
}

template <int CH>
static void loadScaleIntoChannel() {
    channels[CH].LoadSelectedScale();
    MarkUnsaved();
    ShowTemporaryMessage("LOADED", 700);
}

// ── Pitch ────────────────────────────────────────────────────
template <int CH>
static String getOctave() {
    int o = channels[CH].GetOctave();
    return (o > 0) ? "+" + String(o) : String(o);
}

template <int CH>
static void setOctave(int d) {
    channels[CH].SetOctave(channels[CH].GetOctave() + Dir(d));
    MarkUnsaved();
}

template <int CH>
static String getPitchMode() { return String(channels[CH].GetPitchModeName()); }

template <int CH>
static void setPitchMode(int d) {
    channels[CH].SetPitchMode(channels[CH].GetPitchMode() + Dir(d));
    MarkUnsaved();
}

// Settle replaces the original firmware's input sensitivity trim. That trim was
// a gain on the incoming pitch CV, which is exactly the wrong tool now that the
// calibration wizard fits the input scale properly: any setting other than
// unity detunes the module and makes it quantize to the wrong note.
template <int CH>
static String getSettle() { return String(channels[CH].GetSettle()) + "ms"; }

template <int CH>
static void setSettle(int d) {
    channels[CH].SetSettle(channels[CH].GetSettle() + d);
    MarkUnsaved();
}

template <int CH>
static String getGlide() { return String(channels[CH].GetGlide()) + "%"; }

template <int CH>
static void setGlide(int d) {
    channels[CH].SetGlide(channels[CH].GetGlide() + d);
    MarkUnsaved();
}

// ── Gate / envelope ──────────────────────────────────────────
template <int CH>
static String getGateMode() { return String(channels[CH].envelope.GetModeName()); }

template <int CH>
static void setGateMode(int d) {
    channels[CH].envelope.SetMode(channels[CH].envelope.GetMode() + Dir(d));
    MarkUnsaved();
}

template <int CH>
static String getAttack() { return String(channels[CH].envelope.GetAttack()) + "ms"; }

// Attack/decay move in 5 ms steps so the full 0–2 s / 0–4 s ranges stay
// reachable in a reasonable number of turns once the encoder's speed factor
// multiplies the delta.
template <int CH>
static void setAttack(int d) {
    channels[CH].envelope.SetAttack(channels[CH].envelope.GetAttack() + d * 5);
    MarkUnsaved();
}

template <int CH>
static String getDecay() { return String(channels[CH].envelope.GetDecay()) + "ms"; }

template <int CH>
static void setDecay(int d) {
    channels[CH].envelope.SetDecay(channels[CH].envelope.GetDecay() + d * 5);
    MarkUnsaved();
}

template <int CH>
static String getSyncMode() { return String(channels[CH].GetSyncModeName()); }

template <int CH>
static void setSyncMode(int d) {
    channels[CH].SetSyncMode(channels[CH].GetSyncMode() + Dir(d));
    MarkUnsaved();
}

template <int CH>
static String getGateLevel() { return String(channels[CH].envelope.GetLevel()) + "%"; }

template <int CH>
static void setGateLevel(int d) {
    channels[CH].envelope.SetLevel(channels[CH].envelope.GetLevel() + d);
    MarkUnsaved();
}

// ── Keyboard note toggles (group 0) ──────────────────────────
template <int CH, int NOTE>
static void toggleNote() {
    channels[CH].ToggleNote(NOTE);
    MarkUnsaved();
}

// ── Input routing / transposition ──────────────────
// Handing IN 2 to a transposition CV costs channel 2 its pitch input, so the
// switch is explicit. In return channel 2 quantizes IN 1 alongside channel 1 —
// two voicings of the same melody, transposed together.
static String getIn2Role() { return String(In2RoleNames[constrain((int)in2Role, 0, (int)In2RoleLength - 1)]); }

static void setIn2Role(int d) {
    in2Role = (uint8_t)constrain((int)in2Role + Dir(d), 0, (int)In2RoleLength - 1);
    MarkUnsaved();
}

static String getTransposeRange() {
    return String(TransposeRangeNames[constrain((int)transposeRange, 0, (int)TransposeRangeLength - 1)]);
}

static void setTransposeRange(int d) {
    transposeRange = (uint8_t)constrain((int)transposeRange + Dir(d), 0, (int)TransposeRangeLength - 1);
    MarkUnsaved();
}

template <int CH>
static String getTransposeEnabled() { return channels[CH].GetTransposeEnabled() ? String("ON") : String("OFF"); }

template <int CH>
static void toggleTransposeEnabled() {
    channels[CH].SetTransposeEnabled(!channels[CH].GetTransposeEnabled());
    MarkUnsaved();
}

// ── Settings ─────────────────────────────────────────────────
static const char *screenTimeoutOptions[] = {"Off", "2s", "5s", "10s", "20s"};
static const int screenTimeoutCount = 5;

static String getTimeout() { return String(screenTimeoutOptions[constrain(menuScreenTimeout, 0, screenTimeoutCount - 1)]); }

static void setTimeout(int d) {
    menuScreenTimeout = constrain(menuScreenTimeout + Dir(d), 0, screenTimeoutCount - 1);
    static const unsigned long kTimeoutOpts[] = {0, 2000, 5000, 10000, 20000};
    displayMgr.SetMenuTimeout(kTimeoutOpts[menuScreenTimeout]);
    MarkUnsaved();
}

static String getSaveSlot() { return String(saveSlot); }

static void setSaveSlot(int d) {
    saveSlot = constrain(saveSlot + Dir(d), 0, NUM_SLOTS - 1);
}

static void doSave() {
    Save(CollectParams(), saveSlot);
    unsavedChanges = false;
    displayMgr.SetUnsavedChanges(false);
    ShowTemporaryMessage("SAVED", 900);
}

static void doLoad() {
    UpdateParameters(Load(saveSlot));
    unsavedChanges = false;
    displayMgr.SetUnsavedChanges(false);
    ShowTemporaryMessage("LOADED", 900);
}

// Defaults are applied to the live state but not written to flash — the user
// still has to Save, so a mis-click is recoverable by loading the slot again.
static void doLoadDefaults() {
    UpdateParameters(LoadDefaultParams());
    MarkUnsaved();
    ShowTemporaryMessage("DEFAULTS", 900);
}

// ─────────────────────────────────────────────────────────────
// THE MENU TABLE
// ─────────────────────────────────────────────────────────────
const MenuItem MENU_ITEMS[] = {
    // ── Group 0: keyboard (home). One item per note, per channel. ──────────
    // Rendered entirely by the custom keyboard renderer, hence ROW_HIDDEN.
    {"C  1", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<0, 0>},   // 1
    {"C# 1", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<0, 1>},   // 2
    {"D  1", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<0, 2>},   // 3
    {"D# 1", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<0, 3>},   // 4
    {"E  1", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<0, 4>},   // 5
    {"F  1", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<0, 5>},   // 6
    {"F# 1", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<0, 6>},   // 7
    {"G  1", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<0, 7>},   // 8
    {"G# 1", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<0, 8>},   // 9
    {"A  1", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<0, 9>},   // 10
    {"A# 1", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<0, 10>},  // 11
    {"B  1", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<0, 11>},  // 12
    {"C  2", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<1, 0>},   // 13
    {"C# 2", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<1, 1>},   // 14
    {"D  2", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<1, 2>},   // 15
    {"D# 2", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<1, 3>},   // 16
    {"E  2", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<1, 4>},   // 17
    {"F  2", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<1, 5>},   // 18
    {"F# 2", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<1, 6>},   // 19
    {"G  2", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<1, 7>},   // 20
    {"G# 2", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<1, 8>},   // 21
    {"A  2", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<1, 9>},   // 22
    {"A# 2", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<1, 10>},  // 23
    {"B  2", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleNote<1, 11>},  // 24

    // ── Group 1: scales ────────────────────────────────────────────────────
    {"CH1 SCALE:", getScaleName<0>, nullptr, 76, 0, 1, ROW_SINGLE, MENU_EDIT, setScale<0>, nullptr}, // 25
    {"CH1 ROOT:", getRootName<0>, nullptr, 76, 0, 1, ROW_SINGLE, MENU_EDIT, setRoot<0>, nullptr},    // 26
    {"LOAD INTO CH1", nullptr, nullptr, 0, 0, 1, ROW_ACTION, MENU_ACTION, nullptr, loadScaleIntoChannel<0>}, // 27
    {"CH2 SCALE:", getScaleName<1>, nullptr, 76, 0, 1, ROW_SINGLE, MENU_EDIT, setScale<1>, nullptr}, // 28
    {"CH2 ROOT:", getRootName<1>, nullptr, 76, 0, 1, ROW_SINGLE, MENU_EDIT, setRoot<1>, nullptr},    // 29
    {"LOAD INTO CH2", nullptr, nullptr, 0, 0, 1, ROW_ACTION, MENU_ACTION, nullptr, loadScaleIntoChannel<1>}, // 30

    // ── Group 2: channel 1 pitch ────────────────────
    {"MODE:", getPitchMode<0>, nullptr, 76, 0, 2, ROW_SINGLE, MENU_EDIT, setPitchMode<0>, nullptr}, // 31
    {"OCTAVE:", getOctave<0>, nullptr, 76, 0, 2, ROW_SINGLE, MENU_EDIT, setOctave<0>, nullptr},     // 32
    {"SETTLE:", getSettle<0>, nullptr, 76, 0, 2, ROW_SINGLE, MENU_EDIT, setSettle<0>, nullptr},     // 33
    {"GLIDE:", getGlide<0>, nullptr, 76, 0, 2, ROW_SINGLE, MENU_EDIT, setGlide<0>, nullptr},        // 34

    // ── Group 3: channel 2 pitch ────────────────────
    {"MODE:", getPitchMode<1>, nullptr, 76, 0, 3, ROW_SINGLE, MENU_EDIT, setPitchMode<1>, nullptr}, // 35
    {"OCTAVE:", getOctave<1>, nullptr, 76, 0, 3, ROW_SINGLE, MENU_EDIT, setOctave<1>, nullptr},     // 36
    {"SETTLE:", getSettle<1>, nullptr, 76, 0, 3, ROW_SINGLE, MENU_EDIT, setSettle<1>, nullptr},     // 37
    {"GLIDE:", getGlide<1>, nullptr, 76, 0, 3, ROW_SINGLE, MENU_EDIT, setGlide<1>, nullptr},        // 38

    // ── Group 4: channel 1 gate/envelope ─────────────
    {"MODE:", getGateMode<0>, nullptr, 76, 0, 4, ROW_SINGLE, MENU_EDIT, setGateMode<0>, nullptr},    // 39
    {"ATTACK:", getAttack<0>, nullptr, 76, 0, 4, ROW_SINGLE, MENU_EDIT, setAttack<0>, nullptr},      // 40
    {"DECAY:", getDecay<0>, nullptr, 76, 0, 4, ROW_SINGLE, MENU_EDIT, setDecay<0>, nullptr},         // 41
    {"SYNC:", getSyncMode<0>, nullptr, 76, 0, 4, ROW_SINGLE, MENU_EDIT, setSyncMode<0>, nullptr},    // 42
    {"LEVEL:", getGateLevel<0>, nullptr, 76, 0, 4, ROW_SINGLE, MENU_EDIT, setGateLevel<0>, nullptr}, // 43

    // ── Group 5: channel 2 gate/envelope ─────────────
    {"MODE:", getGateMode<1>, nullptr, 76, 0, 5, ROW_SINGLE, MENU_EDIT, setGateMode<1>, nullptr},    // 44
    {"ATTACK:", getAttack<1>, nullptr, 76, 0, 5, ROW_SINGLE, MENU_EDIT, setAttack<1>, nullptr},      // 45
    {"DECAY:", getDecay<1>, nullptr, 76, 0, 5, ROW_SINGLE, MENU_EDIT, setDecay<1>, nullptr},         // 46
    {"SYNC:", getSyncMode<1>, nullptr, 76, 0, 5, ROW_SINGLE, MENU_EDIT, setSyncMode<1>, nullptr},    // 47
    {"LEVEL:", getGateLevel<1>, nullptr, 76, 0, 5, ROW_SINGLE, MENU_EDIT, setGateLevel<1>, nullptr}, // 48

    // ── Group 6: input routing / transposition ───────
    {"IN2 ROLE:", getIn2Role, nullptr, 76, 0, 6, ROW_SINGLE, MENU_EDIT, setIn2Role, nullptr},               // 49
    {"TR RANGE:", getTransposeRange, nullptr, 76, 0, 6, ROW_SINGLE, MENU_EDIT, setTransposeRange, nullptr}, // 50
    {"CH1 TRANSP:", getTransposeEnabled<0>, nullptr, 88, 0, 6, ROW_SINGLE, MENU_TOGGLE, nullptr, toggleTransposeEnabled<0>}, // 51
    {"CH2 TRANSP:", getTransposeEnabled<1>, nullptr, 88, 0, 6, ROW_SINGLE, MENU_TOGGLE, nullptr, toggleTransposeEnabled<1>}, // 52

    // ── Group 7: settings ──────────────────────────
    {"SCR TIMEOUT:", getTimeout, nullptr, 88, 0, 7, ROW_SINGLE, MENU_EDIT, setTimeout, nullptr},    // 53
    {"PRESET SLOT:", getSaveSlot, nullptr, 88, 0, 7, ROW_SINGLE, MENU_EDIT, setSaveSlot, nullptr},  // 54
    {"SAVE", nullptr, nullptr, 0, 0, 7, ROW_ACTION, MENU_ACTION, nullptr, doSave},                  // 55
    {"LOAD", nullptr, nullptr, 0, 0, 7, ROW_ACTION, MENU_ACTION, nullptr, doLoad},                  // 56
    {"LOAD DEFAULTS", nullptr, nullptr, 0, 0, 7, ROW_ACTION, MENU_ACTION, nullptr, doLoadDefaults}, // 57
};

const int MENU_ITEM_COUNT = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
