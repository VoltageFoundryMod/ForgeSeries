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
//   menuRender.hpp       — HandleDisplay() and the physics home screen
//
// HOW ITEMS AND GROUPS WORK
// ──────────────────────────
// • Every entry in MENU_ITEMS[] is a "menu item" numbered 1…N (1-based).
//   Item number = array index + 1.
// • The global  menuItem  tracks which item is selected.
// • The global  menuMode  is 0 when navigating, or equals the item number
//   currently being edited.
// • Items sharing a  group  render on the same page. Turning the encoder walks
//   items in array order, so the page changes when you cross a group boundary.
// • MENU_ITEM_COUNT is computed automatically — there is no manual counter.
//
// GROUP MAP
// ──────────
//   0  HOME       — the physics view (custom renderer in menuRender.hpp)
//   1  CLOCK      — bpm, ppqn, quantize grid, IN 1 role
//   2  COUPLING   — proximity, couple amount, reset/kick
//   3  A PHYSICS  — gravity, bounce, grip, spin, reverse, balls
//   4  B PHYSICS
//   5  A NOTES    — scale, root, spread, bias, peg count
//   6  B NOTES
//   7  A GATE     — mode, attack, decay, level, accent
//   8  B GATE
//   9  CV         — IN 2 / IN 3 target + depth
//  10  SETTINGS   — preset slot, save, load, screen timeout
//
// PAGE LENGTH LIMIT: six rows. MD_START_Y=12 with MD_ROW_H=9 puts row 6 at
// y=57, whose glyphs end on row 63 — exactly the bottom of the screen. A
// seventh row is silently clipped.
//
// Per-container handlers are templates parameterised on the index, so the two
// containers share one implementation instead of duplicated copy-paste pairs.

// ── Globals owned by src/main.cpp ────────────────────────────
extern GravityChannel channels[NUM_CHANNELS];
extern ContainerParams containerParams[2];
extern WorldParams worldParams;
extern PhysicsWorld physicsWorld;
extern Clock clockEngine;
extern DisplayManager displayMgr;
extern bool unsavedChanges;
extern bool displayRefresh; // REQUEST_DISPLAY_REFRESH() writes it
extern int menuMode;
extern int menuScreenTimeout;
extern void ShowTemporaryMessage(const char *msg, uint32_t durationMs);

// ── Shared helpers ───────────────────────────────────────────
static inline void MarkUnsaved() {
    unsavedChanges = true;
    displayMgr.SetUnsavedChanges(true);
}

// Reduce an encoder delta to a direction. Used where a single step per detent is
// the only sensible behaviour (enums, toggles) regardless of spin speed.
static inline int Dir(int delta) { return delta > 0 ? 1 : -1; }

static inline String Pct(float v01) { return String((int)lroundf(v01 * 100.0f)) + "%"; }

// ── CLOCK ────────────────────────────────────────────────────
static String getBpm() {
    // The "e" means a clock is genuinely coming in — IsExternalLive(), not
    // IsExternal(). Keying it off the latter would mark the module as externally
    // clocked from the moment it boots, since CLOCK is IN 1's default role, and
    // the badge would never tell you anything.
    if (clockEngine.IsExternalLive()) {
        return String((int)lroundf(clockEngine.GetEffectiveBpm())) + "E";
    }
    return String(clockEngine.GetBpm());
}
static void setBpm(int d) {
    clockEngine.SetBpm(clockEngine.GetBpm() + d);
    MarkUnsaved();
}

static String getPpqn() { return String(ClockPPQNNames[clockEngine.GetPpqn()]); }
static void setPpqn(int d) {
    clockEngine.SetPpqn(clockEngine.GetPpqn() + Dir(d));
    MarkUnsaved();
}

static String getQuantize() { return String(QuantizeDivNames[clockEngine.GetQuantize()]); }
static void setQuantize(int d) {
    clockEngine.SetQuantize(clockEngine.GetQuantize() + Dir(d));
    MarkUnsaved();
}

