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
// Groups 1, 2, 3, 6, 7, 11 use the GENERIC renderer (MD_RenderGroup in
// menuDisplay.hpp) — it iterates items by group and draws them using their
// rowStyle.  Adding items to these groups requires NO changes to rendering.
//
// Groups 0, 4, 5, 8, 9, 10, 12 have CUSTOM renderers in menuRender.hpp
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

#include "menuDefinitions.hpp"
#include "outputs.hpp"
#include "clockEngine.hpp"
#include "cvInputs.hpp"
#include "presetManager.hpp" // saveSlot, NUM_SLOTS, CollectParams, UpdateParameters, LoadDefaultParams
#include "storage.hpp"       // Save(), Load()

// ---- Externs from src/main.cpp --------------------------------
extern bool  unsavedChanges;
extern int   euclideanOutputSelect;
extern int   envelopeOutputSelect;
extern int   quantizerOutputSelect;
extern int   menuScreenTimeout;
extern int   menuMode;

// Functions defined later in main.cpp — forward declarations only.
extern void ShowTemporaryMessage(const char *msg, uint32_t durationMs = 1000);
extern void ToggleMasterState();

// ================================================================
// Group 1 — BPM / transport  (items 1–2)
// ================================================================

static void setBPM(int d) {
    BPM = (unsigned int)constrain((int)BPM + d, (int)minBPM, (int)maxBPM);
    lastInternalBPM = BPM;
    bpmNeedsUpdate  = true;
    unsavedChanges  = true;
}
// item 2: ToggleMasterState() — extern above

// ================================================================
// Group 2 — Clock dividers  (items 3–7)
// ================================================================

static void setDiv0(int d) { if (outputs[0].IsEnvelopeType()) return; outputs[0].SetDivider(outputs[0].GetDividerIndex() + d); unsavedChanges = true; }
static void setDiv1(int d) { if (outputs[1].IsEnvelopeType()) return; outputs[1].SetDivider(outputs[1].GetDividerIndex() + d); unsavedChanges = true; }
static void setDiv2(int d) { if (outputs[2].IsEnvelopeType()) return; outputs[2].SetDivider(outputs[2].GetDividerIndex() + d); unsavedChanges = true; }
static void setDiv3(int d) { if (outputs[3].IsEnvelopeType()) return; outputs[3].SetDivider(outputs[3].GetDividerIndex() + d); unsavedChanges = true; }

static void setExtDivider(int d) {
    externalDividerIndex = constrain(externalDividerIndex + d, 0, dividerAmount - 1);
    unsavedChanges = true;
}

// ================================================================
// Group 3 — Output state toggles  (items 8–11)
// ================================================================

static void toggleOut0() { outputs[0].ToggleOutputState(); unsavedChanges = true; }
static void toggleOut1() { outputs[1].ToggleOutputState(); unsavedChanges = true; }
static void toggleOut2() { outputs[2].ToggleOutputState(); unsavedChanges = true; }
static void toggleOut3() { outputs[3].ToggleOutputState(); unsavedChanges = true; }

// ================================================================
// Group 4 — Pulse probability  (items 12–15)
// ================================================================

static void setProb0(int d) { outputs[0].SetPulseProbability(outputs[0].GetPulseProbability() + d); unsavedChanges = true; }
static void setProb1(int d) { outputs[1].SetPulseProbability(outputs[1].GetPulseProbability() + d); unsavedChanges = true; }
static void setProb2(int d) { outputs[2].SetPulseProbability(outputs[2].GetPulseProbability() + d); unsavedChanges = true; }
static void setProb3(int d) { outputs[3].SetPulseProbability(outputs[3].GetPulseProbability() + d); unsavedChanges = true; }

// ================================================================
// Group 5 — Euclidean rhythm  (items 16–21)
// ================================================================

static void setEuclideanOutputSel(int d) {
    int dir = (d > 0) ? 1 : -1;
    euclideanOutputSelect = (euclideanOutputSelect + dir + NUM_OUTPUTS) % NUM_OUTPUTS;
    unsavedChanges = true;
}
static void toggleEuclidean()         { outputs[euclideanOutputSelect].ToggleEuclidean();                                                                unsavedChanges = true; }
static void setEuclideanSteps(int d)  { outputs[euclideanOutputSelect].SetEuclideanSteps(outputs[euclideanOutputSelect].GetEuclideanSteps() + d);         unsavedChanges = true; }
static void setEuclideanTrig(int d)   { outputs[euclideanOutputSelect].SetEuclideanTriggers(outputs[euclideanOutputSelect].GetEuclideanTriggers() + d);   unsavedChanges = true; }
static void setEuclideanRot(int d)    { outputs[euclideanOutputSelect].SetEuclideanRotation(outputs[euclideanOutputSelect].GetEuclideanRotation() + d);   unsavedChanges = true; }
static void setEuclideanPad(int d)    { outputs[euclideanOutputSelect].SetEuclideanPadding(outputs[euclideanOutputSelect].GetEuclideanPadding() + d);     unsavedChanges = true; }

