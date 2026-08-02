// Stratocracy — headless capture & Fame economy (GDD §4.7 Stub 4, §4.11 row 4).
// Zero engine dependencies. Pure state transitions; no RNG anywhere.
//
// This module owns the ECONOMY, not the TURN. It never advances a turn and never
// decides whose turn it is -- row 5 does. The turn number arrives as an argument,
// and occupancy/identity arrive as caller-supplied facts, the same way
// Combat.h::repairAmount takes its board facts. That is what lets row 4 land
// before row 5. See spec/economy_spec.md.
#pragma once

#include <string>
#include <vector>

#include "Data.h"
#include "Hex.h"

namespace strat {

constexpr int OWNER_NEUTRAL = -1;
constexpr int SIDE_COUNT    = 2;
constexpr int FLAG_KILL_AWARD = 500;   // flat, and it REPLACES the ordinary award (Q5)

// One side's purse. `fameTotal` is THE pool (§2.7: production, rewards and
// win-score are one pool, not three). `fameCombat` is a separate counter that only
// combat awards touch -- it is §2.8's tiebreak criterion-1 sort key, and passive
// income must never reach it (T-FAME-01).
struct SideEconomy {
    int fameTotal  = 0;
    int fameCombat = 0;
};

// A capturable tile (§2.3 Town/Factory) and who holds it now.
struct Objective {
    Hex hex;
    int owner        = OWNER_NEUTRAL;
    int terrainIndex = 0;      // into the loaded TerrainDef table; income comes from there
};

// Capture progress is held by the TILE, not by the unit, and names the unit that
// accumulated it so progress can never transfer (Q4, T-FAME-05).
struct CaptureProgress {
    Hex hex;
    int unitId    = -1;
    int turnsHeld = 0;
};

// A queued build. Its Fame was committed when it was queued and is not refundable
// (Q8, T-FAME-04); it holds the factory's slot until it spawns.
struct PendingBuild {
    Hex factoryHex;
    int side     = 0;
    int defIndex = 0;
};

struct EconomyState {
    SideEconomy side[SIDE_COUNT];
    std::vector<Objective>        objectives;
    std::vector<CaptureProgress>  captures;
    std::vector<PendingBuild>     pending;
    int captureTurns = 1;      // N, per-scenario data; 1 on the shipped scenario
};

// What the caller knows about who is standing on a hex this turn.
struct CaptureOccupant {
    Hex  hex;
    int  unitId     = -1;
    int  side       = OWNER_NEUTRAL;
    bool canCapture = false;   // from the UnitDef; Infantry only (T-DATA-03)
};

struct SpawnResult {
    Hex  hex;
    int  side     = 0;
    int  defIndex = 0;
    bool spawned  = false;     // false == still waiting, slot still held
};

// Sets a side's opening Fame. The caller supplies the configured value -- 200 at
// Normal, 350/100 for the PLAYER on Easy/Hard (§2.9) -- so no literal 200 lives
// here (Q8, T-FAME-02).
void initSide(EconomyState& s, int side, int startingFame);

// Start-of-turn income for `side`. Returns the amount added. NO ACCRUAL ON TURN 1:
// a side's turn-1 buying power is its starting Fame alone (Q8). Touches fameTotal
// only, never fameCombat.
int accrueIncome(EconomyState& s, const std::vector<TerrainDef>& terrain,
                 int side, int turnNumber);

// Queues a build at an owned factory. Deducts the §2.4 cost immediately. Refuses --
// and changes nothing -- if the factory is not owned, is not a factory, already
// holds a pending build, or the cost is unaffordable.
bool queueBuild(EconomyState& s, const std::vector<UnitDef>& units,
                const std::vector<TerrainDef>& terrain,
                int side, const Hex& factoryHex, int defIndex, std::string& err);

// Tries to place every pending build: the factory hex if free, else the
// canonically smallest free neighbour, else it keeps waiting and keeps the slot.
std::vector<SpawnResult> resolveBuilds(EconomyState& s, const MapBounds& bounds,
                                       const std::vector<Hex>& occupied);

// Evaluates holding for `side` against the current occupancy. Returns the hexes
// that changed hands this tick.
std::vector<Hex> captureTick(EconomyState& s, const std::vector<CaptureOccupant>& occupants,
                             int side);

// Half the victim's §2.4 cost, or a flat 500 for a flag -- REPLACING the ordinary
// award, never stacking (Q5). No undamaged-strike bonus exists (Q6).
int killAward(const UnitDef& victim, bool victimIsFlag);

// Applies that award to the killer: fameTotal AND fameCombat (T-FAME-01).
void awardKill(EconomyState& s, int killerSide, const UnitDef& victim, bool victimIsFlag);

const Objective* findObjective(const EconomyState& s, const Hex& h);

} // namespace strat