static String getIn1Role() { return String(In1RoleNames[in1Role]); }
static void setIn1Role(int d) {
    in1Role = (uint8_t)constrain((int)in1Role + Dir(d), 0, (int)In1RoleLength - 1);
    // The clock only follows the jack while the jack is a clock; anything else
    // has to hand tempo back to the internal setting or the containers would
    // keep turning at a tempo nothing is driving any more.
    clockEngine.SetExternal(in1Role == In1Clock);
    MarkUnsaved();
}

// ── COUPLING ─────────────────────────────────────────────────
static String getProximity() { return Pct(worldParams.proximity); }
static void setProximity(int d) {
    worldParams.proximity = constrain(worldParams.proximity + (float)d * 0.01f, 0.0f, 1.0f);
    MarkUnsaved();
}

static String getCoupling() { return Pct(worldParams.coupling); }
static void setCoupling(int d) {
    worldParams.coupling = constrain(worldParams.coupling + (float)d * 0.01f, 0.0f, 1.0f);
    MarkUnsaved();
}

static void actReset() {
    physicsWorld.Reset();
    ShowTemporaryMessage("RESET", 500);
}
static void actKick() {
    physicsWorld.Kick(180.0f);
    ShowTemporaryMessage("KICK", 400);
}

// ── PHYSICS (per container) ──────────────────────────────────
template <int C>
static String getGravity() { return String((int)lroundf(containerParams[C].gravity)); }
template <int C>
static void setGravity(int d) {
    containerParams[C].gravity = constrain(containerParams[C].gravity + (float)d * 5.0f,
                                           PARAM_GRAVITY_MIN, PARAM_GRAVITY_MAX);
    MarkUnsaved();
}

template <int C>
static String getBounce() { return Pct(containerParams[C].bounce); }
template <int C>
static void setBounce(int d) {
    containerParams[C].bounce = constrain(containerParams[C].bounce + (float)d * 0.01f,
                                          PARAM_BOUNCE_MIN, PARAM_BOUNCE_MAX);
    MarkUnsaved();
}

template <int C>
static String getGrip() { return Pct(containerParams[C].grip); }
template <int C>
static void setGrip(int d) {
    containerParams[C].grip = constrain(containerParams[C].grip + (float)d * 0.02f,
                                        PARAM_GRIP_MIN, PARAM_GRIP_MAX);
    MarkUnsaved();
}

template <int C>
static String getSpin() { return String(SpinRateNames[containerParams[C].spin]); }
template <int C>
static void setSpin(int d) {
    // Clamped to the clock-locked ratios. SpinFree exists in the enum and is
    // honoured by Clock::OmegaFor(), but it needs a rate control to be useful
    // and there is no seventh row free on this page — it is reachable from the
    // Rack context menu when the VCV port lands.
    containerParams[C].spin =
        (uint8_t)constrain((int)containerParams[C].spin + Dir(d), 0, (int)Spin16);
    MarkUnsaved();
}

template <int C>
static String getReverse() { return containerParams[C].reverse ? String("REV") : String("FWD"); }
template <int C>
static void actReverse() {
    containerParams[C].reverse = !containerParams[C].reverse;
    MarkUnsaved();
}

template <int C>
static String getBalls() { return String(containerParams[C].balls); }
template <int C>
static void setBalls(int d) {
    containerParams[C].balls =
        (uint8_t)constrain((int)containerParams[C].balls + Dir(d), PHYS_MIN_BALLS, PHYS_MAX_BALLS);
    MarkUnsaved();
}

// ── NOTES (per container) ────────────────────────────────────
template <int C>
static String getScaleName() { return String(scaleNames[channels[C].GetScaleIndex()]); }
template <int C>
static void setScale(int d) {
    channels[C].SelectScale(channels[C].GetScaleIndex() + Dir(d));
    MarkUnsaved();
}

template <int C>
static String getRootName() { return String(noteNames[channels[C].GetRootIndex()]); }
template <int C>
static void setRoot(int d) {
    channels[C].SelectRoot(channels[C].GetRootIndex() + Dir(d));
    MarkUnsaved();
}

// SPREAD is how many octaves the peg ring covers; BIAS is where the notes
// crowd inside it. Together they replace the old single OCTAVE offset.
template <int C>
static String getSpread() { return String(channels[C].GetSpread()) + "oct"; }
template <int C>
static void setSpread(int d) {
    channels[C].SetSpread(channels[C].GetSpread() + Dir(d));
    MarkUnsaved();
}