// ================================================================
// Group 6 — Swing  (items 22–29)
// ================================================================

static void setSwingAmt0(int d)   { outputs[0].SetSwingAmount(outputs[0].GetSwingAmountIndex() + d); unsavedChanges = true; }
static void setSwingEvery0(int d) { outputs[0].SetSwingEvery(outputs[0].GetSwingEvery() + d);        unsavedChanges = true; }
static void setSwingAmt1(int d)   { outputs[1].SetSwingAmount(outputs[1].GetSwingAmountIndex() + d); unsavedChanges = true; }
static void setSwingEvery1(int d) { outputs[1].SetSwingEvery(outputs[1].GetSwingEvery() + d);        unsavedChanges = true; }
static void setSwingAmt2(int d)   { outputs[2].SetSwingAmount(outputs[2].GetSwingAmountIndex() + d); unsavedChanges = true; }
static void setSwingEvery2(int d) { outputs[2].SetSwingEvery(outputs[2].GetSwingEvery() + d);        unsavedChanges = true; }
static void setSwingAmt3(int d)   { outputs[3].SetSwingAmount(outputs[3].GetSwingAmountIndex() + d); unsavedChanges = true; }
static void setSwingEvery3(int d) { outputs[3].SetSwingEvery(outputs[3].GetSwingEvery() + d);        unsavedChanges = true; }

// ================================================================
// Group 7 — Phase shift  (items 30–33)
// ================================================================

static void setPhase0(int d) { outputs[0].SetPhase(outputs[0].GetPhase() + d); unsavedChanges = true; }
static void setPhase1(int d) { outputs[1].SetPhase(outputs[1].GetPhase() + d); unsavedChanges = true; }
static void setPhase2(int d) { outputs[2].SetPhase(outputs[2].GetPhase() + d); unsavedChanges = true; }
static void setPhase3(int d) { outputs[3].SetPhase(outputs[3].GetPhase() + d); unsavedChanges = true; }

// ================================================================
// Group 8 — Duty cycle  (items 34–37)
// ================================================================

static void setDuty0(int d) { outputs[0].SetDutyCycle(outputs[0].GetDutyCycle() + d); unsavedChanges = true; }
static void setDuty1(int d) { outputs[1].SetDutyCycle(outputs[1].GetDutyCycle() + d); unsavedChanges = true; }
static void setDuty2(int d) { outputs[2].SetDutyCycle(outputs[2].GetDutyCycle() + d); unsavedChanges = true; }
static void setDuty3(int d) { outputs[3].SetDutyCycle(outputs[3].GetDutyCycle() + d); unsavedChanges = true; }

// ================================================================
// Group 8 — Waveform (all 4 outputs)  (items 8–11)
// ================================================================

// Waveform scroll: ignore speedFactor magnitude — use direction only (preserves original ±1 wrap)
static void setWaveform0(int d) {
    int dir = (d > 0) ? 1 : -1;
    outputs[0].SetWaveformType(static_cast<WaveformType>((outputs[0].GetWaveformType() + WaveformTypeLength + dir) % WaveformTypeLength));
    unsavedChanges = true;
}
static void setWaveform1(int d) {
    int dir = (d > 0) ? 1 : -1;
    outputs[1].SetWaveformType(static_cast<WaveformType>((outputs[1].GetWaveformType() + WaveformTypeLength + dir) % WaveformTypeLength));
    unsavedChanges = true;
}
static void setWaveform2(int d) {
    int dir = (d > 0) ? 1 : -1;
    outputs[2].SetWaveformType(static_cast<WaveformType>((outputs[2].GetWaveformType() + WaveformTypeLength + dir) % WaveformTypeLength));
    unsavedChanges = true;
}
static void setWaveform3(int d) {
    int dir = (d > 0) ? 1 : -1;
    outputs[3].SetWaveformType(static_cast<WaveformType>((outputs[3].GetWaveformType() + WaveformTypeLength + dir) % WaveformTypeLength));
    unsavedChanges = true;
}

// ================================================================
// Group 13 — Level / Offset (all 4 outputs)  (items 12–19)
// ================================================================

static void setLevel0(int d)  { outputs[0].SetLevel(outputs[0].GetLevel() + d);   unsavedChanges = true; }
static void setOffset0(int d) { outputs[0].SetOffset(outputs[0].GetOffset() + d); unsavedChanges = true; }
static void setLevel1(int d)  { outputs[1].SetLevel(outputs[1].GetLevel() + d);   unsavedChanges = true; }
static void setOffset1(int d) { outputs[1].SetOffset(outputs[1].GetOffset() + d); unsavedChanges = true; }
static void setLevel2(int d)  { outputs[2].SetLevel(outputs[2].GetLevel() + d);   unsavedChanges = true; }
static void setOffset2(int d) { outputs[2].SetOffset(outputs[2].GetOffset() + d); unsavedChanges = true; }
static void setLevel3(int d)  { outputs[3].SetLevel(outputs[3].GetLevel() + d);   unsavedChanges = true; }
static void setOffset3(int d) { outputs[3].SetOffset(outputs[3].GetOffset() + d); unsavedChanges = true; }

