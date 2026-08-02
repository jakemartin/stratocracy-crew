// Stratocracy — capture & Fame economy, PASS-1 HALLUCINATION (deliberately wrong).
//
// This file exists to be blocked. Both bugs are the plausible reading a careful
// author reaches WITHOUT the Director's rulings in front of them:
//
//   BUG 1 — income accrues on turn 1. Every strategy game pays you on turn 1, and
//           nothing in §2.7's income bullet looks like an exception. Q8 ruled the
//           other way: turn-1 buying power is starting Fame ALONE. Caught by
//           T-FAME-02.
//   BUG 2 — passive income also credits fameCombat, on the reading that Fame is
//           one pool so every source should touch every counter. §2.7 does say one
//           pool -- but fameCombat is §2.8's TIEBREAK sort key, so crediting income
//           to it lets a side that never fought win criterion 1, and makes the
//           §2.8 mutual-passivity guard unreachable. Caught by T-FAME-01.
//
// Both compile, both look reasonable, and both change who wins a capped match.
#include "Economy.h"

#include <algorithm>

namespace strat {

namespace {

Objective* mutableObjective(EconomyState& s, const Hex& h) {
    for (Objective& o : s.objectives) if (hexEqual(o.hex, h)) return &o;
    return nullptr;
}

CaptureProgress* mutableProgress(EconomyState& s, const Hex& h) {
    for (CaptureProgress& c : s.captures) if (hexEqual(c.hex, h)) return &c;
    return nullptr;
}

void clearProgress(EconomyState& s, const Hex& h) {
    s.captures.erase(std::remove_if(s.captures.begin(), s.captures.end(),
                     [&h](const CaptureProgress& c) { return hexEqual(c.hex, h); }),
                     s.captures.end());
}

bool isOccupied(const std::vector<Hex>& occupied, const Hex& h) {
    for (const Hex& o : occupied) if (hexEqual(o, h)) return true;
    return false;
}

bool validSide(int side) { return side >= 0 && side < SIDE_COUNT; }

} // namespace

const Objective* findObjective(const EconomyState& s, const Hex& h) {
    for (const Objective& o : s.objectives) if (hexEqual(o.hex, h)) return &o;
    return nullptr;
}

void initSide(EconomyState& s, int side, int startingFame) {
    if (!validSide(side)) return;
    s.side[side].fameTotal  = startingFame;   // the configured value; no literal 200 here
    s.side[side].fameCombat = 0;
}

int accrueIncome(EconomyState& s, const std::vector<TerrainDef>& terrain,
                 int side, int turnNumber) {
    if (!validSide(side)) return 0;
    (void)turnNumber;                     // <-- BUG 1: income accrues on turn 1 too.
    int income = 0;
    for (const Objective& o : s.objectives) {
        if (o.owner != side) continue;
        if (o.terrainIndex < 0 || static_cast<std::size_t>(o.terrainIndex) >= terrain.size()) continue;
        income += terrain[o.terrainIndex].incomeFame;
    }
    s.side[side].fameTotal  += income;
    s.side[side].fameCombat += income;    // <-- BUG 2: "it's all Fame, isn't it?"
    return income;
}

bool queueBuild(EconomyState& s, const std::vector<UnitDef>& units,
                const std::vector<TerrainDef>& terrain,
                int side, const Hex& factoryHex, int defIndex, std::string& err) {
    if (!validSide(side)) { err = "invalid side"; return false; }
    if (defIndex < 0 || static_cast<std::size_t>(defIndex) >= units.size()) {
        err = "no such unit type"; return false;
    }
    const Objective* o = findObjective(s, factoryHex);
    if (o == nullptr) { err = "no objective at that hex"; return false; }
    if (o->owner != side) { err = "factory is not held by this side"; return false; }
    if (o->terrainIndex < 0 || static_cast<std::size_t>(o->terrainIndex) >= terrain.size()) {
        err = "objective has no terrain row"; return false;
    }
    if (!terrain[o->terrainIndex].isSpawnPoint) { err = "not a build point"; return false; }
    // One build per factory per turn, read through T-FAME-04's holding clause: a
    // waiting build keeps the slot, so the slot is busy until it spawns.
    for (const PendingBuild& p : s.pending)
        if (hexEqual(p.factoryHex, factoryHex)) { err = "factory already has a pending build"; return false; }

    const int cost = units[defIndex].costFame;
    if (s.side[side].fameTotal < cost) { err = "unaffordable"; return false; }

    // Q8, ruled: Fame is committed at QUEUE time, not spawn time, and is not
    // refundable. Deducting here is the ruling, not an optimisation.
    s.side[side].fameTotal -= cost;
    PendingBuild p;
    p.factoryHex = factoryHex;
    p.side = side;
    p.defIndex = defIndex;
    s.pending.push_back(p);
    return true;
}

std::vector<SpawnResult> resolveBuilds(EconomyState& s, const MapBounds& bounds,
                                       const std::vector<Hex>& occupied) {
    std::vector<SpawnResult> out;
    std::vector<Hex> taken = occupied;          // later spawns see earlier ones
    std::vector<PendingBuild> stillWaiting;

    for (const PendingBuild& p : s.pending) {
        SpawnResult r;
        r.side = p.side;
        r.defIndex = p.defIndex;

        if (!isOccupied(taken, p.factoryHex)) {
            r.hex = p.factoryHex;
            r.spawned = true;
        } else {
            // "an adjacent free hex" -- §2.7 does not say which, and T-FAME-09
            // requires a reproducible answer, so: the canonically smallest free
            // neighbour (r asc, then q asc). A documented choice, not a new rule.
            Hex adj[HEX_DIRECTIONS];
            const int n = neighbors(p.factoryHex, bounds, adj);
            std::vector<Hex> free;
            for (int i = 0; i < n; ++i) if (!isOccupied(taken, adj[i])) free.push_back(adj[i]);
            sortCanonical(free);
            if (!free.empty()) { r.hex = free.front(); r.spawned = true; }
        }

        if (r.spawned) taken.push_back(r.hex);
        else           stillWaiting.push_back(p);   // boxed in: waits, and KEEPS the slot
        out.push_back(r);
    }
    s.pending = stillWaiting;
    return out;
}

std::vector<Hex> captureTick(EconomyState& s, const std::vector<CaptureOccupant>& occupants,
                             int side) {
    std::vector<Hex> flipped;
    if (!validSide(side)) return flipped;

    for (Objective& o : s.objectives) {
        const CaptureOccupant* here = nullptr;
        for (const CaptureOccupant& c : occupants)
            if (hexEqual(c.hex, o.hex)) { here = &c; break; }

        // Q4, ruled: progress is tile-held and resets to ZERO the moment the
        // capturing Infantry leaves or dies. No occupant, wrong side, or a unit
        // that cannot capture -- all reset. It never survives an interruption.
        if (here == nullptr || here->side != side || !here->canCapture) {
            clearProgress(s, o.hex);
            continue;
        }
        if (o.owner == side) { clearProgress(s, o.hex); continue; }   // already ours

        CaptureProgress* prog = mutableProgress(s, o.hex);
        if (prog == nullptr) {
            CaptureProgress fresh;
            fresh.hex = o.hex;
            fresh.unitId = here->unitId;
            fresh.turnsHeld = 1;
            s.captures.push_back(fresh);
            prog = &s.captures.back();
        } else if (prog->unitId != here->unitId) {
            // A DIFFERENT unit is standing here: progress never transfers, so this
            // starts over rather than continuing (Q4).
            prog->unitId = here->unitId;
            prog->turnsHeld = 1;
        } else {
            prog->turnsHeld += 1;
        }

        if (prog->turnsHeld >= s.captureTurns) {
            o.owner = side;               // income flips with ownership (T-FAME-06)
            clearProgress(s, o.hex);
            flipped.push_back(o.hex);
        }
    }
    sortCanonical(flipped);
    return flipped;
}

int killAward(const UnitDef& victim, bool victimIsFlag) {
    // Q5, ruled: the flag award REPLACES the ordinary one rather than stacking, so
    // a flag Tank pays 500, not 650. Q6, ruled: there is no undamaged-strike bonus
    // -- it was cut rather than priced, so nothing is added here for a clean strike.
    if (victimIsFlag) return FLAG_KILL_AWARD;
    return victim.costFame / 2;           // 100/150/200/300 -> 50/75/100/150, all integers
}

void awardKill(EconomyState& s, int killerSide, const UnitDef& victim, bool victimIsFlag) {
    if (!validSide(killerSide)) return;
    const int award = killAward(victim, victimIsFlag);
    s.side[killerSide].fameTotal  += award;   // one pool...
    s.side[killerSide].fameCombat += award;   // ...and the tiebreak counter too
}

} // namespace strat