// Shown as LOW/EVEN/HIGH with the amount, because a bare signed percentage does
// not say which way it leans.
template <int C>
static String getBias() {
    int b = channels[C].GetBias();
    if (b == 0) {
        return String("EVEN");
    }
    return String(b < 0 ? "LO" : "HI") + String(b < 0 ? -b : b);
}
template <int C>
static void setBias(int d) {
    channels[C].SetBias(channels[C].GetBias() + d * 5);
    MarkUnsaved();
}

template <int C>
static String getPegs() { return String(containerParams[C].pegs); }
template <int C>
static void setPegs(int d) {
    containerParams[C].pegs =
        (uint8_t)constrain((int)containerParams[C].pegs + Dir(d), PHYS_MIN_PEGS, PHYS_MAX_PEGS);
    MarkUnsaved();
}

// ── GATE (per container) ─────────────────────────────────────
template <int C>
static String getGateMode() { return String(channels[C].envelope.GetModeName()); }
template <int C>
static void setGateMode(int d) {
    channels[C].envelope.SetMode(channels[C].envelope.GetMode() + Dir(d));
    MarkUnsaved();
}

template <int C>
static String getAttack() { return String(channels[C].envelope.GetAttack()); }
template <int C>
static void setAttack(int d) {
    channels[C].envelope.SetAttack(channels[C].envelope.GetAttack() + d * 5);
    MarkUnsaved();
}

template <int C>
static String getDecay() { return String(channels[C].envelope.GetDecay()); }
template <int C>
static void setDecay(int d) {
    channels[C].envelope.SetDecay(channels[C].envelope.GetDecay() + d * 10);
    MarkUnsaved();
}

template <int C>
static String getLevel() { return String(channels[C].GetGateLevel()) + "%"; }
template <int C>
static void setLevel(int d) {
    channels[C].SetGateLevel(channels[C].GetGateLevel() + d);
    MarkUnsaved();
}

template <int C>
static String getAccent() { return String(channels[C].GetAccent()) + "%"; }
template <int C>
static void setAccent(int d) {
    channels[C].SetAccent(channels[C].GetAccent() + d);
    MarkUnsaved();
}

// ── CV ───────────────────────────────────────────────────────
template <int I>
static String getCvTarget() { return String(CVTargetNames[cvTarget[I]]); }
template <int I>
static void setCvTarget(int d) {
    cvTarget[I] = (uint8_t)constrain((int)cvTarget[I] + Dir(d), 0, (int)CVTargetLength - 1);
    MarkUnsaved();
}

template <int I>
static String getCvDepth() { return String(cvDepth[I]) + "%"; }
template <int I>
static void setCvDepth(int d) {
    cvDepth[I] = (uint8_t)constrain((int)cvDepth[I] + d, 0, 100);
    MarkUnsaved();
}

// ── SETTINGS ─────────────────────────────────────────────────
static String getSlot() { return String(saveSlot); }
static void setSlot(int d) {
    saveSlot = constrain(saveSlot + Dir(d), 0, NUM_SLOTS - 1);
    REQUEST_DISPLAY_REFRESH();
}

static void actSave() {
    Save(CollectParams(), saveSlot);
    unsavedChanges = false;
    displayMgr.SetUnsavedChanges(false);
    ShowTemporaryMessage("SAVED", 700);
}

static void actLoad() {
    UpdateParameters(Load(saveSlot));
    unsavedChanges = false;
    displayMgr.SetUnsavedChanges(false);
    ShowTemporaryMessage("LOADED", 700);
}

static const char *const screenTimeoutNames[] = {"OFF", "2s", "5s", "10s", "20s"};
static String getTimeout() { return String(screenTimeoutNames[constrain(menuScreenTimeout, 0, 4)]); }
static void setTimeout(int d) {
    menuScreenTimeout = constrain(menuScreenTimeout + Dir(d), 0, 4);
    static const unsigned long kTimeoutOpts[] = {0, 2000, 5000, 10000, 20000};
    displayMgr.SetMenuTimeout(kTimeoutOpts[menuScreenTimeout]);
    MarkUnsaved();
}