// ================================================================
// Group 10 — Envelope settings  (items 44–50)
// ================================================================

// Envelope output select wraps across all 4 outputs (indices 0–3).
static void setEnvOutputSel(int d) {
    int dir = (d > 0) ? 1 : -1;
    envelopeOutputSelect = (envelopeOutputSelect + dir + NUM_OUTPUTS) % NUM_OUTPUTS;
    unsavedChanges = true;
}
// Attack/decay/release receive delta × 2 to match the original ×2 speedFactor scale.
static void setAttack(int d)  { outputs[envelopeOutputSelect].SetAttack(outputs[envelopeOutputSelect].GetAttack()     + d * 2); unsavedChanges = true; }
static void setDecay(int d)   { outputs[envelopeOutputSelect].SetDecay(outputs[envelopeOutputSelect].GetDecay()       + d * 2); unsavedChanges = true; }
static void setSustain(int d) { outputs[envelopeOutputSelect].SetSustain(outputs[envelopeOutputSelect].GetSustain()   + d);     unsavedChanges = true; }
static void setRelease(int d) { outputs[envelopeOutputSelect].SetRelease(outputs[envelopeOutputSelect].GetRelease()   + d * 2); unsavedChanges = true; }
static void setCurve(int d)   { outputs[envelopeOutputSelect].SetCurve(outputs[envelopeOutputSelect].GetCurve()       + d * 0.01f); unsavedChanges = true; }
static void toggleRetrigger() { outputs[envelopeOutputSelect].ToggleRetrigger(); unsavedChanges = true; }

// ================================================================
// Group 11 — CV inputs  (items 51–56)
// ================================================================

// Target scroll: direction only; skip duplicates across the two CV channels.
static void setCVTarget0(int d) {
    int dir = (d > 0) ? 1 : -1;
    CVTarget tmp = static_cast<CVTarget>((pendingCVInputTarget[0] + CVTargetLength + dir) % CVTargetLength);
    if (tmp != pendingCVInputTarget[1] || tmp == CVTarget::None) {
        pendingCVInputTarget[0] = tmp;
    } else {
        pendingCVInputTarget[0] = static_cast<CVTarget>((pendingCVInputTarget[0] + CVTargetLength + dir * 2) % CVTargetLength);
    }
}
static void setCVTarget1(int d) {
    int dir = (d > 0) ? 1 : -1;
    CVTarget tmp = static_cast<CVTarget>((pendingCVInputTarget[1] + CVTargetLength + dir) % CVTargetLength);
    if (tmp != pendingCVInputTarget[0] || tmp == CVTarget::None) {
        pendingCVInputTarget[1] = tmp;
    } else {
        pendingCVInputTarget[1] = static_cast<CVTarget>((pendingCVInputTarget[1] + CVTargetLength + dir * 2) % CVTargetLength);
    }
}
// Note: attenuation/offset do not set unsavedChanges — preserving original behaviour.
static void setCVAttn0(int d)   { CVInputAttenuation[0] = constrain(CVInputAttenuation[0] + d, 0, 100); }
static void setCVOffset0(int d) { CVInputOffset[0]      = constrain(CVInputOffset[0]      + d, 0, 100); }
static void setCVAttn1(int d)   { CVInputAttenuation[1] = constrain(CVInputAttenuation[1] + d, 0, 100); }
static void setCVOffset1(int d) { CVInputOffset[1]      = constrain(CVInputOffset[1]      + d, 0, 100); }

// ================================================================
// Group 12 — Quantizer  (items 57–61)
// ================================================================

// Quantizer output select wraps across all 4 outputs (indices 0–3).
static void setQtzOutputSel(int d) {
    int dir = (d > 0) ? 1 : -1;
    quantizerOutputSelect = (quantizerOutputSelect + dir + NUM_OUTPUTS) % NUM_OUTPUTS;
    unsavedChanges = true;
}
static void toggleQuantizer()  { outputs[quantizerOutputSelect].ToggleQuantizer(); unsavedChanges = true; }
static void setQtzNote(int d)  { outputs[quantizerOutputSelect].SetQuantizerNoteIndex(outputs[quantizerOutputSelect].GetQuantizerNoteIndex()     + d); unsavedChanges = true; }
static void setQtzScale(int d) { outputs[quantizerOutputSelect].SetQuantizerScaleIndex(outputs[quantizerOutputSelect].GetQuantizerScaleIndex()   + d); unsavedChanges = true; }
static void setQtzOctave(int d){ outputs[quantizerOutputSelect].SetQuantizerOctaveShift(outputs[quantizerOutputSelect].GetQuantizerOctaveShift() + d); unsavedChanges = true; }

// ================================================================
// Group 13 — Settings / save-load  (items 62–66)
// ================================================================

