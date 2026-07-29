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
//   2  LOOP       — phrase length in beats, nap/wake, per-container shift
//   3  COUPLING   — proximity, couple amount, reset/kick
//   4  A PHYSICS  — gravity, bounce, grip, spin, reverse, balls
//   5  B PHYSICS
//   6  A NOTES    — scale, root, spread, bias, peg count
//   7  B NOTES
//   8  A GATE     — mode, attack, decay, level, accent
//   9  B GATE
//  10  CV         — IN 2 / IN 3 target + depth
//  11  SETTINGS   — preset slot, save, load, randomize, screen timeout
//
// LOOP sits next to CLOCK rather than at the end because it is a clock-domain
// control — its length is in beats and it follows the tempo.
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
extern bool unsavedChanges;
extern bool displayRefresh; // REQUEST_DISPLAY_REFRESH() writes it
extern int menuMode;
extern int menuScreenTimeout;
extern void ShowTemporaryMessage(const char *msg, uint32_t durationMs);

// ── Live physics view ────────────────────────────────────────
// Parameters flagged livePreview are ones you judge by ear and eye, not by their
// number: you cannot tell what GRAVITY 140 means without watching the balls fall.
// While one is being turned the physics view takes the screen and the parameter
// rides along in a strip at the bottom, so the value and its effect are visible
// at the same time.
//
// Armed on the first detent rather than on the click that enters edit mode —
// until you actually turn something the rest of the page is still worth seeing.
// It then holds for LIVE_VIEW_HOLD_MS past the last detent, so a slow adjustment
// does not flicker back and forth between the two screens between turns.
static const unsigned long LIVE_VIEW_HOLD_MS = 4000;
static unsigned long liveViewUntil = 0; // millis() deadline; 0 = show the menu page

static inline void LiveViewArm() { liveViewUntil = millis() + LIVE_VIEW_HOLD_MS; }
static inline void LiveViewClear() { liveViewUntil = 0; }
static inline bool LiveViewActive() {
    // Signed difference, not `millis() < liveViewUntil`: the deadline is computed
    // by addition and so straddles the 49-day wrap twice per boot-life.
    return liveViewUntil != 0 && (long)(millis() - liveViewUntil) < 0;
}

// True if the given 1-based menu item hands the screen to the physics view.
static inline bool MenuItemIsLive(int item) {
    return item >= 1 && item <= MENU_ITEM_COUNT && MENU_ITEMS[item - 1].livePreview;
}

// Apply an encoder turn to the item in edit mode. The one place that arms the
// live view for MENU_EDIT items, called by both the panel firmware and the VCV
// port so the two cannot drift apart.
static inline void MenuApplyEdit(int item, int delta) {
    if (item < 1 || item > MENU_ITEM_COUNT) {
        return;
    }
    if (MENU_ITEMS[item - 1].setter) {
        MENU_ITEMS[item - 1].setter(delta);
    }
    if (MENU_ITEMS[item - 1].livePreview) {
        LiveViewArm();
    }
}

// ── Shared helpers ───────────────────────────────────────────
static inline void MarkUnsaved() {
    unsavedChanges = true;
    displayMgr.SetUnsavedChanges(true);
}

// Reduce an encoder delta to a direction. Used where a single step per detent is
// the only sensible behaviour (enums, toggles) regardless of spin speed.
// Named StepDir, not Dir: the RP2040 core's filesystem API exports a global
// `class Dir`, and appStorage.hpp now reaches it, so the short name is
// ambiguous at every call site.
static inline int StepDir(int delta) { return delta > 0 ? 1 : -1; }

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
    clockEngine.SetPpqn(clockEngine.GetPpqn() + StepDir(d));
    MarkUnsaved();
}

static String getQuantize() { return String(QuantizeDivNames[clockEngine.GetQuantize()]); }
static void setQuantize(int d) {
    clockEngine.SetQuantize(clockEngine.GetQuantize() + StepDir(d));
    MarkUnsaved();
}

