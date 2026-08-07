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
//   menuRender.hpp       — HandleDisplay() and the loom home screen
//
// HOW ITEMS AND GROUPS WORK
// ──────────────────────────
// • Every entry in MENU_ITEMS[] is a "menu item" numbered 1…N (1-based).
// • The global  menuItem  tracks which item is selected.
// • The global  menuMode  is 0 when navigating, or equals the item number
//   currently being edited.
// • Items sharing a  group  render on the same page.
// • MENU_ITEM_COUNT is computed automatically — there is no manual counter.
//
// GROUP MAP
// ──────────
//    0  HOME       — the loom (custom renderer in menuRender.hpp)
//    1  REG A      — length, chance, and the four editing actions
//    2  REG B
//    3  WEAVE      — amount, direction
//    4  CLOCK      — bpm, input ppqn, rate
//    5  SCALE      — root, scale, transpose
//    6  ROUTING    — the one row that stamps all four jacks
//    7  OUT 1      — source, type, depth, rotate, + two contextual rows
//    8  OUT 2
//    9  OUT 3
//   10  OUT 4
//   11  CV IN      — IN 2 / IN 3 target + depth
//   12  SETTINGS   — screen timeout, boot menu
//   13  PRESETS    — slot, save, load, random
//
// PAGE LENGTH LIMIT: six rows. MD_START_Y=12 with MD_ROW_H=9 puts row 6 at
// y=57, whose glyphs end on row 63 — exactly the bottom of the screen. A
// seventh row is silently clipped, which is why each OUT page carries exactly
// six fields and not seven.
//
// Per-register and per-jack handlers are templates parameterised on the index,
// so the two registers and the four jacks share one implementation each rather
// than duplicated copy-paste sets.

// ── Globals owned by the app TU ──────────────────────────────
extern WeavePair registers;
extern OutputBank outputs;
extern StepClock clockEngine;
extern Quantizer quantizer;
extern RegParams regParams[WEA_NUM_REGS];
extern GlobalParams globalParams;
extern bool unsavedChanges;
extern bool displayRefresh; // REQUEST_DISPLAY_REFRESH() writes it
extern int menuMode;
extern int menuScreenTimeout;
extern uint16_t noteMask;
extern uint8_t scaleIndex;
extern uint8_t rootIndex;
extern void ShowTemporaryMessage(const char *msg, uint32_t durationMs);

// ── Live loom view ───────────────────────────────────────────
// Some parameters are judged by watching the loom, not by reading a number:
// WEAVE is a picture of crossing strands, LENGTH is where the squares turn into
// dots, and tempo is how fast the whole thing moves. While one of those is being
// turned the loom takes the screen and the value rides along in a strip at the
// bottom, so the number and its effect are visible at once.
//
// Armed on the first detent rather than on the click that enters edit mode —
// until you actually turn something, the rest of the page is still worth seeing.
// It then holds for LIVE_VIEW_HOLD_MS past the last detent, so a slow adjustment
// does not flicker between the two screens between turns.
static const unsigned long LIVE_VIEW_HOLD_MS = 4000;
static unsigned long liveViewUntil = 0; // millis() deadline; 0 = show the menu page

