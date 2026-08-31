#pragma once
// ============================================================
// menuHandlers.hpp — setter/getter functions and MENU_ITEMS[].
// Included once from src/main.cpp after all other headers.
// All functions are static so the compiler can inline/DCE them
// freely even though their addresses are taken in the table.
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
//   menuDisplay.hpp      — low-level draw primitives (MD_Row, MD_TwoColRow …)
//   menuRender.hpp       — HandleDisplay() and group-specific renderers
//
// HOW ITEMS AND GROUPS WORK
// ──────────────────────────
// • Every entry in MENU_ITEMS[] is a "menu item" numbered 1…N (1-based).
//   Item number  =  array index + 1.
// • The global  menuItem  tracks which item is selected.
// • The global  menuMode  is 0 when navigating, or equals the item number
//   that is currently being edited.  Setter functions read it to determine
//   which sub-field to modify (e.g. CV ATTN vs OFF).
// • Items that share the same  group  field are displayed on the same page.
//   Encoder left/right navigation steps through menu items sequentially;
//   the page shown is always the group of the current item.
// • MENU_ITEM_COUNT is computed automatically from sizeof(MENU_ITEMS)/sizeof([0]).
//   Navigation and the scroll bar use it directly — no manual counter needed.
//
// MENU_ITEMS[] COLUMN GUIDE
// ──────────────────────────
//   { label, valueFn, valueFn2, col1x, col2x, group, rowStyle, type, setter, action }
//
//   label    — left-side text shown in every row style except ROW_HIDDEN.
//   valueFn  — returns the displayable value string; nullptr → no value shown.
//   valueFn2 — second value for ROW_TWOCOL; nullptr for all other styles.
//   col1x    — pixel X for valueFn  (used by ROW_TWOCOL; for ROW_SINGLE the
//              renderer uses col1x as the value X — set it to ~70-84).
//   col2x    — pixel X for valueFn2 (ROW_TWOCOL only; ignored otherwise).
//   group    — page number; items with the same group appear together.
//   rowStyle — ROW_SINGLE   : label left, value at col1x.
//              ROW_TWOCOL   : label left, two values at col1x / col2x.
//                             The *next* item is normally ROW_HIDDEN and
//                             handles the second column's setter.
//              ROW_HIDDEN   : item has no row on-screen; editor still works.
//                             Cursor + editing are handled by the TWOCOL row.
//              ROW_ACTION   : label only, no value.  Cursor is hollow triangle.
//   type     — MENU_EDIT    : encoder rotate calls setter(±delta).
//              MENU_TOGGLE  : click calls action() directly; no edit mode.
//              MENU_ACTION  : click calls action() directly; no edit mode.
//   setter   — called with ±speedFactor when rotating encoder in edit mode.
//   action   — called on click for MENU_TOGGLE / MENU_ACTION.
//
// RENDERERS: GENERIC vs CUSTOM
// ──────────────────────────────
// Groups 1, 3, 6, 7, 8, 11, 12, 15, 16 use the GENERIC renderer (MD_RenderGroup
// in menuDisplay.hpp) — it iterates items by group and draws them using their
// rowStyle.  Adding items to these groups requires NO changes to rendering.
//
// Groups 0, 2, 4, 5, 9, 10, 13, 14 have CUSTOM renderers in menuRender.hpp
// because they need special layouts (pattern grids, column headers, shared
// rows, full-width text, etc).  If you add items to these groups or change
// item numbers within them, also update the corresponding renderer block.
//
// ══════════════════════════════════════════════════════════════╗
// RECIPE: ADD ITEMS TO A GENERIC GROUP (e.g. waveform Out 1/2) ║
// ══════════════════════════════════════════════════════════════╝
// Example: add "OUT 1 WAV" and "OUT 2 WAV" to group 8 (Output Settings).
//
// 1. Add getter functions (see existing getWav2/getWav3 below for the pattern):
//      static String getWav0() { return outputs[0].GetWaveformTypeDescription(); }
//      static String getWav1() { return outputs[1].GetWaveformTypeDescription(); }
//
// 2. Add setter functions (see existing setWaveform2/3 for the pattern):
//      static void setWaveform0(int d) { int dir=(d>0)?1:-1;
//          outputs[0].SetWaveformType(...); unsavedChanges=true; }
//      static void setWaveform1(int d) { ... }
//
// 3. Append rows to MENU_ITEMS[] inside group 8 (after item 43):
//      { "OUT 1 WAV:", getWav0, nullptr, 70, 0, 8, ROW_SINGLE, MENU_EDIT, setWaveform0, nullptr }, // 44 ← NEW
//      { "OUT 2 WAV:", getWav1, nullptr, 70, 0, 8, ROW_SINGLE, MENU_EDIT, setWaveform1, nullptr }, // 45 ← NEW
//    All subsequent item numbers shift up by 2.
//
// 4. No separate counter to update — MENU_ITEM_COUNT is computed automatically.
//
// 5. Fix hardcoded item-number checks in lib/menuRender.hpp that reference
//    item numbers ≥ 44 (the insertion point).  Search for numeric literals
//    like  `== 49`, `<= 52`, `== 53`, `== 55`.  Shift each by 2.
//    Also update the group 12 item checks (62–66 become 64–68).
//
// ═════════════════════════════════════════════════════════╗
// RECIPE: ADD A NEW PAGE (new group)                       ║
// ═════════════════════════════════════════════════════════╝
// 1. Choose a new group number (next free integer after 12 → use 13).
// 2. Add getter/setter functions.
// 3. Append items at the end of MENU_ITEMS[] with the new group number.
// 4. No separate counter to update — MENU_ITEM_COUNT is automatic.
// 5. Add the page title to the groupTitles[] array in lib/menuRender.hpp:
//      "", "", ..., "MY NEW PAGE"   // index = group number
// 6. If the group needs a custom layout, add an  if (grp == 13) { … return; }
//    block in HandleDisplay() inside menuRender.hpp, before the generic
//    fallback.  Otherwise the generic renderer handles it automatically.
//
// ═══════════════════════════════════════════════════════════╗
// RECIPE: MOVE A GROUP TO A DIFFERENT MENU POSITION         ║
// ═══════════════════════════════════════════════════════════╝
// Groups are identified by the group field — the on-screen order is
// determined by item number order in MENU_ITEMS[].  To move a group:
//
// 1. Cut the item rows for that group from MENU_ITEMS[] and paste them
//    at the desired position (encoder navigates in array order).
// 2. Reassign sequential item numbers in the comments (they must be
//    strictly 1…N without gaps — renumber all shifted items).
// 3. Update all hardcoded item-number literals in menuRender.hpp
//    (search the file for  == <number>  and  <= <number>).
// 4. The group field itself does NOT need to change (it just identifies
//    which items share a page; it is independent of display order).
// 5. MENU_ITEM_COUNT updates automatically; no manual counter to keep.
//
// ═══════════════════════════════════════════════════════════╗
// RECIPE: ADD A ROW_TWOCOL ITEM PAIR (e.g. two linked params)║
// ═══════════════════════════════════════════════════════════╝
// Pattern: one ROW_TWOCOL item + one ROW_HIDDEN item with the same label,
// valueFn/valueFn2, and col1x/col2x.  The TWOCOL item handles colA's setter;
// the HIDDEN item handles colB's setter.  The renderer sel-highlights both
// when either is active.  See items 22/23 (Swing) for a live example.
//
// ────────────────────────────────────────────────────────────

#include "clockEngine.hpp"
#include "cvInputs.hpp"
#include "menuDefinitions.hpp"
#include "outputs.hpp"
#include "presetManager.hpp" // saveSlot, NUM_SLOTS, CollectParams, UpdateParameters, LoadDefaultParams
#include "storage.hpp"       // Save(), Load()

// ---- Externs from src/main.cpp --------------------------------
extern bool unsavedChanges;
extern int euclideanOutputSelect;
extern int envelopeOutputSelect;
extern int quantizerOutputSelect;
extern int loopOutputSelect;
extern int menuScreenTimeout;
extern int menuMode;

// Functions defined later in main.cpp — forward declarations only.
extern void ShowTemporaryMessage(const char *msg, uint32_t durationMs = 1000);
extern void ToggleMasterState();

// ================================================================
// Group 1 — BPM / transport  (items 1–2)
// ================================================================

