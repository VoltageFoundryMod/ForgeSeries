#pragma once
// ============================================================
// menuHandlers.hpp — setter/getter functions and MENU_ITEMS[].
// Included once from the app TU after all other headers.
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
//   menuRender.hpp       — HandleDisplay() and the Lissajous home screen
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
//   0  HOME       — the Lissajous plot (custom renderer in menuRender.hpp)
//   1  A SYSTEM   — attractor, speed, its four parameters
//   2  A OUTPUT   — which axis each jack follows, level, offset, smooth, range
//                   Rows are named for the panel: A1/A2 are generator A's two
//                   jacks, down the LEFT column. B1/B2 are B's, down the right.
//   3  B SYSTEM
//   4  B OUTPUT
//   5  LINK       — couple, IN 1 role, the re-seed gestures
//   6  CV IN      — IN 2 / IN 3 target + depth
//   7  SETTINGS   — home view, screen timeout, boot menu
//   8  PRESETS    — preset slot, save, load, randomize
//
// Each generator gets two pages rather than one: what the system IS (which
// equations, how fast, with what constants) and how it is HEARD (which axes,
// how loud, how smooth) are separate decisions, and cramming them into one page
// would exceed the six-row limit anyway.
//
// PAGE LENGTH LIMIT: six rows. MD_START_Y=12 with MD_ROW_H=9 puts row 6 at
// y=57, whose glyphs end on row 63 — exactly the bottom of the screen. A
// seventh row is silently clipped.
//
// Per-generator handlers are templates parameterised on the index, so the two
// generators share one implementation instead of duplicated copy-paste pairs.
// The parameter rows go one further and are templated on the parameter index
// too: `setParam<1, 2>` is generator B's third parameter.

// ── Globals owned by the app TU ──────────────────────────────
extern ChaosWorld world;
extern GenParams genParams[2];
extern WorldParams worldParams;
extern bool unsavedChanges;
extern bool displayRefresh; // REQUEST_DISPLAY_REFRESH() writes it
extern int menuMode;
extern int menuScreenTimeout;
extern uint8_t homeView;
extern void ShowTemporaryMessage(const char *msg, uint32_t durationMs);

// ── Live plot view ───────────────────────────────────────────
// Parameters flagged livePreview are ones you judge by watching the figure, not
// by reading the number: RHO 28 means nothing until you see the wing open. While
// one is being turned the plot takes the screen and the parameter rides along in
// a strip at the bottom, so the value and its effect are visible at once.
//
// Armed on the first detent rather than on the click that enters edit mode —
// until you actually turn something the rest of the page is still worth seeing.
// It then holds for LIVE_VIEW_HOLD_MS past the last detent, so a slow adjustment
// does not flicker between the two screens between turns.
static const unsigned long LIVE_VIEW_HOLD_MS = 4000;
static unsigned long liveViewUntil = 0; // millis() deadline; 0 = show the menu page

static inline void LiveViewArm() { liveViewUntil = millis() + LIVE_VIEW_HOLD_MS; }
static inline void LiveViewClear() { liveViewUntil = 0; }
static inline bool LiveViewActive() {
    // Signed difference, not `millis() < liveViewUntil`: the deadline is computed
    // by addition and so straddles the 49-day wrap twice per boot-life.
    return liveViewUntil != 0 && (long)(millis() - liveViewUntil) < 0;
}

// True if the given 1-based menu item hands the screen to the plot.
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
// `class Dir` that appStorage.hpp reaches, so the short name is ambiguous at
// every call site.
static inline int StepDir(int delta) { return delta > 0 ? 1 : -1; }

static inline String Pct(float v01) { return String((int)lroundf(v01 * 100.0f)) + "%"; }

// A system parameter, printed with as many decimals as it needs and no more.
// The twelve systems' parameters span four orders of magnitude — Finance's A is
// 0.001 and Chen's is 35 — so a fixed number of decimals is either unreadable at
// one end or a lie at the other.
static String FmtParam(float v) {
    const float a = fabsf(v);
    if (a < 0.01f)
        return String(v, 4);
    if (a < 1.0f)
        return String(v, 3);
    if (a < 10.0f)
        return String(v, 2);
    return String(v, 1);
}