static String getIn1Role() { return String(In1RoleNames[in1Role]); }
static void setIn1Role(int d) {
    in1Role = (uint8_t)constrain((int)in1Role + StepDir(d), 0, (int)In1RoleLength - 1);
    // The clock only follows the jack while the jack is a clock; anything else
    // has to hand tempo back to the internal setting or the containers would
    // keep turning at a tempo nothing is driving any more.
    clockEngine.SetExternal(in1Role == In1Clock);
    MarkUnsaved();
}

// ── LOOP ─────────────────────────────────────────────────────
// The module's one weakness is that a phrase you like walks away. The
// simulation is deterministic, so LOOP snapshots it and rewinds every N beats;
// see the loop section of physics.hpp for how the rewind is kept exact.
static String getLoopBeats() {
    return worldParams.loopBeats == 0 ? String("OFF") : String(worldParams.loopBeats);
}
static void setLoopBeats(int d) {
    // Accepts the encoder's speed factor rather than a single step: the range
    // runs to 64 and a long phrase should not need sixty detents.
    worldParams.loopBeats =
        (uint8_t)constrain((int)worldParams.loopBeats + d, 0, PARAM_LOOP_BEATS_MAX);
    MarkUnsaved();
}

static String getLoopWake() { return String(worldParams.loopWake); }
static void setLoopWake(int d) {
    worldParams.loopWake = (uint8_t)constrain((int)worldParams.loopWake + StepDir(d),
                                              PARAM_LOOP_WAKE_MIN, PARAM_LOOP_WAKE_MAX);
    MarkUnsaved();
}

static String getLoopNap() {
    return worldParams.loopNap == 0 ? String("OFF") : String(worldParams.loopNap);
}
static void setLoopNap(int d) {
    worldParams.loopNap =
        (uint8_t)constrain((int)worldParams.loopNap + StepDir(d), 0, PARAM_LOOP_NAP_MAX);
    MarkUnsaved();
}

// Offsetting one container's nap cycle against the other is what turns nap/wake
// from a tremolo into call-and-response: wake 1 / nap 1 with B shifted by 1 has
// the containers trading phrases.
template <int C>
static String getLoopShift() { return String(worldParams.loopShift[C]); }
template <int C>
static void setLoopShift(int d) {
    worldParams.loopShift[C] =
        (uint8_t)constrain((int)worldParams.loopShift[C] + StepDir(d), 0, PARAM_LOOP_SHIFT_MAX);
    MarkUnsaved();
}

// Throw the captured phrase away and keep whatever the balls are doing now.
// The loop is a lottery — this is the re-roll, and it is how the page is
// actually used once the length is set.
static void actNewPhrase() {
    physicsWorld.ArmLoop();
    ShowTemporaryMessage("PHRASE", 400);
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
        (uint8_t)constrain((int)containerParams[C].spin + StepDir(d), 0, (int)Spin16);
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
        (uint8_t)constrain((int)containerParams[C].balls + StepDir(d), PHYS_MIN_BALLS, PHYS_MAX_BALLS);
    MarkUnsaved();
}

// ── NOTES (per container) ────────────────────────────────────
template <int C>
static String getScaleName() { return String(scaleNames[channels[C].GetScaleIndex()]); }
template <int C>
static void setScale(int d) {
    channels[C].SelectScale(channels[C].GetScaleIndex() + StepDir(d));
    MarkUnsaved();
}

template <int C>
static String getRootName() { return String(noteNames[channels[C].GetRootIndex()]); }
template <int C>
static void setRoot(int d) {
    channels[C].SelectRoot(channels[C].GetRootIndex() + StepDir(d));
    MarkUnsaved();
}