static void setBPM(int d) {
    BPM = (unsigned int)constrain((int)BPM + d, (int)minBPM, (int)maxBPM);
    lastInternalBPM = BPM;
    bpmNeedsUpdate = true;
    unsavedChanges = true;
}
// item 2: ToggleMasterState() — extern above

// ================================================================
// Group 2 — Clock dividers  (items 3–7)
// ================================================================

static void setExtDivider(int d) {
    externalDividerIndex = constrain(externalDividerIndex + d, 0, dividerAmount - 1);
    unsavedChanges = true;
}

// ================================================================
// Group 3 — Output state toggles  (items 8–11)
// ================================================================

// ================================================================
// Group 4 — Pulse probability  (items 12–15)
// ================================================================

// ================================================================
// Group 5 — Euclidean rhythm  (items 16–21)
// ================================================================

static void setEuclideanOutputSel(int d) {
    int dir = (d > 0) ? 1 : -1;
    euclideanOutputSelect = (euclideanOutputSelect + dir + ActiveOutputs()) % ActiveOutputs();
    unsavedChanges = true;
}
static void toggleEuclidean() {
    outputs[euclideanOutputSelect].ToggleEuclidean();
    unsavedChanges = true;
}
static void setEuclideanSteps(int d) {
    outputs[euclideanOutputSelect].SetEuclideanSteps(outputs[euclideanOutputSelect].GetEuclideanSteps() + d);
    unsavedChanges = true;
}
static void setEuclideanTrig(int d) {
    outputs[euclideanOutputSelect].SetEuclideanTriggers(outputs[euclideanOutputSelect].GetEuclideanTriggers() + d);
    unsavedChanges = true;
}
static void setEuclideanRot(int d) {
    outputs[euclideanOutputSelect].SetEuclideanRotation(outputs[euclideanOutputSelect].GetEuclideanRotation() + d);
    unsavedChanges = true;
}
static void setEuclideanPad(int d) {
    outputs[euclideanOutputSelect].SetEuclideanPadding(outputs[euclideanOutputSelect].GetEuclideanPadding() + d);
    unsavedChanges = true;
}

// ================================================================
// Group 6 — Swing  (items 22–29)
// ================================================================

// ================================================================
// Group 7 — Phase shift  (items 30–33)
// ================================================================

// ================================================================
// Group 8 — Duty cycle  (items 34–37)
// ================================================================

// ================================================================
// Group 8 — Waveform (all 4 outputs)  (items 8–11)
// ================================================================

// Waveform scroll: ignore speedFactor magnitude — use direction only (preserves original ±1 wrap)
// ================================================================
// Group 13 — Level / Offset (all 4 outputs)  (items 12–19)
// ================================================================

// ================================================================
// Group 10 — Envelope settings  (items 44–50)
// ================================================================

// Envelope output select wraps across all 4 outputs (indices 0–3).
static void setEnvOutputSel(int d) {
    int dir = (d > 0) ? 1 : -1;
    envelopeOutputSelect = (envelopeOutputSelect + dir + ActiveOutputs()) % ActiveOutputs();
    unsavedChanges = true;
}
// Attack/decay/release receive delta × 2 to match the original ×2 speedFactor scale.
static void setAttack(int d) {
    outputs[envelopeOutputSelect].SetAttack(outputs[envelopeOutputSelect].GetAttack() + d * 2);
    unsavedChanges = true;
}
static void setDecay(int d) {
    outputs[envelopeOutputSelect].SetDecay(outputs[envelopeOutputSelect].GetDecay() + d * 2);
    unsavedChanges = true;
}
static void setSustain(int d) {
    outputs[envelopeOutputSelect].SetSustain(outputs[envelopeOutputSelect].GetSustain() + d);
    unsavedChanges = true;
}
static void setRelease(int d) {
    outputs[envelopeOutputSelect].SetRelease(outputs[envelopeOutputSelect].GetRelease() + d * 2);
    unsavedChanges = true;
}
static void setCurve(int d) {
    outputs[envelopeOutputSelect].SetCurve(outputs[envelopeOutputSelect].GetCurve() + d * 0.01f);
    unsavedChanges = true;
}
static void toggleRetrigger() {
    outputs[envelopeOutputSelect].ToggleRetrigger();
    unsavedChanges = true;
}

// ================================================================
// Group 11 — CV inputs  (items 51–56)
// ================================================================

// Target scroll: direction only, wrapping, skipping any target already taken
// by another CV input (None is always available). Walks rather than stepping
// twice, so it stays correct with three inputs — the old pair of setters
// assumed exactly one other channel to collide with.
static bool _cvTargetTakenElsewhere(int ch, CVTarget t) {
    if (t == CVTarget::None)
        return false;
    for (int i = 0; i < ActiveCvIns(); i++)
        if (i != ch && pendingCVInputTarget[i] == t)
            return true;
    return false;
}
static void setCVTargetCh(int ch, int d) {
    const int n = CVTargetCount();
    const int dir = (d > 0) ? 1 : -1;
    int t = pendingCVInputTarget[ch];
    for (int step = 0; step < n; step++) {
        t = (t + n + dir) % n;
        if (!_cvTargetTakenElsewhere(ch, static_cast<CVTarget>(t)))
            break;
    }
    pendingCVInputTarget[ch] = static_cast<CVTarget>(t);
}
static void setCVTarget0(int d) { setCVTargetCh(0, d); }
static void setCVTarget1(int d) { setCVTargetCh(1, d); }
static void setCVTarget2(int d) { setCVTargetCh(2, d); }
// Note: attenuation/offset do not set unsavedChanges — preserving original behaviour.
static void setCVAttn0(int d) { CVInputAttenuation[0] = constrain(CVInputAttenuation[0] + d, 0, 100); }
static void setCVOffset0(int d) { CVInputOffset[0] = constrain(CVInputOffset[0] + d, 0, 100); }
static void setCVAttn1(int d) { CVInputAttenuation[1] = constrain(CVInputAttenuation[1] + d, 0, 100); }
static void setCVOffset1(int d) { CVInputOffset[1] = constrain(CVInputOffset[1] + d, 0, 100); }
static void setCVAttn2(int d) { CVInputAttenuation[2] = constrain(CVInputAttenuation[2] + d, 0, 100); }
static void setCVOffset2(int d) { CVInputOffset[2] = constrain(CVInputOffset[2] + d, 0, 100); }

// ================================================================
// Group 12 — Quantizer  (items 57–61)
// ================================================================

// Quantizer output select wraps across all 4 outputs (indices 0–3).
static void setQtzOutputSel(int d) {
    int dir = (d > 0) ? 1 : -1;
    quantizerOutputSelect = (quantizerOutputSelect + dir + ActiveOutputs()) % ActiveOutputs();
    unsavedChanges = true;
}
static void toggleQuantizer() {
    outputs[quantizerOutputSelect].ToggleQuantizer();
    unsavedChanges = true;
}
static void setQtzNote(int d) {
    outputs[quantizerOutputSelect].SetQuantizerNoteIndex(outputs[quantizerOutputSelect].GetQuantizerNoteIndex() + d);
    unsavedChanges = true;
}
static void setQtzScale(int d) {
    outputs[quantizerOutputSelect].SetQuantizerScaleIndex(outputs[quantizerOutputSelect].GetQuantizerScaleIndex() + d);
    unsavedChanges = true;
}
static void setQtzOctave(int d) {
    outputs[quantizerOutputSelect].SetQuantizerOctaveShift(outputs[quantizerOutputSelect].GetQuantizerOctaveShift() + d);
    unsavedChanges = true;
}

// ================================================================
// Group 13 — Settings / save-load  (items 62–66)
// ================================================================

// Save-slot scroll wraps within [0, NUM_SLOTS-1] — the range Save()/Load()
// actually accept. It used to wrap over [0, NUM_SLOTS] inclusive, which offered
// a slot one past the end that both functions rejected on their bounds check,
// so saving to the last displayed slot quietly did nothing.
static void setSaveSlot(int d) {
    int dir = (d > 0) ? 1 : -1;
    saveSlot = (saveSlot + dir + NUM_SLOTS) % NUM_SLOTS;
}