// SPEED reads as a rate multiplier, which is what it is: 1.00x is the figure
// traced at the tempo the system was catalogued at.
static String FmtSpeed(float s) {
    return (s < 10.0f) ? String(s, 2) + "x" : String(s, 1) + "x";
}

// ── SYSTEM (per generator) ───────────────────────────────────
template <int G>
static String getSystem() { return String(AttSpec(genParams[G].system).name); }
template <int G>
static void setSystem(int d) {
    const int next = constrain((int)genParams[G].system + StepDir(d), 0, (int)AttractorCount - 1);
    if (next == (int)genParams[G].system) {
        return;
    }
    // Parameter 1 is SIGMA on Lorenz and ALPHA on Chua: carrying the old numbers
    // across hands the new system values from a different equation, usually ones
    // it has no attractor at. The published set is the only sane landing point.
    LoadSystemDefaults(genParams[G], next);
    MarkUnsaved();
}

// SPEED is geometric, not linear: the range is 1600:1 and the ear hears rate
// ratios, so a fixed increment would crawl at the top and jump at the bottom.
// 4 % per detent, multiplied by the encoder's acceleration factor.
template <int G>
static String getSpeed() { return FmtSpeed(genParams[G].speed); }
template <int G>
static void setSpeed(int d) {
    genParams[G].speed =
        constrain(genParams[G].speed * powf(1.04f, (float)d), ATT_SPEED_MIN, ATT_SPEED_MAX);
    MarkUnsaved();
}

// ── The four parameter rows (per generator) ──────────────────
// Every system shows four rows whatever it actually has, and the ones it does
// not have read "-". A page whose row count changed with the selected system
// would move every row below it under the cursor, which is worse than a couple
// of inert rows — and the inert rows are also what tells you at a glance that
// SPROTT B has nothing to turn.
template <int G, int K>
static String getParamName() {
    const AttractorSpec &sp = AttSpec(genParams[G].system);
    return (K < (int)sp.paramCount) ? String(sp.params[K].name) : String("-");
}
template <int G, int K>
static String getParamValue() {
    const AttractorSpec &sp = AttSpec(genParams[G].system);
    return (K < (int)sp.paramCount) ? FmtParam(genParams[G].param[K]) : String("");
}
template <int G, int K>
static void setParam(int d) {
    const AttractorSpec &sp = AttSpec(genParams[G].system);
    if (K >= (int)sp.paramCount) {
        return;
    }
    // One percent of the parameter's own span per detent, so the control feels
    // the same on ALPHA (5..30) as on A (0.0001..0.01).
    const float step = (sp.params[K].max - sp.params[K].min) * 0.01f;
    genParams[G].param[K] = constrain(genParams[G].param[K] + (float)d * step,
                                      sp.params[K].min, sp.params[K].max);
    MarkUnsaved();
}

// ── OUTPUT (per generator) ───────────────────────────────────
// Which state variable each of the generator's two jacks follows. Setting both
// to the same axis is allowed and gives two copies of one voltage — pointless,
// but it is not the firmware's job to forbid it, and it is the fastest way to
// prove to yourself that the pair really are different signals.
template <int G, int J>
static String getSrc() { return String(AttAxisNames[genParams[G].src[J]]); }
template <int G, int J>
static void setSrc(int d) {
    genParams[G].src[J] =
        (uint8_t)constrain((int)genParams[G].src[J] + StepDir(d), 0, (int)AxisCount - 1);
    MarkUnsaved();
}

template <int G>
static String getLevel() { return String(genParams[G].level) + "%"; }
template <int G>
static void setLevel(int d) {
    genParams[G].level = (uint8_t)constrain((int)genParams[G].level + d, 0, ATT_LEVEL_MAX);
    MarkUnsaved();
}