static inline void LiveViewArm() { liveViewUntil = millis() + LIVE_VIEW_HOLD_MS; }
static inline void LiveViewClear() { liveViewUntil = 0; }
static inline bool LiveViewActive() {
    return liveViewUntil != 0 && (long)(millis() - liveViewUntil) < 0;
}
static inline bool MenuItemIsLive(int item) {
    return item >= 1 && item <= MENU_ITEM_COUNT && MENU_ITEMS[item - 1].livePreview;
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

// Apply an encoder turn to the item in edit mode. The one place that arms the
// live view, called by both the panel firmware and the VCV port so the two
// cannot drift apart.
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

// ── REGISTER (per register) ──────────────────────────────────
template <int R> static String getLength() { return String(regParams[R].length); }
template <int R> static void setLength(int d) {
    regParams[R].length = (uint8_t)constrain((int)regParams[R].length + StepDir(d),
                                             WEA_MIN_LENGTH, WEA_MAX_LENGTH);
    registers.Reg(R).SetLength(regParams[R].length);
    MarkUnsaved();
}

// CHANCE reads as a percentage because both ends are meaningful settings rather
// than extremes to be avoided: 0 locks the pattern and 100 locks it inverted at
// twice the length. Design.md §2.
template <int R> static String getChance() { return String(regParams[R].chance) + "%"; }
template <int R> static void setChance(int d) {
    regParams[R].chance = (uint8_t)constrain((int)regParams[R].chance + d, 0, 100);
    MarkUnsaved();
}

template <int R> static void actRandomize() {
    registers.Reg(R).Randomize(_rndGen);
    MarkUnsaved();
}
template <int R> static void actClear() {
    registers.Reg(R).Clear();
    MarkUnsaved();
}
template <int R> static void actFill() {
    registers.Reg(R).Fill();
    MarkUnsaved();
}
template <int R> static void actInvert() {
    registers.Reg(R).Invert();
    MarkUnsaved();
}

// ── WEAVE ────────────────────────────────────────────────────
static String getWeave() { return String(globalParams.weave) + "%"; }
static void setWeave(int d) {
    globalParams.weave = (uint8_t)constrain((int)globalParams.weave + d, 0, 100);
    MarkUnsaved();
}

static String getWeaveDir() { return String(WeaveDirNames[globalParams.dir]); }
static void setWeaveDir(int d) {
    globalParams.dir = (uint8_t)constrain((int)globalParams.dir + StepDir(d), 0,
                                          (int)WeaveDirLength - 1);
    MarkUnsaved();
}

// ── CLOCK ────────────────────────────────────────────────────
// The "E" means a clock is genuinely coming in — IsExternalLive(), not
// IsExternal(): IN 1 is always configured as a clock on this module, so the
// configured flag would read as external from the moment it boots.
static String getBpm() {
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

// Steps per beat, on both clock sources — see lib/clock.hpp. The names are
// ClockForge's, so "/4" and "x4" mean the same thing across the series.
static String getRate() { return String(clockEngine.RateName()); }
static void setRate(int d) {
    clockEngine.SetRate(clockEngine.GetRate() + StepDir(d));
    MarkUnsaved();
}

// ── SCALE ────────────────────────────────────────────────────
static String getRoot() { return String(noteNames[rootIndex]); }
static void setRoot(int d) {
    rootIndex = (uint8_t)constrain((int)rootIndex + StepDir(d), 0, 11);
    bool notes[12];
    BuildScale(scaleIndex, rootIndex, notes);
    noteMask = 0;
    for (int i = 0; i < 12; i++) {
        if (notes[i]) {
            noteMask |= (uint16_t)(1u << i);
        }
    }
    RebuildQuantizer();
    MarkUnsaved();
}

static String getScale() { return String(scaleNames[scaleIndex]); }
static void setScale(int d) {
    scaleIndex = (uint8_t)constrain((int)scaleIndex + StepDir(d), 0, numScales - 1);
    bool notes[12];
    BuildScale(scaleIndex, rootIndex, notes);
    noteMask = 0;
    for (int i = 0; i < 12; i++) {
        if (notes[i]) {
            noteMask |= (uint16_t)(1u << i);
        }
    }
    RebuildQuantizer();
    MarkUnsaved();
}

static String getTranspose() {
    const int t = globalParams.transpose;
    return (t > 0 ? String("+") : String("")) + String(t);
}
static void setTranspose(int d) {
    globalParams.transpose =
        (int8_t)constrain((int)globalParams.transpose + StepDir(d), -24, 24);
    MarkUnsaved();
}

// ── ROUTING ──────────────────────────────────────────────────
// Recomputed from the slots, never stored — so editing a jack shows CUSTOM
// without anything having to remember that it happened. Design.md §5.
static String getRouting() {
    const uint8_t r = RoutingOf(outputs.Slots());
    return r == WEA_ROUTING_CUSTOM ? String("CUSTOM") : String(RoutingNames[r]);
}
static void setRouting(int d) {
    const uint8_t cur = RoutingOf(outputs.Slots());
    // From CUSTOM, the first turn lands on a real template rather than stepping
    // off the end of the enum from an index that is not in it.
    int next = (cur == WEA_ROUTING_CUSTOM) ? 0 : (int)cur + StepDir(d);
    next = constrain(next, 0, (int)RoutingLength - 1);
    ApplyRouting(outputs.Slots(), (uint8_t)next);
    MarkUnsaved();
}

// ── OUT (per jack) ───────────────────────────────────────────
template <int J> static String getOutSource() {
    return String(RegSourceNames[outputs.Slot(J).source]);
}
template <int J> static void setOutSource(int d) {
    OutSlot &s = outputs.Slot(J);
    s.source = (uint8_t)constrain((int)s.source + StepDir(d), 0, (int)RegSourceLength - 1);
    MarkUnsaved();
}

template <int J> static String getOutType() {
    return String(OutTypeNames[outputs.Slot(J).type]);
}
template <int J> static void setOutType(int d) {
    OutSlot &s = outputs.Slot(J);
    const uint8_t was = s.type;
    s.type = (uint8_t)constrain((int)s.type + StepDir(d), 0, (int)OutTypeLength - 1);
    if (s.type == was) {
        return;
    }
    // The two contextual rows mean different things per type, so carry sensible
    // values across rather than reinterpreting the old ones — a SLEW of 20 read
    // as a TRIG width of 40 ms is not what anyone asked for.
    switch (s.type) {
    case OutNote:
        s.depth = 5;
        s.param = 2;
        s.param2 = 0;
        break;
    case OutMod:
        s.depth = 8;
        s.param = 100;
        s.param2 = 0;
        break;
    case OutGate:
        s.depth = 1;
        s.param = 50;
        s.param2 = 0;
        break;
    case OutTrig:
        s.depth = 3;
        s.param = 50;
        s.param2 = 5;
        break;
    default:
        break;
    }
    MarkUnsaved();
}

template <int J> static String getOutDepth() {
    return String(outputs.Slot(J).depth);
}
template <int J> static void setOutDepth(int d) {
    OutSlot &s = outputs.Slot(J);
    s.depth = (uint8_t)constrain((int)s.depth + StepDir(d), 1, WEA_MAX_DEPTH);
    MarkUnsaved();
}

template <int J> static String getOutRotate() {
    return String(outputs.Slot(J).rotate);
}
template <int J> static void setOutRotate(int d) {
    OutSlot &s = outputs.Slot(J);
    // AB addresses 32 positions, A and B 16. Wrapping rather than clamping: the
    // ring has no end, and stopping at 15 would be a lie about the topology.
    const int span = (s.source == SrcAB) ? 32 : 16;
    s.rotate = (uint8_t)(((int)s.rotate + StepDir(d) + span) % span);
    MarkUnsaved();
}

// Contextual row 1: RANGE / LEVEL / THRESH depending on TYPE.
template <int J> static String getOutParam() {
    const OutSlot &s = outputs.Slot(J);
    switch (s.type) {
    case OutNote:
        return String(s.param) + "oct";
    case OutMod:
        return String(s.param) + "%";
    default:
        return String(s.param) + "%";
    }
}
template <int J> static void setOutParam(int d) {
    OutSlot &s = outputs.Slot(J);
    if (s.type == OutNote) {
        s.param = (uint8_t)constrain((int)s.param + StepDir(d), 1, QUANT_OCTAVES);
    } else {
        s.param = (uint8_t)constrain((int)s.param + d, 0, 100);
    }
    MarkUnsaved();
}
template <int J> static const char *outParamLabel() {
    switch (outputs.Slot(J).type) {
    case OutNote:
        return "RANGE";
    case OutMod:
        return "LEVEL";
    default:
        return "THRESH";
    }
}

// Contextual row 2: SLEW / WIDTH, and nothing at all for GATE.
template <int J> static String getOutParam2() {
    const OutSlot &s = outputs.Slot(J);
    if (s.type == OutTrig) {
        return String((int)s.param2 * 2) + "ms";
    }
    if (s.type == OutGate) {
        return String("-");
    }
    return String(s.param2) + "%";
}
template <int J> static void setOutParam2(int d) {
    OutSlot &s = outputs.Slot(J);
    if (s.type == OutGate) {
        return; // a gate has no second parameter; the row shows "-"
    }
    if (s.type == OutTrig) {
        s.param2 = (uint8_t)constrain((int)s.param2 + StepDir(d), 1, 100);
    } else {
        s.param2 = (uint8_t)constrain((int)s.param2 + d, 0, 100);
    }
    MarkUnsaved();
}
template <int J> static const char *outParam2Label() {
    return outputs.Slot(J).type == OutTrig ? "WIDTH" : "SLEW";
}

// ── CV IN ────────────────────────────────────────────────────
template <int I> static String getCvTarget() { return String(CVTargetNames[cvTarget[I]]); }
template <int I> static void setCvTarget(int d) {
    cvTarget[I] = (uint8_t)constrain((int)cvTarget[I] + StepDir(d), 0,
                                     (int)CVTargetLength - 1);
    MarkUnsaved();
}
template <int I> static String getCvDepth() { return String(cvDepth[I]) + "%"; }
template <int I> static void setCvDepth(int d) {
    cvDepth[I] = (uint8_t)constrain((int)cvDepth[I] + d, 0, 100);
    MarkUnsaved();
}

// ── SETTINGS ─────────────────────────────────────────────────
static const char *const screenTimeoutNames[] = {"OFF", "2s", "5s", "10s", "20s"};
static String getTimeout() {
    return String(screenTimeoutNames[constrain(menuScreenTimeout, 0, 4)]);
}
static void setTimeout(int d) {
    menuScreenTimeout = constrain(menuScreenTimeout + StepDir(d), 0, 4);
    static const unsigned long kTimeoutOpts[] = {0, 2000, 5000, 10000, 20000};
    displayMgr.SetMenuTimeout(kTimeoutOpts[menuScreenTimeout]);
    MarkUnsaved();
}

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

static void actRandom() {
    RandomizeParams((uint32_t)micros());
    MarkUnsaved();
    ShowTemporaryMessage("RANDOM", 700);
}

// ─────────────────────────────────────────────────────────────
// MENU_ITEMS[]
// { label, valueFn, valueFn2, col1x, col2x, group, rowStyle, type, setter, action }
// ─────────────────────────────────────────────────────────────
const MenuItem MENU_ITEMS[] = {
    // ── 0 HOME ──
    {"HOME", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_ACTION, nullptr, nullptr},

    // ── 1 REG A ──
    // LENGTH and CHANCE carry the live flag: LENGTH is visible on the loom as
    // where the squares turn into dots, and CHANCE as how fast the pattern
    // churns. Both are much easier to set by watching than by reading.
    {"LENGTH", getLength<0>, nullptr, 88, 0, 1, ROW_SINGLE, MENU_EDIT, setLength<0>, nullptr, true},
    {"CHANCE", getChance<0>, nullptr, 88, 0, 1, ROW_SINGLE, MENU_EDIT, setChance<0>, nullptr, true},
    {"RANDOMIZE", nullptr, nullptr, 0, 0, 1, ROW_ACTION, MENU_ACTION, nullptr, actRandomize<0>},
    {"INVERT", nullptr, nullptr, 0, 0, 1, ROW_ACTION, MENU_ACTION, nullptr, actInvert<0>},
    {"CLEAR", nullptr, nullptr, 0, 0, 1, ROW_ACTION, MENU_ACTION, nullptr, actClear<0>},
    {"FILL", nullptr, nullptr, 0, 0, 1, ROW_ACTION, MENU_ACTION, nullptr, actFill<0>},

    // ── 2 REG B ──
    {"LENGTH", getLength<1>, nullptr, 88, 0, 2, ROW_SINGLE, MENU_EDIT, setLength<1>, nullptr, true},
    {"CHANCE", getChance<1>, nullptr, 88, 0, 2, ROW_SINGLE, MENU_EDIT, setChance<1>, nullptr, true},
    {"RANDOMIZE", nullptr, nullptr, 0, 0, 2, ROW_ACTION, MENU_ACTION, nullptr, actRandomize<1>},
    {"INVERT", nullptr, nullptr, 0, 0, 2, ROW_ACTION, MENU_ACTION, nullptr, actInvert<1>},
    {"CLEAR", nullptr, nullptr, 0, 0, 2, ROW_ACTION, MENU_ACTION, nullptr, actClear<1>},
    {"FILL", nullptr, nullptr, 0, 0, 2, ROW_ACTION, MENU_ACTION, nullptr, actFill<1>},

    // ── 3 WEAVE ──
    // The module's signature control, and the one the screen draws literally:
    // the strand crossings in the channel ARE this number.
    {"AMOUNT", getWeave, nullptr, 88, 0, 3, ROW_SINGLE, MENU_EDIT, setWeave, nullptr, true},
    {"DIR", getWeaveDir, nullptr, 88, 0, 3, ROW_SINGLE, MENU_EDIT, setWeaveDir, nullptr, true},

    // ── 4 CLOCK ──
    // "IN PPQN" rather than "PPQN": it describes the clock ARRIVING at IN 1 —
    // how many pulses that source sends per beat — and does nothing at all when
    // the internal clock is running. RATE is the one that sets the step rate.
    {"BPM", getBpm, nullptr, 88, 0, 4, ROW_SINGLE, MENU_EDIT, setBpm, nullptr, true},
    {"IN PPQN", getPpqn, nullptr, 88, 0, 4, ROW_SINGLE, MENU_EDIT, setPpqn, nullptr},
    {"RATE", getRate, nullptr, 88, 0, 4, ROW_SINGLE, MENU_EDIT, setRate, nullptr, true},

    // ── 5 SCALE ──
    {"ROOT", getRoot, nullptr, 88, 0, 5, ROW_SINGLE, MENU_EDIT, setRoot, nullptr},
    {"SCALE", getScale, nullptr, 82, 0, 5, ROW_SINGLE, MENU_EDIT, setScale, nullptr},
    {"TRANSPOSE", getTranspose, nullptr, 88, 0, 5, ROW_SINGLE, MENU_EDIT, setTranspose, nullptr},

    // ── 6 ROUTING ──
    // One row on its own page, deliberately. It is the module's identity switch
    // — two voices, one voice plus modulation, or four drum tracks — and burying
    // it under five neighbours would hide the thing most people want first.
    {"ROUTING", getRouting, nullptr, 76, 0, 6, ROW_SINGLE, MENU_EDIT, setRouting, nullptr},

    // ── 7..10 OUT 1..4 ── (exactly six rows each — the page limit)
    //
    // DEPTH and ROTATE carry the live flag; the other four fields do not, and
    // the line is whether the loom can SHOW the change. ROTATE walks the jack's
    // digit along the row and DEPTH widens the underline beneath it, so both are
    // judged by looking. SOURCE, TYPE and the two contextual fields change what
    // the jack emits rather than where it reads — nothing on the loom moves, so
    // a preview would be a strip in front of a still picture.
    {"SOURCE", getOutSource<0>, nullptr, 88, 0, 7, ROW_SINGLE, MENU_EDIT, setOutSource<0>, nullptr},
    {"TYPE", getOutType<0>, nullptr, 88, 0, 7, ROW_SINGLE, MENU_EDIT, setOutType<0>, nullptr},
    {"DEPTH", getOutDepth<0>, nullptr, 88, 0, 7, ROW_SINGLE, MENU_EDIT, setOutDepth<0>, nullptr, true},
    {"ROTATE", getOutRotate<0>, nullptr, 88, 0, 7, ROW_SINGLE, MENU_EDIT, setOutRotate<0>, nullptr, true},
    {"", getOutParam<0>, nullptr, 88, 0, 7, ROW_SINGLE, MENU_EDIT, setOutParam<0>, nullptr},
    {"", getOutParam2<0>, nullptr, 88, 0, 7, ROW_SINGLE, MENU_EDIT, setOutParam2<0>, nullptr},

    {"SOURCE", getOutSource<1>, nullptr, 88, 0, 8, ROW_SINGLE, MENU_EDIT, setOutSource<1>, nullptr},
    {"TYPE", getOutType<1>, nullptr, 88, 0, 8, ROW_SINGLE, MENU_EDIT, setOutType<1>, nullptr},
    {"DEPTH", getOutDepth<1>, nullptr, 88, 0, 8, ROW_SINGLE, MENU_EDIT, setOutDepth<1>, nullptr, true},
    {"ROTATE", getOutRotate<1>, nullptr, 88, 0, 8, ROW_SINGLE, MENU_EDIT, setOutRotate<1>, nullptr, true},
    {"", getOutParam<1>, nullptr, 88, 0, 8, ROW_SINGLE, MENU_EDIT, setOutParam<1>, nullptr},
    {"", getOutParam2<1>, nullptr, 88, 0, 8, ROW_SINGLE, MENU_EDIT, setOutParam2<1>, nullptr},

    {"SOURCE", getOutSource<2>, nullptr, 88, 0, 9, ROW_SINGLE, MENU_EDIT, setOutSource<2>, nullptr},
    {"TYPE", getOutType<2>, nullptr, 88, 0, 9, ROW_SINGLE, MENU_EDIT, setOutType<2>, nullptr},
    {"DEPTH", getOutDepth<2>, nullptr, 88, 0, 9, ROW_SINGLE, MENU_EDIT, setOutDepth<2>, nullptr, true},
    {"ROTATE", getOutRotate<2>, nullptr, 88, 0, 9, ROW_SINGLE, MENU_EDIT, setOutRotate<2>, nullptr, true},
    {"", getOutParam<2>, nullptr, 88, 0, 9, ROW_SINGLE, MENU_EDIT, setOutParam<2>, nullptr},
    {"", getOutParam2<2>, nullptr, 88, 0, 9, ROW_SINGLE, MENU_EDIT, setOutParam2<2>, nullptr},

    {"SOURCE", getOutSource<3>, nullptr, 88, 0, 10, ROW_SINGLE, MENU_EDIT, setOutSource<3>, nullptr},
    {"TYPE", getOutType<3>, nullptr, 88, 0, 10, ROW_SINGLE, MENU_EDIT, setOutType<3>, nullptr},
    {"DEPTH", getOutDepth<3>, nullptr, 88, 0, 10, ROW_SINGLE, MENU_EDIT, setOutDepth<3>, nullptr, true},
    {"ROTATE", getOutRotate<3>, nullptr, 88, 0, 10, ROW_SINGLE, MENU_EDIT, setOutRotate<3>, nullptr, true},
    {"", getOutParam<3>, nullptr, 88, 0, 10, ROW_SINGLE, MENU_EDIT, setOutParam<3>, nullptr},
    {"", getOutParam2<3>, nullptr, 88, 0, 10, ROW_SINGLE, MENU_EDIT, setOutParam2<3>, nullptr},

    // ── 11 CV IN ──
    {"IN2 DEST", getCvTarget<0>, nullptr, 72, 0, 11, ROW_SINGLE, MENU_EDIT, setCvTarget<0>, nullptr},
    {"IN2 DEPTH", getCvDepth<0>, nullptr, 88, 0, 11, ROW_SINGLE, MENU_EDIT, setCvDepth<0>, nullptr},
    {"IN3 DEST", getCvTarget<1>, nullptr, 72, 0, 11, ROW_SINGLE, MENU_EDIT, setCvTarget<1>, nullptr},
    {"IN3 DEPTH", getCvDepth<1>, nullptr, 88, 0, 11, ROW_SINGLE, MENU_EDIT, setCvDepth<1>, nullptr},

    // ── 12 SETTINGS ──
    {"TIMEOUT", getTimeout, nullptr, 88, 0, 12, ROW_SINGLE, MENU_EDIT, setTimeout, nullptr},
    {"BOOT MENU", nullptr, nullptr, 0, 0, 12, ROW_ACTION, MENU_ACTION, nullptr, actBootMenu},

    // ── 13 PRESETS ──
    {"SLOT", getSlot, nullptr, 88, 0, 13, ROW_SINGLE, MENU_EDIT, setSlot, nullptr},
    {"SAVE", nullptr, nullptr, 0, 0, 13, ROW_ACTION, MENU_ACTION, nullptr, actSave},
    {"LOAD", nullptr, nullptr, 0, 0, 13, ROW_ACTION, MENU_ACTION, nullptr, actLoad},
    {"RANDOM", nullptr, nullptr, 0, 0, 13, ROW_ACTION, MENU_ACTION, nullptr, actRandom},
};

const int MENU_ITEM_COUNT = (int)(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]));

// The two contextual rows on each OUT page have no fixed label — what they mean
// follows TYPE. menuRender.hpp asks for the label at draw time rather than
// storing it, so switching a jack from NOTE to TRIG relabels the page with no
// state to keep in sync.
static const char *OutRowLabel(uint8_t group, int rowInGroup) {
    if (group < 7 || group > 10) {
        return nullptr;
    }
    switch (group) {
    case 7:
        return rowInGroup == 4 ? outParamLabel<0>() : outParam2Label<0>();
    case 8:
        return rowInGroup == 4 ? outParamLabel<1>() : outParam2Label<1>();
    case 9:
        return rowInGroup == 4 ? outParamLabel<2>() : outParam2Label<2>();
    default:
        return rowInGroup == 4 ? outParamLabel<3>() : outParam2Label<3>();
    }
}