static void actionSave() {
    Save(CollectParams(), saveSlot);
    unsavedChanges = false;
    ShowTemporaryMessage("SAVED");
}
static void actionLoad() {
    LoadSaveParams p = Load(saveSlot);
    UpdateParameters(p);
    unsavedChanges = false;
    ShowTemporaryMessage("LOADED");
}
static void actionLoadDefaults() {
    LoadSaveParams p = LoadDefaultParams();
    UpdateParameters(p);
    unsavedChanges = false;
    ShowTemporaryMessage("LOADED");
}

// ================================================================
// Group 14 — Cross operations  (items 74–81)
// ================================================================

// Op/source scroll: direction only, wraps within the respective list.
// ================================================================
// Group 15 — Loops  (items 82–86)
// ================================================================

// Loop output select wraps across all 4 outputs (indices 0–3).
static void setLoopOutputSel(int d) {
    int dir = (d > 0) ? 1 : -1;
    loopOutputSelect = (loopOutputSelect + dir + ActiveOutputs()) % ActiveOutputs();
    unsavedChanges = true;
}
static void setLoopBeats(int d) {
    outputs[loopOutputSelect].SetLoopBeats(outputs[loopOutputSelect].GetLoopBeats() + d);
    unsavedChanges = true;
}
static void setLoopWake(int d) {
    outputs[loopOutputSelect].SetLoopWake(outputs[loopOutputSelect].GetLoopWake() + d);
    unsavedChanges = true;
}
static void setLoopNap(int d) {
    outputs[loopOutputSelect].SetLoopNap(outputs[loopOutputSelect].GetLoopNap() + d);
    unsavedChanges = true;
}
static void setLoopShift(int d) {
    outputs[loopOutputSelect].SetLoopShift(outputs[loopOutputSelect].GetLoopShift() + d);
    unsavedChanges = true;
}

// ── Per-output accessors ─────────────────────────────────────────────────────
// One template per parameter, instantiated per output, rather than one function
// per (parameter, output) pair. Thirteen parameters across eight outputs is 104
// near-identical functions written out by hand; it was already 52 before the
// expander, and every one of them differed from its neighbours only in a single
// digit. ChaosForge's menu uses the same shape.
//
// An instantiation is an ordinary function pointer, so MENU_ITEMS[] takes
// getDiv<0> / setDiv<0> exactly as it took getDiv0 / setDiv0.
//
// N is a 0-based output index and must be < NUM_MAX_OUTPUTS. Rows for outputs
// 5-8 are only reachable when an expander is enabled — see MenuItemEnabled().

template <int N> static String getDiv() { return outputs[N].GetDividerDescription(); }
template <int N> static void setDiv(int d) {
    // An envelope output's divider is locked to the "Env" slot.
    if (outputs[N].IsEnvelopeType())
        return;
    outputs[N].SetDivider(outputs[N].GetDividerIndex() + d);
    unsavedChanges = true;
}

template <int N> static String getWav() { return outputs[N].GetWaveformTypeDescription(); }
template <int N> static void setWav(int d) {
    const int dir = (d > 0) ? 1 : -1;
    outputs[N].SetWaveformType(static_cast<WaveformType>(
        (outputs[N].GetWaveformType() + WaveformTypeLength + dir) % WaveformTypeLength));
    unsavedChanges = true;
}

template <int N> static String getLvl() { return outputs[N].GetLevelDescription(); }
template <int N> static void setLvl(int d) {
    outputs[N].SetLevel(outputs[N].GetLevel() + d);
    unsavedChanges = true;
}
template <int N> static String getOff() { return outputs[N].GetOffsetDescription(); }
template <int N> static void setOff(int d) {
    outputs[N].SetOffset(outputs[N].GetOffset() + d);
    unsavedChanges = true;
}

template <int N> static String getState() { return outputs[N].GetOutputState() ? "ON" : "OFF"; }
template <int N> static void toggleState() {
    outputs[N].ToggleOutputState();
    unsavedChanges = true;
}
template <int N> static String getInv() { return outputs[N].GetInvertDescription(); }
template <int N> static void toggleInv() {
    outputs[N].ToggleInvert();
    unsavedChanges = true;
}

template <int N> static String getProb() { return outputs[N].GetPulseProbabilityDescription(); }
template <int N> static void setProb(int d) {
    outputs[N].SetPulseProbability(outputs[N].GetPulseProbability() + d);
    unsavedChanges = true;
}

template <int N> static String getSwingAmt() { return outputs[N].GetSwingAmountDescription(); }
template <int N> static void setSwingAmt(int d) {
    outputs[N].SetSwingAmount(outputs[N].GetSwingAmountIndex() + d);
    unsavedChanges = true;
}
// Shared buffer: the renderer draws one value at a time, which is the same
// assumption every other buffered getter in this file makes.
static char _swingEvBuf[4];
template <int N> static String getSwingEv() {
    snprintf(_swingEvBuf, sizeof(_swingEvBuf), "%d", outputs[N].GetSwingEvery());
    return _swingEvBuf;
}
template <int N> static void setSwingEv(int d) {
    outputs[N].SetSwingEvery(outputs[N].GetSwingEvery() + d);
    unsavedChanges = true;
}

template <int N> static String getPhase() { return outputs[N].GetPhaseDescription(); }
template <int N> static void setPhase(int d) {
    outputs[N].SetPhase(outputs[N].GetPhase() + d);
    unsavedChanges = true;
}
template <int N> static String getDuty() { return outputs[N].GetDutyCycleDescription(); }
template <int N> static void setDuty(int d) {
    outputs[N].SetDutyCycle(outputs[N].GetDutyCycle() + d);
    unsavedChanges = true;
}

template <int N> static String getCrossOp() { return outputs[N].GetCrossOpDescription(); }
template <int N> static void setCrossOp(int d) {
    const int dir = (d > 0) ? 1 : -1;
    outputs[N].SetCrossOp((outputs[N].GetCrossOpIndex() + CrossOpLength + dir) % CrossOpLength);
    unsavedChanges = true;
}
template <int N> static String getCrossSrc() { return outputs[N].GetCrossSourceDescription(); }
template <int N> static void setCrossSrc(int d) {
    const int dir = (d > 0) ? 1 : -1;
    outputs[N].SetCrossSource((outputs[N].GetCrossSourceIndex() + CrossSourceLength + dir) % CrossSourceLength);
    unsavedChanges = true;
}

// ================================================================
// Menu item identities
// ================================================================
// A handful of items have to be recognised by number rather than by their
// MenuItem fields: renderers that lay two items out on one row, and the CV
// target rows, which edit through a pending copy committed on the way out of
// edit mode.
//
// THESE ARE THE ONLY PLACE AN ITEM NUMBER IS WRITTEN DOWN. They used to be bare
// literals scattered over four files — menuRender.hpp, this file, clk_app.cpp
// and the Rack port's fw_engine.cpp — so reordering the menu meant finding all
// sixteen of them, and two of the four files are host-specific, meaning a miss
// showed up in only one of the two builds.
//
// Item numbers are 1-based and must match the comments in MENU_ITEMS[] below,
// where each of these rows is tagged with the constant that names it.
static constexpr int MI_EUC_ROT = 112;         // group 4, shares a row with PAD
static constexpr int MI_EUC_PAD = 113;
static constexpr int MI_ENV_CURVE = 124;       // group 9, shares a row with RETRIG
static constexpr int MI_ENV_RETRIG = 125;
static constexpr int MI_CV_TARGET_FIRST = 131; // group 10, one row per CV input
static constexpr int MI_CV_TARGET_LAST = 133;  // ...the third is the expander's
static constexpr int MI_CV_ATTN_FIRST = 134;   // first ATTN/OFF pair row

// The eight pages that exist only when an expander is fitted: one per
// per-output page in the 3-55 block, for outputs 5-8.
static constexpr uint8_t MI_GROUP_EXP_FIRST = 17;
static constexpr uint8_t MI_GROUP_EXP_LAST = 24;

// Is `item` one of the CV target rows, and if so which input? Returns -1 when
// it is not. Range-based rather than a list of equality tests so that adding
// the expander's IN 4 row is a change to MI_CV_TARGET_LAST and nothing else.
static inline int CVTargetItemChannel(int item) {
    return (item >= MI_CV_TARGET_FIRST && item <= MI_CV_TARGET_LAST)
               ? item - MI_CV_TARGET_FIRST
               : -1;
}

// ================================================================
// Value getter functions — return const char* for display.
// Each uses a static char buffer (safe: only one is live at a time
// since they are called sequentially during display rendering).
// ================================================================

