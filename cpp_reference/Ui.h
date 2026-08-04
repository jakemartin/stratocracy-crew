// Stratocracy — headless UI binding contract (GDD §4.7 Stub 8, §4.11 row 8).
// Zero engine dependencies. Pure projection; no RNG, no clock, no I/O.
//
// This module owns NO RULES and NO BOARD. It is the contract for how every widget
// is fed: widgets bind to the view-model snapshot below plus the §4.9 event list,
// and hold no rules state (§4.1). Every value here is produced by the module that
// already owns it, and both queries delegate. A number this module computed for
// itself would be a defect even when it is the right number -- the point of the row
// is that the screen cannot disagree with the simulation.
//
// It is NOT layout and NOT visual design; that is §2.11's lane. See spec/ui_spec.md.
#pragma once

#include <string>
#include <vector>

#include "Combat.h"    // Unit, resolveDamage, defenderCanCounter -- verified at 5ffa8d6
#include "Data.h"      // UnitDef, TerrainDef
#include "Economy.h"   // EconomyState, Objective, CaptureProgress, SIDE_COUNT
#include "Hex.h"       // Hex, MapBounds, canonical order
#include "Move.h"      // Board, ReachEntry, reachable -- T-UI-02's set
#include "Scenario.h"  // ScenarioGuided -- the guidedOpening seat isGuidedMarked reads
#include "Turn.h"      // TurnState, ResultTier, hasMoved/hasActed -- T-TURN-01's two flags