// Save-slot scroll wraps within [0, NUM_SLOTS] inclusive.
static void setSaveSlot(int d) {
    int dir = (d > 0) ? 1 : -1;
    saveSlot = (saveSlot + dir + NUM_SLOTS + 1) % (NUM_SLOTS + 1);
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
// Value getter functions — return const char* for display.
// Each uses a static char buffer (safe: only one is live at a time
// since they are called sequentially during display rendering).
// ================================================================

// ── Group 1: BPM / transport ─────────────────────────────────
// (BPM display is a fully custom renderer — no getter needed)

// ── Group 2: Clock dividers ──────────────────────────────────
static String getDiv0()   { return outputs[0].GetDividerDescription(); }
static String getDiv1()   { return outputs[1].GetDividerDescription(); }
static String getDiv2()   { return outputs[2].GetDividerDescription(); }
static String getDiv3()   { return outputs[3].GetDividerDescription(); }
static String getExtDiv() {
    return externalDividerDescription[externalDividerIndex];
}

// ── Group 3: Output state ────────────────────────────────────
static String getState0() { return outputs[0].GetOutputState() ? "ON" : "OFF"; }
static String getState1() { return outputs[1].GetOutputState() ? "ON" : "OFF"; }
static String getState2() { return outputs[2].GetOutputState() ? "ON" : "OFF"; }
static String getState3() { return outputs[3].GetOutputState() ? "ON" : "OFF"; }

// ── Group 4: Pulse probability ───────────────────────────────
static String getProb0()  { return outputs[0].GetPulseProbabilityDescription(); }
static String getProb1()  { return outputs[1].GetPulseProbabilityDescription(); }
static String getProb2()  { return outputs[2].GetPulseProbabilityDescription(); }
static String getProb3()  { return outputs[3].GetPulseProbabilityDescription(); }

// ── Group 5: Euclidean rhythm ────────────────────────────────
static char _eucBuf[4];
static String getEucSel()    { snprintf(_eucBuf, sizeof(_eucBuf), "%d", euclideanOutputSelect + 1); return _eucBuf; }
static String getEucEn()     { return outputs[euclideanOutputSelect].GetEuclidean() ? "YES" : "NO"; }
static String getEucSteps()  { snprintf(_eucBuf, sizeof(_eucBuf), "%d", outputs[euclideanOutputSelect].GetEuclideanSteps()); return _eucBuf; }
static String getEucTrig()   { snprintf(_eucBuf, sizeof(_eucBuf), "%d", outputs[euclideanOutputSelect].GetEuclideanTriggers()); return _eucBuf; }
static String getEucRot()    { snprintf(_eucBuf, sizeof(_eucBuf), "%d", outputs[euclideanOutputSelect].GetEuclideanRotation()); return _eucBuf; }
static String getEucPad()    { snprintf(_eucBuf, sizeof(_eucBuf), "%d", outputs[euclideanOutputSelect].GetEuclideanPadding()); return _eucBuf; }

// ── Group 6: Swing ───────────────────────────────────────────
static String getSwingAmt0()   { return outputs[0].GetSwingAmountDescription(); }
static char _swingEvBuf[4];
static String getSwingEv0()    { snprintf(_swingEvBuf, sizeof(_swingEvBuf), "%d", outputs[0].GetSwingEvery()); return _swingEvBuf; }
static String getSwingAmt1()   { return outputs[1].GetSwingAmountDescription(); }
static String getSwingEv1()    { snprintf(_swingEvBuf, sizeof(_swingEvBuf), "%d", outputs[1].GetSwingEvery()); return _swingEvBuf; }
static String getSwingAmt2()   { return outputs[2].GetSwingAmountDescription(); }
static String getSwingEv2()    { snprintf(_swingEvBuf, sizeof(_swingEvBuf), "%d", outputs[2].GetSwingEvery()); return _swingEvBuf; }
static String getSwingAmt3()   { return outputs[3].GetSwingAmountDescription(); }
static String getSwingEv3()    { snprintf(_swingEvBuf, sizeof(_swingEvBuf), "%d", outputs[3].GetSwingEvery()); return _swingEvBuf; }

// ── Group 7: Phase shift ─────────────────────────────────────
static String getPhase0() { return outputs[0].GetPhaseDescription(); }
static String getPhase1() { return outputs[1].GetPhaseDescription(); }
static String getPhase2() { return outputs[2].GetPhaseDescription(); }
static String getPhase3() { return outputs[3].GetPhaseDescription(); }

// ── Group 8: Duty cycle ──────────────────────────────────────
static String getDuty0()  { return outputs[0].GetDutyCycleDescription(); }
static String getDuty1()  { return outputs[1].GetDutyCycleDescription(); }
static String getDuty2()  { return outputs[2].GetDutyCycleDescription(); }
static String getDuty3()  { return outputs[3].GetDutyCycleDescription(); }

// ── Group 8: Waveform (all 4) ────────────────────────────────
static String getWav0()   { return outputs[0].GetWaveformTypeDescription(); }
static String getWav1()   { return outputs[1].GetWaveformTypeDescription(); }
static String getWav2()   { return outputs[2].GetWaveformTypeDescription(); }
static String getWav3()   { return outputs[3].GetWaveformTypeDescription(); }

// ── Group 13: Level / Offset (all 4) ─────────────────────────
static String getLvl0()   { return outputs[0].GetLevelDescription(); }
static String getOff0()   { return outputs[0].GetOffsetDescription(); }
static String getLvl1()   { return outputs[1].GetLevelDescription(); }
static String getOff1()   { return outputs[1].GetOffsetDescription(); }
static String getLvl2()   { return outputs[2].GetLevelDescription(); }
static String getOff2()   { return outputs[2].GetOffsetDescription(); }
static String getLvl3()   { return outputs[3].GetLevelDescription(); }
static String getOff3()   { return outputs[3].GetOffsetDescription(); }

// ── Group 10: Envelope ───────────────────────────────────────
static char _envBuf[8];
static String getEnvSel()  { snprintf(_envBuf, sizeof(_envBuf), "%d", envelopeOutputSelect + 1); return _envBuf; }
static String getAttack()  { return outputs[envelopeOutputSelect].GetAttackDescription(); }
static String getDecay()   { return outputs[envelopeOutputSelect].GetDecayDescription(); }
static String getSustain() { return outputs[envelopeOutputSelect].GetSustainDescription(); }
static String getRelease() { return outputs[envelopeOutputSelect].GetReleaseDescription(); }
static String getCurve()   { return outputs[envelopeOutputSelect].GetCurveDescription(); }
static String getRetrig()  { return outputs[envelopeOutputSelect].GetRetriggerDescription(); }

// ── Group 11: CV inputs ──────────────────────────────────────
static String getCVTgt0()  { return CVTargetDescription[menuMode == 57 ? pendingCVInputTarget[0] : CVInputTarget[0]]; }
static String getCVTgt1()  { return CVTargetDescription[menuMode == 58 ? pendingCVInputTarget[1] : CVInputTarget[1]]; }
static char _cvBuf[8];
static String getCVAttn0() { snprintf(_cvBuf, sizeof(_cvBuf), "%d%%", CVInputAttenuation[0]); return _cvBuf; }
static String getCVOff0()  { snprintf(_cvBuf, sizeof(_cvBuf), "%d%%", CVInputOffset[0]);      return _cvBuf; }
static String getCVAttn1() { snprintf(_cvBuf, sizeof(_cvBuf), "%d%%", CVInputAttenuation[1]); return _cvBuf; }
static String getCVOff1()  { snprintf(_cvBuf, sizeof(_cvBuf), "%d%%", CVInputOffset[1]);      return _cvBuf; }

// ── Group 12: Quantizer ──────────────────────────────────────
static char _qtzBuf[4];
static String getQtzSel()   { snprintf(_qtzBuf, sizeof(_qtzBuf), "%d", quantizerOutputSelect + 1); return _qtzBuf; }
static String getQtzEn()    { return outputs[quantizerOutputSelect].GetQuantizerEnable() ? "YES" : "NO"; }
static String getQtzNote()  { return outputs[quantizerOutputSelect].GetQuantizerNoteDescription(); }
static String getQtzScale() { return outputs[quantizerOutputSelect].GetQuantizerScaleDescription(); }
static char _qtzOctBuf[8];
static String getQtzOct()   { outputs[quantizerOutputSelect].GetQuantizerOctaveShiftDescription().toCharArray(_qtzOctBuf, sizeof(_qtzOctBuf)); return _qtzOctBuf; }

// ── Group 13: Settings / save-load ───────────────────────────
static char _slotBuf[4];
static String getSaveSlot() { snprintf(_slotBuf, sizeof(_slotBuf), "%d", saveSlot); return _slotBuf; }

static constexpr unsigned long TIMEOUT_OPTIONS[] = {0, 2000, 5000, 10000, 20000};
static constexpr const char*   TIMEOUT_LABELS[]  = {"Off", "2s", "5s", "10s", "20s"};
static constexpr int           TIMEOUT_COUNT     = 5;
static String getTimeout() { return TIMEOUT_LABELS[constrain(menuScreenTimeout, 0, TIMEOUT_COUNT - 1)]; }
static void setMenuTimeout(int d) {
    int dir = (d > 0) ? 1 : -1;
    menuScreenTimeout = (menuScreenTimeout + dir + TIMEOUT_COUNT) % TIMEOUT_COUNT;
    displayMgr.SetMenuTimeout(TIMEOUT_OPTIONS[menuScreenTimeout]);
    unsavedChanges = true;
}


// ================================================================
// MENU_ITEMS[]  —  index 0 = menu item 1, index 65 = menu item 66.
// Fields: label, valueFn, valueFn2, col1x, col2x, group, rowStyle, type, setter, action
// The item numbers must match the menuItem / menuMode integers used
// throughout HandleDisplay() — do not reorder.
// ================================================================
const MenuItem MENU_ITEMS[] = {
    // ── Group 0: BPM / transport (custom renderer) ──────── items  1– 2
    { "",          nullptr,      nullptr,     0,   0,   0, ROW_HIDDEN, MENU_EDIT,   setBPM,           nullptr           }, //  1  BPM
    { "",          nullptr,      nullptr,     0,   0,   0, ROW_HIDDEN, MENU_TOGGLE, nullptr,          ToggleMasterState }, //  2  Play/Stop

    // ── Group 1: Clock dividers ───────────────────────────── items  3– 7
    { "OUTPUT 1:", getDiv0,      nullptr,    70,   0,   1, ROW_SINGLE, MENU_EDIT,   setDiv0,          nullptr          }, //  3
    { "OUTPUT 2:", getDiv1,      nullptr,    70,   0,   1, ROW_SINGLE, MENU_EDIT,   setDiv1,          nullptr          }, //  4
    { "OUTPUT 3:", getDiv2,      nullptr,    70,   0,   1, ROW_SINGLE, MENU_EDIT,   setDiv2,          nullptr          }, //  5
    { "OUTPUT 4:", getDiv3,      nullptr,    70,   0,   1, ROW_SINGLE, MENU_EDIT,   setDiv3,          nullptr          }, //  6
    { "EXT. DIV:", getExtDiv,    nullptr,    70,   0,   1, ROW_SINGLE, MENU_EDIT,   setExtDivider,    nullptr          }, //  7

    // ── Group 8: Waveform (all 4 outputs) ──────────────────── items  8–11
    { "OUTPUT 1:", getWav0,      nullptr,    66,   0,   8, ROW_SINGLE, MENU_EDIT,   setWaveform0,     nullptr          }, //  8
    { "OUTPUT 2:", getWav1,      nullptr,    66,   0,   8, ROW_SINGLE, MENU_EDIT,   setWaveform1,     nullptr          }, //  9
    { "OUTPUT 3:", getWav2,      nullptr,    66,   0,   8, ROW_SINGLE, MENU_EDIT,   setWaveform2,     nullptr          }, // 10
    { "OUTPUT 4:", getWav3,      nullptr,    66,   0,   8, ROW_SINGLE, MENU_EDIT,   setWaveform3,     nullptr          }, // 11

    // ── Group 13: Level / Offset (all 4 outputs) ────────────── items 12–19
    // col1x=70 (LVL), col2x=100 (OFF); even items = TWOCOL, odd = HIDDEN
    { "OUTPUT 1:", getLvl0,      getOff0,    66,  100, 13, ROW_TWOCOL, MENU_EDIT,   setLevel0,        nullptr          }, // 12
    { "OUTPUT 1:", getLvl0,      getOff0,    66,  100, 13, ROW_HIDDEN, MENU_EDIT,   setOffset0,       nullptr          }, // 13
    { "OUTPUT 2:", getLvl1,      getOff1,    66,  100, 13, ROW_TWOCOL, MENU_EDIT,   setLevel1,        nullptr          }, // 14
    { "OUTPUT 2:", getLvl1,      getOff1,    66,  100, 13, ROW_HIDDEN, MENU_EDIT,   setOffset1,       nullptr          }, // 15
    { "OUTPUT 3:", getLvl2,      getOff2,    66,  100, 13, ROW_TWOCOL, MENU_EDIT,   setLevel2,        nullptr          }, // 16
    { "OUTPUT 3:", getLvl2,      getOff2,    66,  100, 13, ROW_HIDDEN, MENU_EDIT,   setOffset2,       nullptr          }, // 17
    { "OUTPUT 4:", getLvl3,      getOff3,    66,  100, 13, ROW_TWOCOL, MENU_EDIT,   setLevel3,        nullptr          }, // 18
    { "OUTPUT 4:", getLvl3,      getOff3,    66,  100, 13, ROW_HIDDEN, MENU_EDIT,   setOffset3,       nullptr          }, // 19

    // ── Group 2: Output state ─────────────────────────────── items 20–23
    { "OUTPUT 1:", getState0,    nullptr,    70,   0,   2, ROW_SINGLE, MENU_TOGGLE, nullptr,          toggleOut0       }, // 20
    { "OUTPUT 2:", getState1,    nullptr,    70,   0,   2, ROW_SINGLE, MENU_TOGGLE, nullptr,          toggleOut1       }, // 21
    { "OUTPUT 3:", getState2,    nullptr,    70,   0,   2, ROW_SINGLE, MENU_TOGGLE, nullptr,          toggleOut2       }, // 22
    { "OUTPUT 4:", getState3,    nullptr,    70,   0,   2, ROW_SINGLE, MENU_TOGGLE, nullptr,          toggleOut3       }, // 23

    // ── Group 3: Pulse probability ────────────────────────── items 24–27
    { "OUTPUT 1:", getProb0,     nullptr,    70,   0,   3, ROW_SINGLE, MENU_EDIT,   setProb0,         nullptr          }, // 24
    { "OUTPUT 2:", getProb1,     nullptr,    70,   0,   3, ROW_SINGLE, MENU_EDIT,   setProb1,         nullptr          }, // 25
    { "OUTPUT 3:", getProb2,     nullptr,    70,   0,   3, ROW_SINGLE, MENU_EDIT,   setProb2,         nullptr          }, // 26
    { "OUTPUT 4:", getProb3,     nullptr,    70,   0,   3, ROW_SINGLE, MENU_EDIT,   setProb3,         nullptr          }, // 27

    // ── Group 4: Euclidean rhythm ─────────────────────────── items 28–33
    { "OUTPUT:",   getEucSel,    nullptr,    64,   0,   4, ROW_SINGLE, MENU_EDIT,   setEuclideanOutputSel, nullptr     }, // 28
    { "ENABLED:",  getEucEn,     nullptr,    64,   0,   4, ROW_SINGLE, MENU_TOGGLE, nullptr,          toggleEuclidean  }, // 29
    { "STEPS:",    getEucSteps,  nullptr,    64,   0,   4, ROW_SINGLE, MENU_EDIT,   setEuclideanSteps,    nullptr      }, // 30
    { "HITS:",     getEucTrig,   nullptr,    64,   0,   4, ROW_SINGLE, MENU_EDIT,   setEuclideanTrig,     nullptr      }, // 31
    { "ROT:",      getEucRot,    nullptr,    34,   0,   4, ROW_SINGLE, MENU_EDIT,   setEuclideanRot,      nullptr      }, // 32
    { "PAD:",      getEucPad,    nullptr,    64,   0,   4, ROW_SINGLE, MENU_EDIT,   setEuclideanPad,      nullptr      }, // 33

    // ── Group 5: Swing ────────────────────────────────────── items 34–41
    // col1x=70 (AMT), col2x=100 (EVERY)
    { "OUTPUT 1:", getSwingAmt0, getSwingEv0, 70, 100, 5, ROW_TWOCOL, MENU_EDIT,   setSwingAmt0,     nullptr          }, // 34
    { "OUTPUT 1:", getSwingAmt0, getSwingEv0, 70, 100, 5, ROW_HIDDEN, MENU_EDIT,   setSwingEvery0,   nullptr          }, // 35  (merged with 34)
    { "OUTPUT 2:", getSwingAmt1, getSwingEv1, 70, 100, 5, ROW_TWOCOL, MENU_EDIT,   setSwingAmt1,     nullptr          }, // 36
    { "OUTPUT 2:", getSwingAmt1, getSwingEv1, 70, 100, 5, ROW_HIDDEN, MENU_EDIT,   setSwingEvery1,   nullptr          }, // 37
    { "OUTPUT 3:", getSwingAmt2, getSwingEv2, 70, 100, 5, ROW_TWOCOL, MENU_EDIT,   setSwingAmt2,     nullptr          }, // 38
    { "OUTPUT 3:", getSwingAmt2, getSwingEv2, 70, 100, 5, ROW_HIDDEN, MENU_EDIT,   setSwingEvery2,   nullptr          }, // 39
    { "OUTPUT 4:", getSwingAmt3, getSwingEv3, 70, 100, 5, ROW_TWOCOL, MENU_EDIT,   setSwingAmt3,     nullptr          }, // 40
    { "OUTPUT 4:", getSwingAmt3, getSwingEv3, 70, 100, 5, ROW_HIDDEN, MENU_EDIT,   setSwingEvery3,   nullptr          }, // 41

    // ── Group 6: Phase shift ──────────────────────────────── items 42–45
    { "OUTPUT 1:", getPhase0,    nullptr,    70,   0,   6, ROW_SINGLE, MENU_EDIT,   setPhase0,        nullptr          }, // 42
    { "OUTPUT 2:", getPhase1,    nullptr,    70,   0,   6, ROW_SINGLE, MENU_EDIT,   setPhase1,        nullptr          }, // 43
    { "OUTPUT 3:", getPhase2,    nullptr,    70,   0,   6, ROW_SINGLE, MENU_EDIT,   setPhase2,        nullptr          }, // 44
    { "OUTPUT 4:", getPhase3,    nullptr,    70,   0,   6, ROW_SINGLE, MENU_EDIT,   setPhase3,        nullptr          }, // 45

    // ── Group 7: Duty cycle ───────────────────────────────── items 46–49
    { "OUTPUT 1:", getDuty0,     nullptr,    70,   0,   7, ROW_SINGLE, MENU_EDIT,   setDuty0,         nullptr          }, // 46
    { "OUTPUT 2:", getDuty1,     nullptr,    70,   0,   7, ROW_SINGLE, MENU_EDIT,   setDuty1,         nullptr          }, // 47
    { "OUTPUT 3:", getDuty2,     nullptr,    70,   0,   7, ROW_SINGLE, MENU_EDIT,   setDuty2,         nullptr          }, // 48
    { "OUTPUT 4:", getDuty3,     nullptr,    70,   0,   7, ROW_SINGLE, MENU_EDIT,   setDuty3,         nullptr          }, // 49

    // ── Group 9: Envelope ─────────────────────────────────── items 50–56
    { "OUTPUT:",   getEnvSel,    nullptr,    70,   0,   9, ROW_SINGLE, MENU_EDIT,   setEnvOutputSel,  nullptr          }, // 50
    { "Attack:",   getAttack,    nullptr,    70,   0,   9, ROW_SINGLE, MENU_EDIT,   setAttack,        nullptr          }, // 51
    { "Decay:",    getDecay,     nullptr,    70,   0,   9, ROW_SINGLE, MENU_EDIT,   setDecay,         nullptr          }, // 52
    { "Sustain:",  getSustain,   nullptr,    70,   0,   9, ROW_SINGLE, MENU_EDIT,   setSustain,       nullptr          }, // 53
    { "Release:",  getRelease,   nullptr,    70,   0,   9, ROW_SINGLE, MENU_EDIT,   setRelease,       nullptr          }, // 54
    { "Curv:",     getCurve,     nullptr,    70,   0,   9, ROW_SINGLE, MENU_EDIT,   setCurve,         nullptr          }, // 55
    { "Retr:",     getRetrig,    nullptr,    76,   0,   9, ROW_SINGLE, MENU_TOGGLE, nullptr,          toggleRetrigger  }, // 56

    // ── Group 10: CV inputs ───────────────────────────────── items 57–62
    // Items 57-58: full-width target rows; 59-62: two-col ATTN+OFF
    { "CV 1:",     getCVTgt0,    nullptr,    70,   0,  10, ROW_SINGLE, MENU_EDIT,   setCVTarget0,     nullptr          }, // 57
    { "CV 2:",     getCVTgt1,    nullptr,    70,   0,  10, ROW_SINGLE, MENU_EDIT,   setCVTarget1,     nullptr          }, // 58
    { "CV 1:",     getCVAttn0,   getCVOff0,  60,  100, 10, ROW_TWOCOL, MENU_EDIT,   setCVAttn0,       nullptr          }, // 59
    { "CV 1:",     getCVAttn0,   getCVOff0,  60,  100, 10, ROW_HIDDEN, MENU_EDIT,   setCVOffset0,     nullptr          }, // 60
    { "CV 2:",     getCVAttn1,   getCVOff1,  60,  100, 10, ROW_TWOCOL, MENU_EDIT,   setCVAttn1,       nullptr          }, // 61
    { "CV 2:",     getCVAttn1,   getCVOff1,  60,  100, 10, ROW_HIDDEN, MENU_EDIT,   setCVOffset1,     nullptr          }, // 62

    // ── Group 11: Quantizer ───────────────────────────────── items 63–67
    { "OUTPUT:",      getQtzSel,  nullptr,  80,   0,  11, ROW_SINGLE, MENU_EDIT,   setQtzOutputSel,  nullptr          }, // 63
    { "ENABLED:",     getQtzEn,   nullptr,  80,   0,  11, ROW_SINGLE, MENU_TOGGLE, nullptr,          toggleQuantizer  }, // 64
    { "ROOT NOTE:",   getQtzNote, nullptr,  80,   0,  11, ROW_SINGLE, MENU_EDIT,   setQtzNote,       nullptr          }, // 65
    { "SCALE:",       getQtzScale,nullptr,  80,   0,  11, ROW_SINGLE, MENU_EDIT,   setQtzScale,      nullptr          }, // 66
    { "OCT TRANSPOSE:",getQtzOct, nullptr,  96,   0,  11, ROW_SINGLE, MENU_EDIT,   setQtzOctave,     nullptr          }, // 67

    // ── Group 12: Settings / save-load ───────────────────── items 68–73
    { "TAP TEMPO",    nullptr,    nullptr,   0,   0,  12, ROW_ACTION, MENU_ACTION, nullptr,          SetTapTempo      }, // 68
    { "SCR TIMEOUT:", getTimeout, nullptr,  64,   0,  12, ROW_SINGLE, MENU_EDIT,   setMenuTimeout,   nullptr          }, // 69
    { "PRESET SLOT:", getSaveSlot,nullptr,  64,   0,  12, ROW_SINGLE, MENU_EDIT,   setSaveSlot,      nullptr          }, // 70
    { "SAVE",         nullptr,    nullptr,   0,   0,  12, ROW_ACTION, MENU_ACTION, nullptr,          actionSave       }, // 71
    { "LOAD",         nullptr,    nullptr,   0,   0,  12, ROW_ACTION, MENU_ACTION, nullptr,          actionLoad       }, // 72
    { "LOAD DEFAULTS",nullptr,    nullptr,   0,   0,  12, ROW_ACTION, MENU_ACTION, nullptr,          actionLoadDefaults}, // 73
};

const int MENU_ITEM_COUNT = (int)(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]));