// ── Group 1: BPM / transport ─────────────────────────────────
// (BPM display is a fully custom renderer — no getter needed)

// ── Group 2: Clock dividers ──────────────────────────────────
static String getExtDiv() {
    return externalDividerDescription[externalDividerIndex];
}

// ── Group 3: Output state ────────────────────────────────────
// ── Group 4: Pulse probability ───────────────────────────────
// ── Group 5: Euclidean rhythm ────────────────────────────────
static char _eucBuf[4];
static String getEucSel() {
    snprintf(_eucBuf, sizeof(_eucBuf), "%d", euclideanOutputSelect + 1);
    return _eucBuf;
}
static String getEucEn() { return outputs[euclideanOutputSelect].GetEuclidean() ? "YES" : "NO"; }
static String getEucSteps() {
    snprintf(_eucBuf, sizeof(_eucBuf), "%d", outputs[euclideanOutputSelect].GetEuclideanSteps());
    return _eucBuf;
}
static String getEucTrig() {
    snprintf(_eucBuf, sizeof(_eucBuf), "%d", outputs[euclideanOutputSelect].GetEuclideanTriggers());
    return _eucBuf;
}
static String getEucRot() {
    snprintf(_eucBuf, sizeof(_eucBuf), "%d", outputs[euclideanOutputSelect].GetEuclideanRotation());
    return _eucBuf;
}
static String getEucPad() {
    snprintf(_eucBuf, sizeof(_eucBuf), "%d", outputs[euclideanOutputSelect].GetEuclideanPadding());
    return _eucBuf;
}

// ── Group 6: Swing ───────────────────────────────────────────
// ── Group 7: Phase shift ─────────────────────────────────────
// ── Group 8: Duty cycle ──────────────────────────────────────
// ── Group 8: Waveform (all 4) ────────────────────────────────
// ── Group 13: Level / Offset (all 4) ─────────────────────────
// ── Group 10: Envelope ───────────────────────────────────────
static char _envBuf[8];
static String getEnvSel() {
    snprintf(_envBuf, sizeof(_envBuf), "%d", envelopeOutputSelect + 1);
    return _envBuf;
}
static String getAttack() { return outputs[envelopeOutputSelect].GetAttackDescription(); }
static String getDecay() { return outputs[envelopeOutputSelect].GetDecayDescription(); }
static String getSustain() { return outputs[envelopeOutputSelect].GetSustainDescription(); }
static String getRelease() { return outputs[envelopeOutputSelect].GetReleaseDescription(); }
static String getCurve() { return outputs[envelopeOutputSelect].GetCurveDescription(); }
static String getRetrig() { return outputs[envelopeOutputSelect].GetRetriggerDescription(); }

// ── Group 11: CV inputs ──────────────────────────────────────
// While the row is being edited the pending copy is what the user is scrolling
// through; once committed, the live target is what to show.
static String getCVTgtCh(int ch) {
    const bool editing = (menuMode == MI_CV_TARGET_FIRST + ch);
    return CVTargetName(editing ? pendingCVInputTarget[ch] : CVInputTarget[ch]);
}
static String getCVTgt0() { return getCVTgtCh(0); }
static String getCVTgt1() { return getCVTgtCh(1); }
static String getCVTgt2() { return getCVTgtCh(2); }
static char _cvBuf[8];
static String getCVAttn0() {
    snprintf(_cvBuf, sizeof(_cvBuf), "%d%%", CVInputAttenuation[0]);
    return _cvBuf;
}
static String getCVOff0() {
    snprintf(_cvBuf, sizeof(_cvBuf), "%d%%", CVInputOffset[0]);
    return _cvBuf;
}
static String getCVAttn1() {
    snprintf(_cvBuf, sizeof(_cvBuf), "%d%%", CVInputAttenuation[1]);
    return _cvBuf;
}
static String getCVOff1() {
    snprintf(_cvBuf, sizeof(_cvBuf), "%d%%", CVInputOffset[1]);
    return _cvBuf;
}
static String getCVAttn2() {
    snprintf(_cvBuf, sizeof(_cvBuf), "%d%%", CVInputAttenuation[2]);
    return _cvBuf;
}
static String getCVOff2() {
    snprintf(_cvBuf, sizeof(_cvBuf), "%d%%", CVInputOffset[2]);
    return _cvBuf;
}

// ── Group 12: Quantizer ──────────────────────────────────────
static char _qtzBuf[4];
static String getQtzSel() {
    snprintf(_qtzBuf, sizeof(_qtzBuf), "%d", quantizerOutputSelect + 1);
    return _qtzBuf;
}
static String getQtzEn() { return outputs[quantizerOutputSelect].GetQuantizerEnable() ? "YES" : "NO"; }
static String getQtzNote() { return outputs[quantizerOutputSelect].GetQuantizerNoteDescription(); }
static String getQtzScale() { return outputs[quantizerOutputSelect].GetQuantizerScaleDescription(); }
static char _qtzOctBuf[8];
static String getQtzOct() {
    outputs[quantizerOutputSelect].GetQuantizerOctaveShiftDescription().toCharArray(_qtzOctBuf, sizeof(_qtzOctBuf));
    return _qtzOctBuf;
}

// ── Group 16: Presets ────────────────────────────────────────
static char _slotBuf[4];
static String getSaveSlot() {
    snprintf(_slotBuf, sizeof(_slotBuf), "%d", saveSlot);
    return _slotBuf;
}

// ── Group 14: Cross operations ───────────────────────────────
// ── Group 15: Loops ──────────────────────────────────────────
static char _loopSelBuf[4];
static String getLoopSel() {
    snprintf(_loopSelBuf, sizeof(_loopSelBuf), "%d", loopOutputSelect + 1);
    return _loopSelBuf;
}
static String getLoopBeats() { return outputs[loopOutputSelect].GetLoopBeatsDescription(); }
static String getLoopWake() { return outputs[loopOutputSelect].GetLoopWakeDescription(); }
static String getLoopNap() { return outputs[loopOutputSelect].GetLoopNapDescription(); }
static String getLoopShift() { return outputs[loopOutputSelect].GetLoopShiftDescription(); }

// ── Group 12: Settings ───────────────────────────────────────
// Tap tempo carries the BPM it is tapping against, so the row is worth a value
// column rather than being a bare ROW_ACTION — it used to get one from a
// hand-written renderer, which is now gone.
static char _tapBpmBuf[12];
static String getTapBpm() {
    snprintf(_tapBpmBuf, sizeof(_tapBpmBuf), "%u BPM", (unsigned)BPM);
    return _tapBpmBuf;
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
    ShowTemporaryMessage("N/A");
#endif
}

static constexpr unsigned long TIMEOUT_OPTIONS[] = {0, 2000, 5000, 10000, 20000};
static constexpr const char *TIMEOUT_LABELS[] = {"Off", "2s", "5s", "10s", "20s"};
static constexpr int TIMEOUT_COUNT = 5;
static String getTimeout() { return TIMEOUT_LABELS[constrain(menuScreenTimeout, 0, TIMEOUT_COUNT - 1)]; }
static void setMenuTimeout(int d) {
    int dir = (d > 0) ? 1 : -1;
    menuScreenTimeout = (menuScreenTimeout + dir + TIMEOUT_COUNT) % TIMEOUT_COUNT;
    displayMgr.SetMenuTimeout(TIMEOUT_OPTIONS[menuScreenTimeout]);
    unsavedChanges = true;
}

// Which expander is fitted. Stored with the preset and, once the expander lands,
// what gates outputs 5-8, IN 4 and their menu pages. Nothing reads it yet.
static constexpr const char *EXPANDER_LABELS[] = {"None", "Exp 1"};
static constexpr int EXPANDER_COUNT = 2;
static String getExpander() {
    return EXPANDER_LABELS[constrain(expanderType, 0, EXPANDER_COUNT - 1)];
}
static void setExpander(int d) {
    int dir = (d > 0) ? 1 : -1;
    expanderType = (expanderType + dir + EXPANDER_COUNT) % EXPANDER_COUNT;
    // Bring the second DAC up (or leave it alone) the moment the setting
    // changes, rather than only at boot: someone fitting an expander expects
    // to select it and have outputs 5-8 start working, not to power-cycle.
    if (ExpanderFitted())
        InitExpDAC();
    unsavedChanges = true;
}