// SPREAD is how many octaves the peg ring covers; BIAS is where the notes
// crowd inside it. Together they replace the old single OCTAVE offset.
template <int C>
static String getSpread() { return String(channels[C].GetSpread()) + "oct"; }
template <int C>
static void setSpread(int d) {
    channels[C].SetSpread(channels[C].GetSpread() + StepDir(d));
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
        (uint8_t)constrain((int)containerParams[C].pegs + StepDir(d), PHYS_MIN_PEGS, PHYS_MAX_PEGS);
    MarkUnsaved();
}

// ── GATE (per container) ─────────────────────────────────────
template <int C>
static String getGateMode() { return String(channels[C].envelope.GetModeName()); }
template <int C>
static void setGateMode(int d) {
    channels[C].envelope.SetMode(channels[C].envelope.GetMode() + StepDir(d));
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
    cvTarget[I] = (uint8_t)constrain((int)cvTarget[I] + StepDir(d), 0, (int)CVTargetLength - 1);
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
    saveSlot = constrain(saveSlot + StepDir(d), 0, NUM_SLOTS - 1);
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

// Roll a new patch. Shares its implementation with the VCV plugin's Randomize,
// so the panel and the host produce the same kind of result — see
// lib/randomize.hpp for what it does and does not touch. It does not write a
// slot, so the previous patch is one LOAD away as long as you had saved it.
static void actRandom() {
    RandomizeParams((uint32_t)micros());
    MarkUnsaved();
    ShowTemporaryMessage("RANDOM", 700);
}

// Hand the module back to the shell's SELECT MODULE screen — which is also the
// only way into the calibration wizard, since that has to run with no app
// started. The shell finishes the tick, calls End() so anything owed to storage
// is flushed, and reboots into the selector.
//
// The hold-the-encoder gesture does the same thing; this is the discoverable
// way in. Rack has no shell and nothing to switch to, so the port says so
// rather than pretending.
static void actBootMenu() {
#ifdef FORGE_UNIFIED
    ::forge::RequestAppMenu();
#else
    ShowTemporaryMessage("N/A", 700);
#endif
}

static const char *const screenTimeoutNames[] = {"OFF", "2s", "5s", "10s", "20s"};
static String getTimeout() { return String(screenTimeoutNames[constrain(menuScreenTimeout, 0, 4)]); }
static void setTimeout(int d) {
    menuScreenTimeout = constrain(menuScreenTimeout + StepDir(d), 0, 4);
    static const unsigned long kTimeoutOpts[] = {0, 2000, 5000, 10000, 20000};
    displayMgr.SetMenuTimeout(kTimeoutOpts[menuScreenTimeout]);
    MarkUnsaved();
}

// ─────────────────────────────────────────────────────────────
// MENU_ITEMS[]
// { label, valueFn, valueFn2, col1x, col2x, group, rowStyle, type, setter, action,
//   livePreview }
//
// The trailing `true` marks the parameters that are judged by watching the
// simulation rather than by reading a number — turning one hands the screen to
// the physics view for as long as you keep adjusting. Everything else leaves the
// field off and gets false.
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
    {"PROXIMITY", getProximity, nullptr, 82, 0, 2, ROW_SINGLE, MENU_EDIT, setProximity, nullptr, true},
    {"COUPLE", getCoupling, nullptr, 82, 0, 2, ROW_SINGLE, MENU_EDIT, setCoupling, nullptr, true},
    {"RESET BALLS", nullptr, nullptr, 0, 0, 2, ROW_ACTION, MENU_ACTION, nullptr, actReset},
    {"KICK", nullptr, nullptr, 0, 0, 2, ROW_ACTION, MENU_ACTION, nullptr, actKick},

    // ── 3 A PHYSICS ──
    {"GRAVITY", getGravity<0>, nullptr, 76, 0, 3, ROW_SINGLE, MENU_EDIT, setGravity<0>, nullptr, true},
    {"BOUNCE", getBounce<0>, nullptr, 76, 0, 3, ROW_SINGLE, MENU_EDIT, setBounce<0>, nullptr, true},
    {"GRIP", getGrip<0>, nullptr, 76, 0, 3, ROW_SINGLE, MENU_EDIT, setGrip<0>, nullptr, true},
    {"SPIN", getSpin<0>, nullptr, 76, 0, 3, ROW_SINGLE, MENU_EDIT, setSpin<0>, nullptr, true},
    {"DIR", getReverse<0>, nullptr, 76, 0, 3, ROW_SINGLE, MENU_TOGGLE, nullptr, actReverse<0>, true},
    {"BALLS", getBalls<0>, nullptr, 76, 0, 3, ROW_SINGLE, MENU_EDIT, setBalls<0>, nullptr, true},

    // ── 4 B PHYSICS ──
    {"GRAVITY", getGravity<1>, nullptr, 76, 0, 4, ROW_SINGLE, MENU_EDIT, setGravity<1>, nullptr, true},
    {"BOUNCE", getBounce<1>, nullptr, 76, 0, 4, ROW_SINGLE, MENU_EDIT, setBounce<1>, nullptr, true},
    {"GRIP", getGrip<1>, nullptr, 76, 0, 4, ROW_SINGLE, MENU_EDIT, setGrip<1>, nullptr, true},
    {"SPIN", getSpin<1>, nullptr, 76, 0, 4, ROW_SINGLE, MENU_EDIT, setSpin<1>, nullptr, true},
    {"DIR", getReverse<1>, nullptr, 76, 0, 4, ROW_SINGLE, MENU_TOGGLE, nullptr, actReverse<1>, true},
    {"BALLS", getBalls<1>, nullptr, 76, 0, 4, ROW_SINGLE, MENU_EDIT, setBalls<1>, nullptr, true},

    // ── 5 LOOP ── (exactly six rows — the page limit)
    {"BEATS", getLoopBeats, nullptr, 82, 0, 5, ROW_SINGLE, MENU_EDIT, setLoopBeats, nullptr, true},
    {"WAKE", getLoopWake, nullptr, 82, 0, 5, ROW_SINGLE, MENU_EDIT, setLoopWake, nullptr, true},
    {"NAP", getLoopNap, nullptr, 82, 0, 5, ROW_SINGLE, MENU_EDIT, setLoopNap, nullptr, true},
    {"A SHIFT", getLoopShift<0>, nullptr, 82, 0, 5, ROW_SINGLE, MENU_EDIT, setLoopShift<0>, nullptr},
    {"B SHIFT", getLoopShift<1>, nullptr, 82, 0, 5, ROW_SINGLE, MENU_EDIT, setLoopShift<1>, nullptr},
    {"NEW PHRASE", nullptr, nullptr, 0, 0, 5, ROW_ACTION, MENU_ACTION, nullptr, actNewPhrase},

    // ── 6 A NOTES ──
    {"SCALE", getScaleName<0>, nullptr, 76, 0, 6, ROW_SINGLE, MENU_EDIT, setScale<0>, nullptr},
    {"ROOT", getRootName<0>, nullptr, 76, 0, 6, ROW_SINGLE, MENU_EDIT, setRoot<0>, nullptr},
    {"SPREAD", getSpread<0>, nullptr, 76, 0, 6, ROW_SINGLE, MENU_EDIT, setSpread<0>, nullptr},
    {"BIAS", getBias<0>, nullptr, 76, 0, 6, ROW_SINGLE, MENU_EDIT, setBias<0>, nullptr},
    {"PEGS", getPegs<0>, nullptr, 76, 0, 6, ROW_SINGLE, MENU_EDIT, setPegs<0>, nullptr, true},

    // ── 7 B NOTES ──
    {"SCALE", getScaleName<1>, nullptr, 76, 0, 7, ROW_SINGLE, MENU_EDIT, setScale<1>, nullptr},
    {"ROOT", getRootName<1>, nullptr, 76, 0, 7, ROW_SINGLE, MENU_EDIT, setRoot<1>, nullptr},
    {"SPREAD", getSpread<1>, nullptr, 76, 0, 7, ROW_SINGLE, MENU_EDIT, setSpread<1>, nullptr},
    {"BIAS", getBias<1>, nullptr, 76, 0, 7, ROW_SINGLE, MENU_EDIT, setBias<1>, nullptr},
    {"PEGS", getPegs<1>, nullptr, 76, 0, 7, ROW_SINGLE, MENU_EDIT, setPegs<1>, nullptr, true},

    // ── 8 A GATE ──
    {"MODE", getGateMode<0>, nullptr, 76, 0, 8, ROW_SINGLE, MENU_EDIT, setGateMode<0>, nullptr},
    {"ATTACK", getAttack<0>, nullptr, 76, 0, 8, ROW_SINGLE, MENU_EDIT, setAttack<0>, nullptr},
    {"DECAY", getDecay<0>, nullptr, 76, 0, 8, ROW_SINGLE, MENU_EDIT, setDecay<0>, nullptr},
    {"LEVEL", getLevel<0>, nullptr, 76, 0, 8, ROW_SINGLE, MENU_EDIT, setLevel<0>, nullptr},
    {"ACCENT", getAccent<0>, nullptr, 76, 0, 8, ROW_SINGLE, MENU_EDIT, setAccent<0>, nullptr},

    // ── 9 B GATE ──
    {"MODE", getGateMode<1>, nullptr, 76, 0, 9, ROW_SINGLE, MENU_EDIT, setGateMode<1>, nullptr},
    {"ATTACK", getAttack<1>, nullptr, 76, 0, 9, ROW_SINGLE, MENU_EDIT, setAttack<1>, nullptr},
    {"DECAY", getDecay<1>, nullptr, 76, 0, 9, ROW_SINGLE, MENU_EDIT, setDecay<1>, nullptr},
    {"LEVEL", getLevel<1>, nullptr, 76, 0, 9, ROW_SINGLE, MENU_EDIT, setLevel<1>, nullptr},
    {"ACCENT", getAccent<1>, nullptr, 76, 0, 9, ROW_SINGLE, MENU_EDIT, setAccent<1>, nullptr},

    // ── 10 CV ──
    {"IN2 DEST", getCvTarget<0>, nullptr, 72, 0, 10, ROW_SINGLE, MENU_EDIT, setCvTarget<0>, nullptr},
    {"IN2 DEPTH", getCvDepth<0>, nullptr, 82, 0, 10, ROW_SINGLE, MENU_EDIT, setCvDepth<0>, nullptr},
    {"IN3 DEST", getCvTarget<1>, nullptr, 72, 0, 10, ROW_SINGLE, MENU_EDIT, setCvTarget<1>, nullptr},
    {"IN3 DEPTH", getCvDepth<1>, nullptr, 82, 0, 10, ROW_SINGLE, MENU_EDIT, setCvDepth<1>, nullptr},

    // ── 11 SETTINGS ──
    {"TIMEOUT", getTimeout, nullptr, 82, 0, 11, ROW_SINGLE, MENU_EDIT, setTimeout, nullptr},
    {"BOOT MENU", nullptr, nullptr, 0, 0, 11, ROW_ACTION, MENU_ACTION, nullptr, actBootMenu},

    // ── 12 PRESETS ──
    // RANDOM belongs here rather than in SETTINGS: like LOAD it replaces the
    // whole patch, it just rolls the new one instead of reading it from a slot.
    {"SLOT", getSlot, nullptr, 82, 0, 12, ROW_SINGLE, MENU_EDIT, setSlot, nullptr},
    {"SAVE", nullptr, nullptr, 0, 0, 12, ROW_ACTION, MENU_ACTION, nullptr, actSave},
    {"LOAD", nullptr, nullptr, 0, 0, 12, ROW_ACTION, MENU_ACTION, nullptr, actLoad},
    {"RANDOM", nullptr, nullptr, 0, 0, 12, ROW_ACTION, MENU_ACTION, nullptr, actRandom},
};

const int MENU_ITEM_COUNT = (int)(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]));