namespace strat {

// ---------------------------------------------------------------------------
// The world this contract reads. An INPUT, not state this module owns: every
// member belongs to a row that has landed, and this module borrows all of them.
// ---------------------------------------------------------------------------
struct UiUnit {
    int  id       = 0;
    int  side     = 0;
    int  defIndex = 0;         // into `unitDefs`; the §2.4 row this unit is
    Hex  hex;
    Unit unit;                 // current hp/hpMax and the stat block combat reads
    bool isFlag   = false;     // Stub 7's placement field (Q10 open on exactness)
    // The Stub-7 placement this unit was DEPLOYED at, which is not `hex` once it
    // moves. It is an input, borrowed from the scenario file exactly as `isFlag` is,
    // and it exists because `isGuidedMarked` is a property OF THE PLACEMENT: the stub
    // says in as many words that it "does not move when the unit does". Deriving that
    // field from `hex` instead would unmark the marked Infantry the moment beat 1a
    // asked it to move, which is the one thing beat 1a does.
    Hex  placement;
};

struct UiWorld {
    Board                       board;
    std::vector<UiUnit>         units;
    const std::vector<UnitDef>*    unitDefs = nullptr;
    const std::vector<TerrainDef>* terrain  = nullptr;
    const EconomyState*         economy = nullptr;
    const TurnState*            turn    = nullptr;
    // The scenario file's `guidedOpening` entries (Stub 7), one per side. §4.9 names
    // the loaded scenario file as one of the three module-side sources the snapshot
    // may draw on, alongside `GameState` and the §4.8 tables. Null on a fixture with
    // no guided opening, which marks nobody rather than being an error.
    const std::vector<ScenarioGuided>* guided = nullptr;
};

// ---------------------------------------------------------------------------
// The view model = this SNAPSHOT plus the PRESENTATION BLOCK declared below it.
// §4.7 Stub 8 states both field lists and they are followed exactly; NO FIELD IS
// ADDED, and T-UI-05 clause (c) is what enforces that rather than this comment.
//
// The three values this module once filed as unwritten change requests -- the
// per-factory "has built this turn" record §2.11.5's BUILD pulse needs, §2.11.2's
// income rate, and §2.11.1's DONE bit -- were RULED on 2026-08-04 and are written
// into the stub. The first two are snapshot fields below; the DONE bit is the
// presentation block's, because it is derivable from neither turn flag nor from any
// pair of them (Wait and RMB-in-MOVED both reach it without spending the act flag).
//
// TWO KINDS, and every snapshot field has exactly one. A MIRROR -- the unmarked
// default -- equals, unchanged, the module-side value it names. A field marked
// DECLARED DERIVED is computed from those values instead and states its derivation
// beside it. "Derived on every projection rather than stored twice" is NOT the same
// property: `captureProgress` is recomputed on each pass and is still a MIRROR,
// because what it reports equals a value the module holds. The distinction is
// load-bearing -- T-UI-05 checks the two kinds by different rules.
// ---------------------------------------------------------------------------
struct UiHexView {
    Hex hex;
    int terrainId = 0;         // index into the loaded TerrainDef table
    int owner     = OWNER_NEUTRAL;   // capturable hexes only; OWNER_NEUTRAL elsewhere
};

struct UiUnitView {
    int  id       = 0;
    int  side     = 0;
    int  unitId   = 0;         // the stub's `unitId`: the §2.4 row index
    Hex  hex;
    int  hp       = 0;
    int  hpMax    = 0;
    bool isFlag   = false;
    // TWO INDEPENDENT FLAGS, not one (T-TURN-01). Read from TurnState's two sets and
    // never from each other: one field cannot express a unit that has spent exactly
    // one of them, which is the drift this row's GDD half repaired. NEITHER IS
    // §2.11.1's DONE bit -- that bit is the selection machine's own, every §2.11
    // surface reading "has not acted" binds to it, and it is deliberately absent
    // here because where per-unit presentation state lives is unruled.
    bool hasMoved = false;
    bool hasActed = false;
    // Progress is held by the TILE and names the unit that accumulated it (Q4,
    // T-FAME-05), so exactly one unit can carry a non-zero value and this per-unit
    // field expresses the tile's state without loss. A MIRROR: the lookup picks the
    // record, and what this reports equals that record's `turnsHeld` unchanged. It is
    // recomputed on every projection rather than stored twice, which is a different
    // property from DECLARED DERIVED and does not make it one.
    int  captureProgress = 0;
    // DECLARED DERIVED (ruled 2026-08-04). True exactly on the placement that the
    // scenario file's `guidedOpening.infantry` names for this unit's SEAT -- side and
    // deployment hex both -- and false on every other unit. A hex identifies one
    // placement because T-SCN-02 forbids two placements sharing one. Computed here
    // and never widget-side, and read off `placement`, not `hex`, so beat 1a's own
    // move cannot unmark the unit the beat is about.
    bool isGuidedMarked = false;
};

// per-factory (ruled 2026-08-04). §2.11.5's BUILD pulse and its boxed-in footer both
// need facts no other group carries.
struct UiFactoryView {
    Hex  hex;                        // mirrors the scenario file's factory placement
    int  owner = OWNER_NEUTRAL;      // mirrors the objective's owner
    // T-TURN-10's per-factory build allowance, and the §2.7 build that holds the
    // factory's slot until it spawns (T-FAME-04). Both MIRROR state the module
    // already holds -- this exposes it rather than adding it.
    bool hasBuiltThisTurn = false;
    bool buildWaiting     = false;
    // DECLARED DERIVED. True exactly when no hex at or adjacent to the factory is
    // free -- board geometry plus §2.7's spawn rule, which places at the factory hex
    // if free and otherwise at the canonically smallest free neighbour.
    //
    // DISTINCT from `buildWaiting`, and the difference is the case §2.11.5 must
    // display: a boxed-in factory with NOTHING QUEUED has `spawnBlocked` true and
    // `buildWaiting` false, which `buildWaiting` alone cannot express. Q31 asks
    // whether a player may queue into a boxed-in factory; `buildWaiting` is the field
    // such a ruling would bind to, and nothing here rules it -- today the waiting
    // build is an AI-only path and no gate asserts a player-queued one.
    bool spawnBlocked = false;
};

struct UiSideView {
    int fameTotal      = 0;
    int fameCombat     = 0;
    int objectivesHeld = 0;    // the X of "objectives held X of N"
    int survivingHp    = 0;
    // DECLARED DERIVED (ruled 2026-08-04). §2.7's rate over that side's held
    // factories (+100 each) and towns (+25 each), read from the terrain table's
    // `incomeFame` so no surface sums it widget-side (T-UI-03's no-arithmetic clause).
    //
    // It is the STANDING rate, and that is what it reads ON TURN 1: what those
    // holdings will pay at the start of that side's turn 2, NOT the 0 that Q8(a) pays
    // on turn 1. So it is deliberately NOT `accrueIncome`'s return value, whose
    // turn-1 guard would make this field read 0 for a side holding four factories.
    // The field is the rate the holdings carry and never the accrual of this turn.
    int incomePerTurn  = 0;
};

struct UiMatchView {
    int  turn       = 0;
    int  turnCap    = 0;
    int  sideToMove = 0;
    // The stub's `resultTier or null`. InProgress IS the null: §2.8's tier is
    // categorical and no numeric result exists to stand in for it.
    bool       hasResult  = false;
    ResultTier resultTier = ResultTier::InProgress;
};

struct UiSnapshot {
    std::vector<UiHexView>     hexes;      // canonical hex order
    std::vector<UiUnitView>    units;      // ascending unit id
    std::vector<UiFactoryView> factories;  // canonical hex order
    UiSideView side[SIDE_COUNT];
    int        objectiveTotal = 0;      // the N of "objectives held X of N"
    UiMatchView match;
};

// Read-only projection of the world into the view model. Enumerates hexes and
// factories in canonical order and units by ascending id, so two runs on the same
// state produce the same bytes. Pure: it mutates nothing it is given.
//
// It produces the SNAPSHOT ONLY. The presentation block below is deliberately not an
// output of this function: its two members have owners that are not this module, and
// a rules module that filled them in would be asserting state it does not hold.
UiSnapshot buildUiSnapshot(const UiWorld& w);

// ---------------------------------------------------------------------------
// The PRESENTATION BLOCK (§4.7 Stub 8). NOT produced by the rules module: two
// members, each with its owner named, and neither owner a widget.
//
// It is in the view-model rather than inside a widget precisely so that T-INT-05
// (§4.9) can rebuild the screen from the view-model alone -- state in the block
// satisfies that invariant, state in a widget does not.
//
// It is NOT in T-UI-05's subject. That invariant asks whether the snapshot tells the
// truth about the module's state; these members have no module-side counterpart and
// no derivation from one, so there is nothing for it to compare them against.
// ---------------------------------------------------------------------------
struct UiPresentationUnit {
    int id = 0;
    // OWNER: §2.11.1's selection machine, which is a state machine and not a widget.
    // This unit takes no further command this turn. PER-TURN: it clears when the
    // owner's next turn begins. It is the selection machine's only per-unit bit, and
    // it is DERIVABLE FROM NEITHER turn flag nor from any pair of them.
    bool done = false;
    // OWNER: the guidance layer, which is neither the rules module nor a widget.
    // §2.11.6 turn 1a's `Locked this turn.` -- not selectable under the guided
    // opening while beat 1a is outstanding.
    //
    // ITS LIFECYCLE IS NOT `done`'s, and the neighbouring member is the wrong thing
    // to copy it from: this clears when beat 1a RETIRES, which §2.11.6-B states is
    // when the marked Infantry's move completes -- inside turn 1, not at the turn
    // boundary. A unit can be un-locked and not-done in the same turn.
    bool lockedThisTurn = false;
};

struct UiPresentation {
    std::vector<UiPresentationUnit> units;   // ascending unit id
};

// The view-model T-INT-05 (§4.9) rebuilds from: snapshot + presentation block.
struct UiViewModel {
    UiSnapshot     snapshot;
    UiPresentation presentation;
};

// ---------------------------------------------------------------------------
// T-UI-05 — snapshot fidelity (§4.7 Stub 8, minted 2026-08-04)
//
// The snapshot tells the truth about the state the module holds -- the authoritative
// `GameState`, the §4.8 tables and the Stub-7 scenario file it loaded (§4.9) -- field
// by field, in three parts:
//
//   (a) MIRRORS         every unmarked field equals the module-side value it names,
//                       exactly, with nothing widened, narrowed, rounded or reordered
//   (b) DECLARED DERIVED every marked field equals the derivation stated beside it at
//                       the stub, RECOMPUTED INSIDE THE CHECK rather than read back
//                       out of the snapshot -- so a wrong derivation fails here and
//                       is not merely reproduced
//   (c) NO OTHER KIND   a snapshot field that is neither an unmarked mirror of a
//                       named module-side value nor a marked field with a stated
//                       derivation FAILS -- which is what stops a field entering the
//                       snapshot without a contract
//
// WHY THIS DOES NOT CALL buildUiSnapshot: a check that rebuilt the snapshot and
// compared it to itself would agree with any projection, correct or not. Every
// expected value is recomputed from the module that owns it, so the check and the
// projection can disagree. That is the entire content of the invariant.
//
// AND WHY IT DOES NOT CALL THE PROJECTION'S DERIVATION HELPERS EITHER. Clause (b)
// requires each derivation to be recomputed INSIDE the check "so that a wrong
// derivation fails here and is not merely reproduced" -- and a check that shares the
// projection's helper reproduces it exactly. Measured, not assumed: with the check
// routed through the shared helpers, the pass-1 variant's wrong `incomePerTurn` and
// wrong `isGuidedMarked` both passed this invariant, because the projection and the
// check were wrong together. The three DECLARED DERIVED derivations are therefore
// written out a second time inside the check, from the stub's words. The duplication
// is the point, not an oversight.
// ---------------------------------------------------------------------------

enum class UiFieldKind { Mirror, DeclaredDerived };

// One row of the stub's field list, transcribed. `source` names the module-side value
// a mirror equals, or states the derivation a marked field is recomputed from.
struct UiFieldContractEntry {
    const char* group;
    const char* field;
    UiFieldKind kind;
    const char* source;
};

// The stub lists 27 snapshot fields: per-hex 2, per-unit 11, per-factory 5,
// per-side 5, match 4. Of those 22 are mirrors and 5 are DECLARED DERIVED. The
// constant is asserted against BOTH the contract and the enumeration below, so a
// field added to one and not the others fails clause (c) rather than passing quietly.
constexpr int kUiSnapshotFieldCount = 27;
constexpr int kUiMirrorFieldCount   = 22;
constexpr int kUiDerivedFieldCount  = 5;

const std::vector<UiFieldContractEntry>& uiFieldContract();

// One field of the snapshot's observable surface. A stub field is not always one
// scalar -- `hex` is {q, r}, `objectivesHeld X of N` is {X, N}, `resultTier or null`
// is {hasResult, tier} -- so the values travel as a list at the stub's granularity
// rather than being split into fields the stub does not name.
struct UiFieldValue {
    const char*            group;
    int                    index;    // which hex/unit/factory/side; -1 for `match`
    const char*            field;
    std::vector<long long> scalars;
};

// The snapshot's observable surface, emitted field by field in a fixed order. This is
// the definition of "a snapshot field" that clause (c) quantifies over.
std::vector<UiFieldValue> uiEnumerateSnapshot(const UiSnapshot& s);

struct UiFidelityFailure {
    std::string clause;    // "a", "b" or "c"
    std::string field;     // "per-unit[2].hasActed"
    std::string detail;
};

struct UiFidelityResult {
    bool ok = false;
    int  mirrorsChecked = 0;
    int  derivedChecked = 0;
    int  fieldsEnumerated = 0;
    std::vector<UiFidelityFailure> failures;
};

UiFidelityResult uiCheckSnapshotFidelity(const UiWorld& w, const UiSnapshot& s);

// The standing income rate for `side` -- §2.7's rate over that side's held factories
// and towns. Exposed because `incomePerTurn` needs the rate the holdings CARRY, which
// `accrueIncome` cannot give: that function returns 0 on turn 1 by Q8(a) and mutates
// the purse besides. Pure; reads the terrain table's `incomeFame`.
int standingIncomeRate(const EconomyState& e, const std::vector<TerrainDef>& terrain,
                       int side);

// §2.7's spawn rule as a predicate: is every hex at or adjacent to `factoryHex`
// occupied? `spawnBlocked`'s derivation, and the gate recomputes it from here.
bool spawnHexesBlocked(const UiWorld& w, const Hex& factoryHex);

// ---------------------------------------------------------------------------
// Queries (§4.7 Stub 8). Both DELEGATE. There is deliberately no third query:
// T-UI-04's buildlist has no stated shape -- field or query -- and inventing one
// here would pre-empt a Director ruling.
// ---------------------------------------------------------------------------

// T-UI-02. Exactly Move.h::reachable's set for that unit -- hex for hex, cost for
// cost, canonical order -- because the UI queries the module and never recomputes
// movement (§2.5). Empty for an unknown unit id.
std::vector<ReachEntry> uiReachable(const UiWorld& w, int unitId);

struct UiForecast {
    bool        legal = false;
    std::string reason;              // why not, when illegal
    int         distance      = 0;
    int         damage        = 0;
    bool        defenderDies  = false;
    bool        counterFires  = false;
    int         counterDamage = 0;
};

// T-UI-01. The forecast shown before commit, produced by Combat.h::resolveDamage and
// Combat.h::defenderCanCounter (5ffa8d6) AND BY NOTHING ELSE, so "the forecast is
// exactly what resolves" (§2.6, §2.11) is structural rather than asserted. A local
// formula that happens to agree today is the defect this exists to catch: it agrees
// until §2.4 moves, and then the screen lies.
UiForecast uiForecast(const UiWorld& w, int attackerId, const Hex& defenderHex);

// What a resolution SPENDS, applied to a copy. It exists so the gate can measure
// "identical numbers" at both ends rather than only at the forecast end; it calls
// uiForecast and applies its numbers, adding no arithmetic of its own.
struct UiResolution {
    bool applied = false;
    UiForecast forecast;
    int attackerHpAfter = 0;
    int defenderHpAfter = 0;
};
UiResolution uiResolveForGate(const UiWorld& w, int attackerId, const Hex& defenderHex);

const UiUnit*     findUiUnit(const UiWorld& w, int unitId);
const UiUnitView* findUiUnitView(const UiSnapshot& s, int unitId);

} // namespace strat