// ================================================================
// MENU_ITEMS[]  —  index 0 = menu item 1.
// Fields: label, valueFn, valueFn2, col1x, col2x, group, rowStyle, type, setter, action
//
// ARRAY ORDER IS THE ON-SCREEN ORDER. The encoder walks the array, and the page
// shown is the group of the current item, so moving rows moves pages. The
// grouping below is deliberate and worth preserving:
//
//   items  1– 2   the transport home screen
//   items  3–55   per-output pages: every output is a row, four rows a page
//   items 56–78   output-selector pages: an OUTPUT: row scopes the whole page
//   items 79–92   global pages: CV inputs, settings, presets
//
// Keeping the two kinds of per-output page apart is what makes room for the
// expander: outputs 5–8 get their own copies of the 3–55 block appended after
// it, while the 56–78 selector pages just widen their range to 1–8.
//
// Item numbers appear in code ONLY as the MI_* constants above; the comments
// here are what keeps those honest. If you move rows, renumber the comments and
// update the constants — nothing else holds a literal.
// ================================================================
const MenuItem MENU_ITEMS[] = {
    // ── Group 0: BPM / transport (custom renderer) ──────── items  1– 2
    {"", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_EDIT, setBPM, nullptr},              //   1  BPM
    {"", nullptr, nullptr, 0, 0, 0, ROW_HIDDEN, MENU_TOGGLE, nullptr, ToggleMasterState}, //   2  Play/Stop

    // ── Group 1: Clock dividers ───────────────────────────── items  3– 7
    {"OUTPUT 1:", getDiv<0>, nullptr, 70, 0, 1, ROW_SINGLE, MENU_EDIT, setDiv<0>, nullptr},         //   3
    {"OUTPUT 2:", getDiv<1>, nullptr, 70, 0, 1, ROW_SINGLE, MENU_EDIT, setDiv<1>, nullptr},         //   4
    {"OUTPUT 3:", getDiv<2>, nullptr, 70, 0, 1, ROW_SINGLE, MENU_EDIT, setDiv<2>, nullptr},         //   5
    {"OUTPUT 4:", getDiv<3>, nullptr, 70, 0, 1, ROW_SINGLE, MENU_EDIT, setDiv<3>, nullptr},         //   6
    {"EXT. DIV:", getExtDiv, nullptr, 70, 0, 1, ROW_SINGLE, MENU_EDIT, setExtDivider, nullptr}, //   7

    // ── Group 8: Waveform (all 4 outputs) ──────────────────── items  8–11
    {"OUTPUT 1:", getWav<0>, nullptr, 66, 0, 8, ROW_SINGLE, MENU_EDIT, setWav<0>, nullptr}, //   8
    {"OUTPUT 2:", getWav<1>, nullptr, 66, 0, 8, ROW_SINGLE, MENU_EDIT, setWav<1>, nullptr}, //   9
    {"OUTPUT 3:", getWav<2>, nullptr, 66, 0, 8, ROW_SINGLE, MENU_EDIT, setWav<2>, nullptr}, //  10
    {"OUTPUT 4:", getWav<3>, nullptr, 66, 0, 8, ROW_SINGLE, MENU_EDIT, setWav<3>, nullptr}, //  11

    // ── Group 13: Level / Offset (all 4 outputs) ────────────── items 12–19
    // col1x=70 (LVL), col2x=100 (OFF); even items = TWOCOL, odd = HIDDEN
    {"OUTPUT 1:", getLvl<0>, getOff<0>, 66, 100, 13, ROW_TWOCOL, MENU_EDIT, setLvl<0>, nullptr},  //  12
    {"OUTPUT 1:", getLvl<0>, getOff<0>, 66, 100, 13, ROW_HIDDEN, MENU_EDIT, setOff<0>, nullptr}, //  13
    {"OUTPUT 2:", getLvl<1>, getOff<1>, 66, 100, 13, ROW_TWOCOL, MENU_EDIT, setLvl<1>, nullptr},  //  14
    {"OUTPUT 2:", getLvl<1>, getOff<1>, 66, 100, 13, ROW_HIDDEN, MENU_EDIT, setOff<1>, nullptr}, //  15
    {"OUTPUT 3:", getLvl<2>, getOff<2>, 66, 100, 13, ROW_TWOCOL, MENU_EDIT, setLvl<2>, nullptr},  //  16
    {"OUTPUT 3:", getLvl<2>, getOff<2>, 66, 100, 13, ROW_HIDDEN, MENU_EDIT, setOff<2>, nullptr}, //  17
    {"OUTPUT 4:", getLvl<3>, getOff<3>, 66, 100, 13, ROW_TWOCOL, MENU_EDIT, setLvl<3>, nullptr},  //  18
    {"OUTPUT 4:", getLvl<3>, getOff<3>, 66, 100, 13, ROW_HIDDEN, MENU_EDIT, setOff<3>, nullptr}, //  19

    // ── Group 2: Output state + invert ────────────────────── items 20–27
    // col1x=58 (STATE), col2x=100 (INV); even items = TWOCOL (state), odd = HIDDEN (invert)
    {"OUT 1:", getState<0>, getInv<0>, 58, 100, 2, ROW_TWOCOL, MENU_TOGGLE, nullptr, toggleState<0>},    //  20
    {"OUT 1:", getState<0>, getInv<0>, 58, 100, 2, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleInv<0>}, //  21
    {"OUT 2:", getState<1>, getInv<1>, 58, 100, 2, ROW_TWOCOL, MENU_TOGGLE, nullptr, toggleState<1>},    //  22
    {"OUT 2:", getState<1>, getInv<1>, 58, 100, 2, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleInv<1>}, //  23
    {"OUT 3:", getState<2>, getInv<2>, 58, 100, 2, ROW_TWOCOL, MENU_TOGGLE, nullptr, toggleState<2>},    //  24
    {"OUT 3:", getState<2>, getInv<2>, 58, 100, 2, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleInv<2>}, //  25
    {"OUT 4:", getState<3>, getInv<3>, 58, 100, 2, ROW_TWOCOL, MENU_TOGGLE, nullptr, toggleState<3>},    //  26
    {"OUT 4:", getState<3>, getInv<3>, 58, 100, 2, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleInv<3>}, //  27

    // ── Group 3: Pulse probability ────────────────────────── items 28–31
    {"OUTPUT 1:", getProb<0>, nullptr, 70, 0, 3, ROW_SINGLE, MENU_EDIT, setProb<0>, nullptr}, //  28
    {"OUTPUT 2:", getProb<1>, nullptr, 70, 0, 3, ROW_SINGLE, MENU_EDIT, setProb<1>, nullptr}, //  29
    {"OUTPUT 3:", getProb<2>, nullptr, 70, 0, 3, ROW_SINGLE, MENU_EDIT, setProb<2>, nullptr}, //  30
    {"OUTPUT 4:", getProb<3>, nullptr, 70, 0, 3, ROW_SINGLE, MENU_EDIT, setProb<3>, nullptr}, //  31

    // ── Group 5: Swing ────────────────────────────────────── items 32–39
    // col1x=70 (AMT), col2x=100 (EVERY)
    {"OUTPUT 1:", getSwingAmt<0>, getSwingEv<0>, 70, 100, 5, ROW_TWOCOL, MENU_EDIT, setSwingAmt<0>, nullptr},   //  32
    {"OUTPUT 1:", getSwingAmt<0>, getSwingEv<0>, 70, 100, 5, ROW_HIDDEN, MENU_EDIT, setSwingEv<0>, nullptr}, //  33  (merged with 32)
    {"OUTPUT 2:", getSwingAmt<1>, getSwingEv<1>, 70, 100, 5, ROW_TWOCOL, MENU_EDIT, setSwingAmt<1>, nullptr},   //  34
    {"OUTPUT 2:", getSwingAmt<1>, getSwingEv<1>, 70, 100, 5, ROW_HIDDEN, MENU_EDIT, setSwingEv<1>, nullptr}, //  35
    {"OUTPUT 3:", getSwingAmt<2>, getSwingEv<2>, 70, 100, 5, ROW_TWOCOL, MENU_EDIT, setSwingAmt<2>, nullptr},   //  36
    {"OUTPUT 3:", getSwingAmt<2>, getSwingEv<2>, 70, 100, 5, ROW_HIDDEN, MENU_EDIT, setSwingEv<2>, nullptr}, //  37
    {"OUTPUT 4:", getSwingAmt<3>, getSwingEv<3>, 70, 100, 5, ROW_TWOCOL, MENU_EDIT, setSwingAmt<3>, nullptr},   //  38
    {"OUTPUT 4:", getSwingAmt<3>, getSwingEv<3>, 70, 100, 5, ROW_HIDDEN, MENU_EDIT, setSwingEv<3>, nullptr}, //  39

    // ── Group 6: Phase / Duty ─────────────────────────────── items 40–47
    // Two per-output scalars that each used to own a four-row page (phase was
    // group 6, duty group 7 — now retired). Paired onto one two-column page:
    // col1x=66 (PHASE), col2x=100 (DUTY); TWOCOL row sets phase, HIDDEN duty.
    {"OUTPUT 1:", getPhase<0>, getDuty<0>, 66, 100, 6, ROW_TWOCOL, MENU_EDIT, setPhase<0>, nullptr}, //  40
    {"OUTPUT 1:", getPhase<0>, getDuty<0>, 66, 100, 6, ROW_HIDDEN, MENU_EDIT, setDuty<0>, nullptr},  //  41
    {"OUTPUT 2:", getPhase<1>, getDuty<1>, 66, 100, 6, ROW_TWOCOL, MENU_EDIT, setPhase<1>, nullptr}, //  42
    {"OUTPUT 2:", getPhase<1>, getDuty<1>, 66, 100, 6, ROW_HIDDEN, MENU_EDIT, setDuty<1>, nullptr},  //  43
    {"OUTPUT 3:", getPhase<2>, getDuty<2>, 66, 100, 6, ROW_TWOCOL, MENU_EDIT, setPhase<2>, nullptr}, //  44
    {"OUTPUT 3:", getPhase<2>, getDuty<2>, 66, 100, 6, ROW_HIDDEN, MENU_EDIT, setDuty<2>, nullptr},  //  45
    {"OUTPUT 4:", getPhase<3>, getDuty<3>, 66, 100, 6, ROW_TWOCOL, MENU_EDIT, setPhase<3>, nullptr}, //  46
    {"OUTPUT 4:", getPhase<3>, getDuty<3>, 66, 100, 6, ROW_HIDDEN, MENU_EDIT, setDuty<3>, nullptr},  //  47

    // ── Group 14: Cross operations (all 4 outputs) ──────────── items 48–55
    // col1x=48 (OP), col2x=92 (SRC); TWOCOL sets the op, HIDDEN the source
    {"OUT 1:", getCrossOp<0>, getCrossSrc<0>, 48, 92, 14, ROW_TWOCOL, MENU_EDIT, setCrossOp<0>, nullptr},  //  48
    {"OUT 1:", getCrossOp<0>, getCrossSrc<0>, 48, 92, 14, ROW_HIDDEN, MENU_EDIT, setCrossSrc<0>, nullptr}, //  49
    {"OUT 2:", getCrossOp<1>, getCrossSrc<1>, 48, 92, 14, ROW_TWOCOL, MENU_EDIT, setCrossOp<1>, nullptr},  //  50
    {"OUT 2:", getCrossOp<1>, getCrossSrc<1>, 48, 92, 14, ROW_HIDDEN, MENU_EDIT, setCrossSrc<1>, nullptr}, //  51
    {"OUT 3:", getCrossOp<2>, getCrossSrc<2>, 48, 92, 14, ROW_TWOCOL, MENU_EDIT, setCrossOp<2>, nullptr},  //  52
    {"OUT 3:", getCrossOp<2>, getCrossSrc<2>, 48, 92, 14, ROW_HIDDEN, MENU_EDIT, setCrossSrc<2>, nullptr}, //  53
    {"OUT 4:", getCrossOp<3>, getCrossSrc<3>, 48, 92, 14, ROW_TWOCOL, MENU_EDIT, setCrossOp<3>, nullptr},  //  54
    {"OUT 4:", getCrossOp<3>, getCrossSrc<3>, 48, 92, 14, ROW_HIDDEN, MENU_EDIT, setCrossSrc<3>, nullptr}, //  55

    // ══ Outputs 5-8, on the expander ════════════════════════════════════════
    // The same eight pages again, for the expander's outputs. Hidden entirely
    // when EXPANDER is NONE - see MenuItemEnabled() below, which the encoder
    // consults while navigating, so to the user these rows are simply not there.

    // ── Group 17: Clock dividers 5-8 (no EXT DIV - that one is global) ── items 56-59
    {"OUTPUT 5:", getDiv<4>, nullptr, 70, 0, 17, ROW_SINGLE, MENU_EDIT, setDiv<4>, nullptr}, //  56
    {"OUTPUT 6:", getDiv<5>, nullptr, 70, 0, 17, ROW_SINGLE, MENU_EDIT, setDiv<5>, nullptr}, //  57
    {"OUTPUT 7:", getDiv<6>, nullptr, 70, 0, 17, ROW_SINGLE, MENU_EDIT, setDiv<6>, nullptr}, //  58
    {"OUTPUT 8:", getDiv<7>, nullptr, 70, 0, 17, ROW_SINGLE, MENU_EDIT, setDiv<7>, nullptr}, //  59

    // ── Group 18: Waveform 5-8 ── items 60-63
    {"OUTPUT 5:", getWav<4>, nullptr, 66, 0, 18, ROW_SINGLE, MENU_EDIT, setWav<4>, nullptr}, //  60
    {"OUTPUT 6:", getWav<5>, nullptr, 66, 0, 18, ROW_SINGLE, MENU_EDIT, setWav<5>, nullptr}, //  61
    {"OUTPUT 7:", getWav<6>, nullptr, 66, 0, 18, ROW_SINGLE, MENU_EDIT, setWav<6>, nullptr}, //  62
    {"OUTPUT 8:", getWav<7>, nullptr, 66, 0, 18, ROW_SINGLE, MENU_EDIT, setWav<7>, nullptr}, //  63

    // ── Group 19: Level / Offset 5-8 ── items 64-71
    {"OUTPUT 5:", getLvl<4>, getOff<4>, 66, 100, 19, ROW_TWOCOL, MENU_EDIT, setLvl<4>, nullptr},  //  64
    {"OUTPUT 5:", getLvl<4>, getOff<4>, 66, 100, 19, ROW_HIDDEN, MENU_EDIT, setOff<4>, nullptr},  //  65
    {"OUTPUT 6:", getLvl<5>, getOff<5>, 66, 100, 19, ROW_TWOCOL, MENU_EDIT, setLvl<5>, nullptr},  //  66
    {"OUTPUT 6:", getLvl<5>, getOff<5>, 66, 100, 19, ROW_HIDDEN, MENU_EDIT, setOff<5>, nullptr},  //  67
    {"OUTPUT 7:", getLvl<6>, getOff<6>, 66, 100, 19, ROW_TWOCOL, MENU_EDIT, setLvl<6>, nullptr},  //  68
    {"OUTPUT 7:", getLvl<6>, getOff<6>, 66, 100, 19, ROW_HIDDEN, MENU_EDIT, setOff<6>, nullptr},  //  69
    {"OUTPUT 8:", getLvl<7>, getOff<7>, 66, 100, 19, ROW_TWOCOL, MENU_EDIT, setLvl<7>, nullptr},  //  70
    {"OUTPUT 8:", getLvl<7>, getOff<7>, 66, 100, 19, ROW_HIDDEN, MENU_EDIT, setOff<7>, nullptr},  //  71

    // ── Group 20: Output state + invert 5-8 ── items 72-79
    {"OUT 5:", getState<4>, getInv<4>, 58, 100, 20, ROW_TWOCOL, MENU_TOGGLE, nullptr, toggleState<4>}, //  72
    {"OUT 5:", getState<4>, getInv<4>, 58, 100, 20, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleInv<4>},   //  73
    {"OUT 6:", getState<5>, getInv<5>, 58, 100, 20, ROW_TWOCOL, MENU_TOGGLE, nullptr, toggleState<5>}, //  74
    {"OUT 6:", getState<5>, getInv<5>, 58, 100, 20, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleInv<5>},   //  75
    {"OUT 7:", getState<6>, getInv<6>, 58, 100, 20, ROW_TWOCOL, MENU_TOGGLE, nullptr, toggleState<6>}, //  76
    {"OUT 7:", getState<6>, getInv<6>, 58, 100, 20, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleInv<6>},   //  77
    {"OUT 8:", getState<7>, getInv<7>, 58, 100, 20, ROW_TWOCOL, MENU_TOGGLE, nullptr, toggleState<7>}, //  78
    {"OUT 8:", getState<7>, getInv<7>, 58, 100, 20, ROW_HIDDEN, MENU_TOGGLE, nullptr, toggleInv<7>},   //  79

    // ── Group 21: Pulse probability 5-8 ── items 80-83
    {"OUTPUT 5:", getProb<4>, nullptr, 70, 0, 21, ROW_SINGLE, MENU_EDIT, setProb<4>, nullptr}, //  80
    {"OUTPUT 6:", getProb<5>, nullptr, 70, 0, 21, ROW_SINGLE, MENU_EDIT, setProb<5>, nullptr}, //  81
    {"OUTPUT 7:", getProb<6>, nullptr, 70, 0, 21, ROW_SINGLE, MENU_EDIT, setProb<6>, nullptr}, //  82
    {"OUTPUT 8:", getProb<7>, nullptr, 70, 0, 21, ROW_SINGLE, MENU_EDIT, setProb<7>, nullptr}, //  83

    // ── Group 22: Swing 5-8 ── items 84-91
    {"OUTPUT 5:", getSwingAmt<4>, getSwingEv<4>, 70, 100, 22, ROW_TWOCOL, MENU_EDIT, setSwingAmt<4>, nullptr}, //  84
    {"OUTPUT 5:", getSwingAmt<4>, getSwingEv<4>, 70, 100, 22, ROW_HIDDEN, MENU_EDIT, setSwingEv<4>, nullptr},  //  85
    {"OUTPUT 6:", getSwingAmt<5>, getSwingEv<5>, 70, 100, 22, ROW_TWOCOL, MENU_EDIT, setSwingAmt<5>, nullptr}, //  86
    {"OUTPUT 6:", getSwingAmt<5>, getSwingEv<5>, 70, 100, 22, ROW_HIDDEN, MENU_EDIT, setSwingEv<5>, nullptr},  //  87
    {"OUTPUT 7:", getSwingAmt<6>, getSwingEv<6>, 70, 100, 22, ROW_TWOCOL, MENU_EDIT, setSwingAmt<6>, nullptr}, //  88
    {"OUTPUT 7:", getSwingAmt<6>, getSwingEv<6>, 70, 100, 22, ROW_HIDDEN, MENU_EDIT, setSwingEv<6>, nullptr},  //  89
    {"OUTPUT 8:", getSwingAmt<7>, getSwingEv<7>, 70, 100, 22, ROW_TWOCOL, MENU_EDIT, setSwingAmt<7>, nullptr}, //  90
    {"OUTPUT 8:", getSwingAmt<7>, getSwingEv<7>, 70, 100, 22, ROW_HIDDEN, MENU_EDIT, setSwingEv<7>, nullptr},  //  91

    // ── Group 23: Phase / Duty 5-8 ── items 92-99
    {"OUTPUT 5:", getPhase<4>, getDuty<4>, 66, 100, 23, ROW_TWOCOL, MENU_EDIT, setPhase<4>, nullptr}, //  92
    {"OUTPUT 5:", getPhase<4>, getDuty<4>, 66, 100, 23, ROW_HIDDEN, MENU_EDIT, setDuty<4>, nullptr},  //  93
    {"OUTPUT 6:", getPhase<5>, getDuty<5>, 66, 100, 23, ROW_TWOCOL, MENU_EDIT, setPhase<5>, nullptr}, //  94
    {"OUTPUT 6:", getPhase<5>, getDuty<5>, 66, 100, 23, ROW_HIDDEN, MENU_EDIT, setDuty<5>, nullptr},  //  95
    {"OUTPUT 7:", getPhase<6>, getDuty<6>, 66, 100, 23, ROW_TWOCOL, MENU_EDIT, setPhase<6>, nullptr}, //  96
    {"OUTPUT 7:", getPhase<6>, getDuty<6>, 66, 100, 23, ROW_HIDDEN, MENU_EDIT, setDuty<6>, nullptr},  //  97
    {"OUTPUT 8:", getPhase<7>, getDuty<7>, 66, 100, 23, ROW_TWOCOL, MENU_EDIT, setPhase<7>, nullptr}, //  98
    {"OUTPUT 8:", getPhase<7>, getDuty<7>, 66, 100, 23, ROW_HIDDEN, MENU_EDIT, setDuty<7>, nullptr},  //  99

    // ── Group 24: Cross operations 5-8 ── items 100-107
    {"OUT 5:", getCrossOp<4>, getCrossSrc<4>, 48, 92, 24, ROW_TWOCOL, MENU_EDIT, setCrossOp<4>, nullptr},  // 100
    {"OUT 5:", getCrossOp<4>, getCrossSrc<4>, 48, 92, 24, ROW_HIDDEN, MENU_EDIT, setCrossSrc<4>, nullptr}, // 101
    {"OUT 6:", getCrossOp<5>, getCrossSrc<5>, 48, 92, 24, ROW_TWOCOL, MENU_EDIT, setCrossOp<5>, nullptr},  // 102
    {"OUT 6:", getCrossOp<5>, getCrossSrc<5>, 48, 92, 24, ROW_HIDDEN, MENU_EDIT, setCrossSrc<5>, nullptr}, // 103
    {"OUT 7:", getCrossOp<6>, getCrossSrc<6>, 48, 92, 24, ROW_TWOCOL, MENU_EDIT, setCrossOp<6>, nullptr},  // 104
    {"OUT 7:", getCrossOp<6>, getCrossSrc<6>, 48, 92, 24, ROW_HIDDEN, MENU_EDIT, setCrossSrc<6>, nullptr}, // 105
    {"OUT 8:", getCrossOp<7>, getCrossSrc<7>, 48, 92, 24, ROW_TWOCOL, MENU_EDIT, setCrossOp<7>, nullptr},  // 106
    {"OUT 8:", getCrossOp<7>, getCrossSrc<7>, 48, 92, 24, ROW_HIDDEN, MENU_EDIT, setCrossSrc<7>, nullptr}, // 107

    // ══ Output-selector pages ═══════════════════════════════════════════════
    // From here on a page scopes itself to one output through its own OUTPUT:
    // row, rather than listing every output as a row of its own. Grouped
    // together so the two kinds of page do not interleave.

    // ── Group 4: Euclidean rhythm ─────────────────────────── items 56–61
    {"OUTPUT:", getEucSel, nullptr, 64, 0, 4, ROW_SINGLE, MENU_EDIT, setEuclideanOutputSel, nullptr}, // 108
    {"ENABLED:", getEucEn, nullptr, 64, 0, 4, ROW_SINGLE, MENU_TOGGLE, nullptr, toggleEuclidean},     // 109
    {"STEPS:", getEucSteps, nullptr, 64, 0, 4, ROW_SINGLE, MENU_EDIT, setEuclideanSteps, nullptr},    // 110
    {"HITS:", getEucTrig, nullptr, 64, 0, 4, ROW_SINGLE, MENU_EDIT, setEuclideanTrig, nullptr},       // 111
    {"ROT:", getEucRot, nullptr, 34, 0, 4, ROW_SINGLE, MENU_EDIT, setEuclideanRot, nullptr},          // 112  MI_EUC_ROT
    {"PAD:", getEucPad, nullptr, 64, 0, 4, ROW_SINGLE, MENU_EDIT, setEuclideanPad, nullptr},          // 113  MI_EUC_PAD

    // ── Group 15: Loops (output selector + params) ──────────── items 62–66
    {"OUTPUT:", getLoopSel, nullptr, 80, 0, 15, ROW_SINGLE, MENU_EDIT, setLoopOutputSel, nullptr},   // 114
    {"LOOP BEATS:", getLoopBeats, nullptr, 80, 0, 15, ROW_SINGLE, MENU_EDIT, setLoopBeats, nullptr}, // 115
    {"WAKE:", getLoopWake, nullptr, 80, 0, 15, ROW_SINGLE, MENU_EDIT, setLoopWake, nullptr},         // 116
    {"NAP:", getLoopNap, nullptr, 80, 0, 15, ROW_SINGLE, MENU_EDIT, setLoopNap, nullptr},            // 117
    {"SHIFT:", getLoopShift, nullptr, 80, 0, 15, ROW_SINGLE, MENU_EDIT, setLoopShift, nullptr},      // 118

    // ── Group 9: Envelope ─────────────────────────────────── items 67–73
    {"OUTPUT:", getEnvSel, nullptr, 70, 0, 9, ROW_SINGLE, MENU_EDIT, setEnvOutputSel, nullptr}, // 119
    {"Attack:", getAttack, nullptr, 70, 0, 9, ROW_SINGLE, MENU_EDIT, setAttack, nullptr},       // 120
    {"Decay:", getDecay, nullptr, 70, 0, 9, ROW_SINGLE, MENU_EDIT, setDecay, nullptr},          // 121
    {"Sustain:", getSustain, nullptr, 70, 0, 9, ROW_SINGLE, MENU_EDIT, setSustain, nullptr},    // 122
    {"Release:", getRelease, nullptr, 70, 0, 9, ROW_SINGLE, MENU_EDIT, setRelease, nullptr},    // 123
    {"Curv:", getCurve, nullptr, 70, 0, 9, ROW_SINGLE, MENU_EDIT, setCurve, nullptr},           // 124  MI_ENV_CURVE
    {"Retr:", getRetrig, nullptr, 76, 0, 9, ROW_SINGLE, MENU_TOGGLE, nullptr, toggleRetrigger}, // 125  MI_ENV_RETRIG

    // ── Group 11: Quantizer ───────────────────────────────── items 74–78
    {"OUTPUT:", getQtzSel, nullptr, 80, 0, 11, ROW_SINGLE, MENU_EDIT, setQtzOutputSel, nullptr},     // 126
    {"ENABLED:", getQtzEn, nullptr, 80, 0, 11, ROW_SINGLE, MENU_TOGGLE, nullptr, toggleQuantizer},   // 127
    {"ROOT NOTE:", getQtzNote, nullptr, 80, 0, 11, ROW_SINGLE, MENU_EDIT, setQtzNote, nullptr},      // 128
    {"SCALE:", getQtzScale, nullptr, 80, 0, 11, ROW_SINGLE, MENU_EDIT, setQtzScale, nullptr},        // 129
    {"OCT TRANSPOSE:", getQtzOct, nullptr, 96, 0, 11, ROW_SINGLE, MENU_EDIT, setQtzOctave, nullptr}, // 130

    // ══ Global pages ════════════════════════════════════════════════════════

    // ── Group 10: CV inputs ───────────────────────────────── items 79–84
    // 79–80: full-width target rows; 81–84: two-col ATTN+OFF
    {"CV 1:", getCVTgt0, nullptr, 70, 0, 10, ROW_SINGLE, MENU_EDIT, setCVTarget0, nullptr},      // 131  MI_CV_TARGET_FIRST
    {"CV 2:", getCVTgt1, nullptr, 70, 0, 10, ROW_SINGLE, MENU_EDIT, setCVTarget1, nullptr},      // 132
    {"CV 3:", getCVTgt2, nullptr, 70, 0, 10, ROW_SINGLE, MENU_EDIT, setCVTarget2, nullptr},      // 133  MI_CV_TARGET_LAST (expander IN 4)
    {"CV 1:", getCVAttn0, getCVOff0, 60, 100, 25, ROW_TWOCOL, MENU_EDIT, setCVAttn0, nullptr},   // 134  MI_CV_ATTN_FIRST
    {"CV 1:", getCVAttn0, getCVOff0, 60, 100, 25, ROW_HIDDEN, MENU_EDIT, setCVOffset0, nullptr}, // 135
    {"CV 2:", getCVAttn1, getCVOff1, 60, 100, 25, ROW_TWOCOL, MENU_EDIT, setCVAttn1, nullptr},   // 136
    {"CV 2:", getCVAttn1, getCVOff1, 60, 100, 25, ROW_HIDDEN, MENU_EDIT, setCVOffset1, nullptr}, // 137
    {"CV 3:", getCVAttn2, getCVOff2, 60, 100, 25, ROW_TWOCOL, MENU_EDIT, setCVAttn2, nullptr},   // 138  (expander)
    {"CV 3:", getCVAttn2, getCVOff2, 60, 100, 25, ROW_HIDDEN, MENU_EDIT, setCVOffset2, nullptr}, // 139

    // ── Group 12: Settings ────────────────────────────────── items 85–88
    {"TAP TEMPO:", getTapBpm, nullptr, 76, 0, 12, ROW_SINGLE, MENU_ACTION, nullptr, SetTapTempo},     // 140
    {"SCR TIMEOUT:", getTimeout, nullptr, 88, 0, 12, ROW_SINGLE, MENU_EDIT, setMenuTimeout, nullptr}, // 141
    {"EXPANDER:", getExpander, nullptr, 82, 0, 12, ROW_SINGLE, MENU_EDIT, setExpander, nullptr},      // 142
    {"BOOT MENU", nullptr, nullptr, 0, 0, 12, ROW_ACTION, MENU_ACTION, nullptr, actBootMenu},         // 143

    // ── Group 16: Presets ─────────────────────────────────── items 89–92
    {"PRESET SLOT:", getSaveSlot, nullptr, 88, 0, 16, ROW_SINGLE, MENU_EDIT, setSaveSlot, nullptr},      // 144
    {"SAVE", nullptr, nullptr, 0, 0, 16, ROW_ACTION, MENU_ACTION, nullptr, actionSave},                  // 145
    {"LOAD", nullptr, nullptr, 0, 0, 16, ROW_ACTION, MENU_ACTION, nullptr, actionLoad},                  // 146
    {"LOAD DEFAULTS", nullptr, nullptr, 0, 0, 16, ROW_ACTION, MENU_ACTION, nullptr, actionLoadDefaults}, // 147
};

