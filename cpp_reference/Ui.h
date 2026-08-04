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
};

struct UiWorld {
    Board                       board;
    std::vector<UiUnit>         units;
    const std::vector<UnitDef>*    unitDefs = nullptr;
    const std::vector<TerrainDef>* terrain  = nullptr;
    const EconomyState*         economy = nullptr;
    const TurnState*            turn    = nullptr;
};

// ---------------------------------------------------------------------------
// The view model. §4.7 Stub 8 states this field list; it is followed exactly, and
// NO FIELD IS ADDED. Three known-missing values are filed as unwritten change
// requests against the GDD rather than invented here: the per-factory "has built
// this turn" record §2.11.5's BUILD pulse needs, §2.11.2's income rate, and
// §2.11.1's DONE bit.
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
    // field expresses the tile's state without loss. DERIVED, never a second copy.
    int  captureProgress = 0;
};

struct UiSideView {
    int fameTotal      = 0;
    int fameCombat     = 0;
    int objectivesHeld = 0;    // the X of "objectives held X of N"
    int survivingHp    = 0;
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
    std::vector<UiHexView>  hexes;      // canonical hex order
    std::vector<UiUnitView> units;      // ascending unit id
    UiSideView side[SIDE_COUNT];
    int        objectiveTotal = 0;      // the N of "objectives held X of N"
    UiMatchView match;
};

// Read-only projection of the world into the view model. Enumerates hexes in
// canonical order and units by ascending id, so two runs on the same state produce
// the same bytes. Pure: it mutates nothing it is given.
UiSnapshot buildUiSnapshot(const UiWorld& w);

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