// OFFSET slides the pair up or down the 0-5 V range. Shown signed, because
// which way it leans is the whole content of the number.
template <int G>
static String getOffset() {
    const int o = genParams[G].offset;
    return String(o > 0 ? "+" : "") + String(o) + "%";
}
template <int G>
static void setOffset(int d) {
    genParams[G].offset =
        (int8_t)constrain((int)genParams[G].offset + d, -ATT_OFFSET_MAX, ATT_OFFSET_MAX);
    MarkUnsaved();
}

template <int G>
static String getSmooth() { return String(genParams[G].smooth) + "%"; }
template <int G>
static void setSmooth(int d) {
    genParams[G].smooth = (uint8_t)constrain((int)genParams[G].smooth + d, 0, ATT_SMOOTH_MAX);
    MarkUnsaved();
}

// FIXED uses the published window measured at the system's default parameters —
// predictable, and identical from one boot to the next. AUTO tracks the orbit's
// own window, which is what you want once the parameters have been moved far
// enough that the figure no longer fills the jack. See generator.hpp.
template <int G>
static String getRange() { return genParams[G].autoRange ? String("AUTO") : String("FIXED"); }
template <int G>
static void actRange() {
    genParams[G].autoRange = genParams[G].autoRange ? 0 : 1;
    MarkUnsaved();
}

// ── LINK ─────────────────────────────────────────────────────
// The signature control: how hard the two orbits pull on each other. At 0 they
// are unrelated. Turned up, they entrain — the two figures start to share their
// timing while keeping their own shapes — and near the top, two copies of one
// system lock into a single orbit.
static String getCouple() { return Pct(worldParams.couple); }
static void setCouple(int d) {
    worldParams.couple = constrain(worldParams.couple + (float)d * 0.01f, 0.0f, 1.0f);
    MarkUnsaved();
}

static String getIn1Role() { return String(In1RoleNames[in1Role]); }
static void setIn1Role(int d) {
    in1Role = (uint8_t)constrain((int)in1Role + StepDir(d), 0, (int)In1RoleLength - 1);
    MarkUnsaved();
}