const int MENU_ITEM_COUNT = (int)(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]));

// ── Which rows exist right now ───────────────────────────────────────────────
// Rows for hardware that is not attached are not greyed out or shown empty —
// the encoder steps straight over them, so with EXPANDER on NONE the menu is
// exactly the menu it was before any of this existed.
//
// core/encoderMenu.hpp calls this through FORGE_MENU_ITEM_ENABLED (defined
// just below), and the CV page's renderer calls it directly, because its
// expander rows sit inside a page it shares with the base board's.
static inline bool MenuItemEnabled(int item) {
    if (item < 1 || item > MENU_ITEM_COUNT)
        return true;

    // Whole pages: outputs 5-8.
    const uint8_t g = MENU_ITEMS[item - 1].group;
    if (g >= MI_GROUP_EXP_FIRST && g <= MI_GROUP_EXP_LAST)
        return ExpanderFitted();

    // Individual rows: IN 4's target, and its attenuation/offset pair, which
    // live on the shared CV page.
    const int tgt = item - MI_CV_TARGET_FIRST;
    if (tgt >= 0 && tgt < NUM_MAX_CV_INS)
        return tgt < ActiveCvIns();
    const int pair = item - MI_CV_ATTN_FIRST;
    if (pair >= 0 && pair < NUM_MAX_CV_INS * 2)
        return (pair / 2) < ActiveCvIns();

    return true;
}

// The scroll indicator has to count what the user can actually reach, not the
// size of the table: with no expander fitted, 55 of the 147 rows do not exist
// as far as the encoder is concerned, and a bar sized for all of them would
// promise menu that cannot be scrolled to.
//
// A linear walk per frame, but the display is rate-limited to 20 Hz and this is
// 147 predicate calls — far cheaper than the render it accompanies.
static inline int MenuVisibleCount() {
    int n = 0;
    for (int i = 1; i <= MENU_ITEM_COUNT; i++)
        if (MenuItemEnabled(i))
            n++;
    return n;
}

// Where `item` sits among the visible rows, 1-based.
static inline int MenuVisibleIndex(int item) {
    int n = 0;
    for (int i = 1; i <= item && i <= MENU_ITEM_COUNT; i++)
        if (MenuItemEnabled(i))
            n++;
    return n;
}

// Picked up by core/encoderMenu.hpp, which is included after this file.
#define FORGE_MENU_ITEM_ENABLED(item) MenuItemEnabled(item)