// ─────────────────────────────────────────────────────────────
// MENU_ITEMS[]
// { label, valueFn, valueFn2, col1x, col2x, group, rowStyle, type, setter, action }
// ─────────────────────────────────────────────────────────────
const MenuItem MENU_ITEMS[] = {
    // ── 0 HOME ──
    {"HOME", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_ACTION, nullptr, nullptr},

    // ── 1 CLOCK ──
    {"BPM", getBpm, nullptr, 76, 0, 1, ROW_SINGLE, MENU_EDIT, setBpm, nullptr},
    {"CLK DIV", getPpqn, nullptr, 76, 0, 1, ROW_SINGLE, MENU_EDIT, setPpqn, nullptr},
    {"QUANTIZE", getQuantize, nullptr, 76, 0, 1, ROW_SINGLE, MENU_EDIT, setQuantize, nullptr},
    {"IN 1", getIn1Role, nullptr, 76, 0, 1, ROW_SINGLE, MENU_EDIT, setIn1Role, nullptr},

    // ── 2 COUPLING ──
    {"PROXIMITY", getProximity, nullptr, 82, 0, 2, ROW_SINGLE, MENU_EDIT, setProximity, nullptr},
    {"COUPLE", getCoupling, nullptr, 82, 0, 2, ROW_SINGLE, MENU_EDIT, setCoupling, nullptr},
    {"RESET BALLS", nullptr, nullptr, 0, 0, 2, ROW_ACTION, MENU_ACTION, nullptr, actReset},
    {"KICK", nullptr, nullptr, 0, 0, 2, ROW_ACTION, MENU_ACTION, nullptr, actKick},

    // ── 3 A PHYSICS ──
    {"GRAVITY", getGravity<0>, nullptr, 76, 0, 3, ROW_SINGLE, MENU_EDIT, setGravity<0>, nullptr},
    {"BOUNCE", getBounce<0>, nullptr, 76, 0, 3, ROW_SINGLE, MENU_EDIT, setBounce<0>, nullptr},
    {"GRIP", getGrip<0>, nullptr, 76, 0, 3, ROW_SINGLE, MENU_EDIT, setGrip<0>, nullptr},
    {"SPIN", getSpin<0>, nullptr, 76, 0, 3, ROW_SINGLE, MENU_EDIT, setSpin<0>, nullptr},
    {"DIR", getReverse<0>, nullptr, 76, 0, 3, ROW_SINGLE, MENU_TOGGLE, nullptr, actReverse<0>},
    {"BALLS", getBalls<0>, nullptr, 76, 0, 3, ROW_SINGLE, MENU_EDIT, setBalls<0>, nullptr},

    // ── 4 B PHYSICS ──
    {"GRAVITY", getGravity<1>, nullptr, 76, 0, 4, ROW_SINGLE, MENU_EDIT, setGravity<1>, nullptr},
    {"BOUNCE", getBounce<1>, nullptr, 76, 0, 4, ROW_SINGLE, MENU_EDIT, setBounce<1>, nullptr},
    {"GRIP", getGrip<1>, nullptr, 76, 0, 4, ROW_SINGLE, MENU_EDIT, setGrip<1>, nullptr},
    {"SPIN", getSpin<1>, nullptr, 76, 0, 4, ROW_SINGLE, MENU_EDIT, setSpin<1>, nullptr},
    {"DIR", getReverse<1>, nullptr, 76, 0, 4, ROW_SINGLE, MENU_TOGGLE, nullptr, actReverse<1>},
    {"BALLS", getBalls<1>, nullptr, 76, 0, 4, ROW_SINGLE, MENU_EDIT, setBalls<1>, nullptr},

    // ── 5 A NOTES ──
    {"SCALE", getScaleName<0>, nullptr, 76, 0, 5, ROW_SINGLE, MENU_EDIT, setScale<0>, nullptr},
    {"ROOT", getRootName<0>, nullptr, 76, 0, 5, ROW_SINGLE, MENU_EDIT, setRoot<0>, nullptr},
    {"SPREAD", getSpread<0>, nullptr, 76, 0, 5, ROW_SINGLE, MENU_EDIT, setSpread<0>, nullptr},
    {"BIAS", getBias<0>, nullptr, 76, 0, 5, ROW_SINGLE, MENU_EDIT, setBias<0>, nullptr},
    {"PEGS", getPegs<0>, nullptr, 76, 0, 5, ROW_SINGLE, MENU_EDIT, setPegs<0>, nullptr},

    // ── 6 B NOTES ──
    {"SCALE", getScaleName<1>, nullptr, 76, 0, 6, ROW_SINGLE, MENU_EDIT, setScale<1>, nullptr},
    {"ROOT", getRootName<1>, nullptr, 76, 0, 6, ROW_SINGLE, MENU_EDIT, setRoot<1>, nullptr},
    {"SPREAD", getSpread<1>, nullptr, 76, 0, 6, ROW_SINGLE, MENU_EDIT, setSpread<1>, nullptr},
    {"BIAS", getBias<1>, nullptr, 76, 0, 6, ROW_SINGLE, MENU_EDIT, setBias<1>, nullptr},
    {"PEGS", getPegs<1>, nullptr, 76, 0, 6, ROW_SINGLE, MENU_EDIT, setPegs<1>, nullptr},

    // ── 7 A GATE ──
    {"MODE", getGateMode<0>, nullptr, 76, 0, 7, ROW_SINGLE, MENU_EDIT, setGateMode<0>, nullptr},
    {"ATTACK", getAttack<0>, nullptr, 76, 0, 7, ROW_SINGLE, MENU_EDIT, setAttack<0>, nullptr},
    {"DECAY", getDecay<0>, nullptr, 76, 0, 7, ROW_SINGLE, MENU_EDIT, setDecay<0>, nullptr},
    {"LEVEL", getLevel<0>, nullptr, 76, 0, 7, ROW_SINGLE, MENU_EDIT, setLevel<0>, nullptr},
    {"ACCENT", getAccent<0>, nullptr, 76, 0, 7, ROW_SINGLE, MENU_EDIT, setAccent<0>, nullptr},

    // ── 8 B GATE ──
    {"MODE", getGateMode<1>, nullptr, 76, 0, 8, ROW_SINGLE, MENU_EDIT, setGateMode<1>, nullptr},
    {"ATTACK", getAttack<1>, nullptr, 76, 0, 8, ROW_SINGLE, MENU_EDIT, setAttack<1>, nullptr},
    {"DECAY", getDecay<1>, nullptr, 76, 0, 8, ROW_SINGLE, MENU_EDIT, setDecay<1>, nullptr},
    {"LEVEL", getLevel<1>, nullptr, 76, 0, 8, ROW_SINGLE, MENU_EDIT, setLevel<1>, nullptr},
    {"ACCENT", getAccent<1>, nullptr, 76, 0, 8, ROW_SINGLE, MENU_EDIT, setAccent<1>, nullptr},

    // ── 9 CV ──
    {"IN2 DEST", getCvTarget<0>, nullptr, 72, 0, 9, ROW_SINGLE, MENU_EDIT, setCvTarget<0>, nullptr},
    {"IN2 DEPTH", getCvDepth<0>, nullptr, 82, 0, 9, ROW_SINGLE, MENU_EDIT, setCvDepth<0>, nullptr},
    {"IN3 DEST", getCvTarget<1>, nullptr, 72, 0, 9, ROW_SINGLE, MENU_EDIT, setCvTarget<1>, nullptr},
    {"IN3 DEPTH", getCvDepth<1>, nullptr, 82, 0, 9, ROW_SINGLE, MENU_EDIT, setCvDepth<1>, nullptr},

    // ── 10 SETTINGS ──
    {"SLOT", getSlot, nullptr, 82, 0, 10, ROW_SINGLE, MENU_EDIT, setSlot, nullptr},
    {"SAVE", nullptr, nullptr, 0, 0, 10, ROW_ACTION, MENU_ACTION, nullptr, actSave},
    {"LOAD", nullptr, nullptr, 0, 0, 10, ROW_ACTION, MENU_ACTION, nullptr, actLoad},
    {"TIMEOUT", getTimeout, nullptr, 82, 0, 10, ROW_SINGLE, MENU_EDIT, setTimeout, nullptr},
};

const int MENU_ITEM_COUNT = (int)(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]));