// Re-seeding puts an orbit back on its start point. On a system this sensitive
// that is not "the same pattern again" — the two generators are seeded a
// thousandth apart and diverge completely within seconds — it is a fresh draw
// from the same figure, which is the gesture this module has instead of a reset.
static void actReseedA() {
    world.Reseed(0);
    ShowTemporaryMessage("SEED A", 400);
}
static void actReseedB() {
    world.Reseed(1);
    ShowTemporaryMessage("SEED B", 400);
}
static void actReseedAll() {
    world.Reseed();
    ShowTemporaryMessage("RESEED", 400);
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
// Which plot the home screen shows. Both is the default and the honest one; a
// single generator gets the whole 128 px, which is worth it when you are shaping
// one figure and want to see what it actually looks like.
static const char *const homeViewNames[] = {"A+B", "A", "B"};
static String getView() { return String(homeViewNames[constrain((int)homeView, 0, 2)]); }
static void setView(int d) {
    homeView = (uint8_t)constrain((int)homeView + StepDir(d), 0, 2);
    MarkUnsaved();
}

static const char *const screenTimeoutNames[] = {"OFF", "2s", "5s", "10s", "20s"};
static String getTimeout() { return String(screenTimeoutNames[constrain(menuScreenTimeout, 0, 4)]); }
static void setTimeout(int d) {
    menuScreenTimeout = constrain(menuScreenTimeout + StepDir(d), 0, 4);
    static const unsigned long kTimeoutOpts[] = {0, 2000, 5000, 10000, 20000};
    displayMgr.SetMenuTimeout(kTimeoutOpts[menuScreenTimeout]);
    MarkUnsaved();
}

// Hand the module back to the shell's SELECT MODULE screen — which is also the
// only way into the calibration wizard, since that has to run with no app
// started. Rack has no shell and nothing to switch to, so the port says so
// rather than pretending.
static void actBootMenu() {
#ifdef FORGE_UNIFIED
    ::forge::RequestAppMenu();
#else
    ShowTemporaryMessage("N/A", 700);
#endif
}

// ── PRESETS ──────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────
// MENU_ITEMS[]
// { label, valueFn, valueFn2, col1x, col2x, group, rowStyle, type, setter, action,
//   livePreview }
//
// The trailing `true` marks the parameters that are judged by watching the plot
// rather than by reading a number — turning one hands the screen to the plot for
// as long as you keep adjusting. That is nearly everything on the SYSTEM, OUTPUT
// and LINK pages, and nothing on the routing or housekeeping pages.
// ─────────────────────────────────────────────────────────────
const MenuItem MENU_ITEMS[] = {
    // ── 0 HOME ──
    {"HOME", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_ACTION, nullptr, nullptr},

    // ── 1 A SYSTEM ── (exactly six rows — the page limit)
    {"SYSTEM", getSystem<0>, nullptr, 64, 0, 1, ROW_SINGLE, MENU_EDIT, setSystem<0>, nullptr, true},
    {"SPEED", getSpeed<0>, nullptr, 82, 0, 1, ROW_SINGLE, MENU_EDIT, setSpeed<0>, nullptr, true},
    {"P1", getParamName<0, 0>, getParamValue<0, 0>, 34, 70, 1, ROW_TWOCOL, MENU_EDIT, setParam<0, 0>, nullptr, true},
    {"P2", getParamName<0, 1>, getParamValue<0, 1>, 34, 70, 1, ROW_TWOCOL, MENU_EDIT, setParam<0, 1>, nullptr, true},
    {"P3", getParamName<0, 2>, getParamValue<0, 2>, 34, 70, 1, ROW_TWOCOL, MENU_EDIT, setParam<0, 2>, nullptr, true},
    {"P4", getParamName<0, 3>, getParamValue<0, 3>, 34, 70, 1, ROW_TWOCOL, MENU_EDIT, setParam<0, 3>, nullptr, true},

    // ── 2 A OUTPUT ──
    {"A1", getSrc<0, 0>, nullptr, 100, 0, 2, ROW_SINGLE, MENU_EDIT, setSrc<0, 0>, nullptr, true},
    {"A2", getSrc<0, 1>, nullptr, 100, 0, 2, ROW_SINGLE, MENU_EDIT, setSrc<0, 1>, nullptr, true},
    {"LEVEL", getLevel<0>, nullptr, 82, 0, 2, ROW_SINGLE, MENU_EDIT, setLevel<0>, nullptr, true},
    {"OFFSET", getOffset<0>, nullptr, 82, 0, 2, ROW_SINGLE, MENU_EDIT, setOffset<0>, nullptr, true},
    {"SMOOTH", getSmooth<0>, nullptr, 82, 0, 2, ROW_SINGLE, MENU_EDIT, setSmooth<0>, nullptr, true},
    {"RANGE", getRange<0>, nullptr, 82, 0, 2, ROW_SINGLE, MENU_TOGGLE, nullptr, actRange<0>, true},

    // ── 3 B SYSTEM ──
    {"SYSTEM", getSystem<1>, nullptr, 64, 0, 3, ROW_SINGLE, MENU_EDIT, setSystem<1>, nullptr, true},
    {"SPEED", getSpeed<1>, nullptr, 82, 0, 3, ROW_SINGLE, MENU_EDIT, setSpeed<1>, nullptr, true},
    {"P1", getParamName<1, 0>, getParamValue<1, 0>, 34, 70, 3, ROW_TWOCOL, MENU_EDIT, setParam<1, 0>, nullptr, true},
    {"P2", getParamName<1, 1>, getParamValue<1, 1>, 34, 70, 3, ROW_TWOCOL, MENU_EDIT, setParam<1, 1>, nullptr, true},
    {"P3", getParamName<1, 2>, getParamValue<1, 2>, 34, 70, 3, ROW_TWOCOL, MENU_EDIT, setParam<1, 2>, nullptr, true},
    {"P4", getParamName<1, 3>, getParamValue<1, 3>, 34, 70, 3, ROW_TWOCOL, MENU_EDIT, setParam<1, 3>, nullptr, true},

    // ── 4 B OUTPUT ──
    {"B1", getSrc<1, 0>, nullptr, 100, 0, 4, ROW_SINGLE, MENU_EDIT, setSrc<1, 0>, nullptr, true},
    {"B2", getSrc<1, 1>, nullptr, 100, 0, 4, ROW_SINGLE, MENU_EDIT, setSrc<1, 1>, nullptr, true},
    {"LEVEL", getLevel<1>, nullptr, 82, 0, 4, ROW_SINGLE, MENU_EDIT, setLevel<1>, nullptr, true},
    {"OFFSET", getOffset<1>, nullptr, 82, 0, 4, ROW_SINGLE, MENU_EDIT, setOffset<1>, nullptr, true},
    {"SMOOTH", getSmooth<1>, nullptr, 82, 0, 4, ROW_SINGLE, MENU_EDIT, setSmooth<1>, nullptr, true},
    {"RANGE", getRange<1>, nullptr, 82, 0, 4, ROW_SINGLE, MENU_TOGGLE, nullptr, actRange<1>, true},

    // ── 5 LINK ──
    {"COUPLE", getCouple, nullptr, 82, 0, 5, ROW_SINGLE, MENU_EDIT, setCouple, nullptr, true},
    {"IN 1", getIn1Role, nullptr, 76, 0, 5, ROW_SINGLE, MENU_EDIT, setIn1Role, nullptr},
    {"RESEED A", nullptr, nullptr, 0, 0, 5, ROW_ACTION, MENU_ACTION, nullptr, actReseedA},
    {"RESEED B", nullptr, nullptr, 0, 0, 5, ROW_ACTION, MENU_ACTION, nullptr, actReseedB},
    {"RESEED ALL", nullptr, nullptr, 0, 0, 5, ROW_ACTION, MENU_ACTION, nullptr, actReseedAll},

    // ── 6 CV IN ──
    {"IN2 DEST", getCvTarget<0>, nullptr, 72, 0, 6, ROW_SINGLE, MENU_EDIT, setCvTarget<0>, nullptr},
    {"IN2 DEPTH", getCvDepth<0>, nullptr, 82, 0, 6, ROW_SINGLE, MENU_EDIT, setCvDepth<0>, nullptr},
    {"IN3 DEST", getCvTarget<1>, nullptr, 72, 0, 6, ROW_SINGLE, MENU_EDIT, setCvTarget<1>, nullptr},
    {"IN3 DEPTH", getCvDepth<1>, nullptr, 82, 0, 6, ROW_SINGLE, MENU_EDIT, setCvDepth<1>, nullptr},

    // ── 7 SETTINGS ──
    {"VIEW", getView, nullptr, 82, 0, 7, ROW_SINGLE, MENU_EDIT, setView, nullptr},
    {"TIMEOUT", getTimeout, nullptr, 82, 0, 7, ROW_SINGLE, MENU_EDIT, setTimeout, nullptr},
    {"BOOT MENU", nullptr, nullptr, 0, 0, 7, ROW_ACTION, MENU_ACTION, nullptr, actBootMenu},

    // ── 8 PRESETS ──
    // RANDOM belongs here rather than in SETTINGS: like LOAD it replaces the
    // whole patch, it just rolls the new one instead of reading it from a slot.
    {"SLOT", getSlot, nullptr, 82, 0, 8, ROW_SINGLE, MENU_EDIT, setSlot, nullptr},
    {"SAVE", nullptr, nullptr, 0, 0, 8, ROW_ACTION, MENU_ACTION, nullptr, actSave},
    {"LOAD", nullptr, nullptr, 0, 0, 8, ROW_ACTION, MENU_ACTION, nullptr, actLoad},
    {"RANDOM", nullptr, nullptr, 0, 0, 8, ROW_ACTION, MENU_ACTION, nullptr, actRandom},
};

const int MENU_ITEM_COUNT = (int)(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]));
