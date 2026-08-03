// Stratocracy — baseline opponent AI implementation (GDD §4.7 Stub 6, §2.9).
// Pure function of state; no RNG. Deliberately un-clever: decisive, readable and
// fully testable (§2.9). Every rule decision is a call into rows 1-5.
#include "Ai.h"

#include <algorithm>
#include <cstddef>

namespace strat {

namespace {

bool validSide(int side) { return side >= 0 && side < SIDE_COUNT; }

Board boardOf(const AiState& s) {
    Board b;
    b.bounds  = s.bounds;
    b.terrain = s.terrain;
    b.occupant.assign(s.terrain.size(), OCCUPANT_NONE);
    for (const AiUnit& u : s.units) {
        const int i = b.index(u.hex);
        if (i >= 0) b.occupant[i] = u.id;
    }
    return b;
}

// A Combat.h participant built from the loaded UnitDef plus current HP. The AI
// stores no stat of its own; every field is looked up.
Unit combatUnit(const AiState& s, const AiUnit& u) {
    const UnitDef& d = s.unitDefs[u.defIndex];
    Unit c;
    c.atk = d.atk; c.def = d.def; c.hp = u.hp; c.hpMax = d.hpMax;
    c.rangeMin = d.rangeMin; c.rangeMax = d.rangeMax; c.type = d.type;
    return c;
}

int terrainDefPctAt(const AiState& s, const Hex& h) {
    Board b; b.bounds = s.bounds; b.terrain = s.terrain;
    b.occupant.assign(s.terrain.size(), OCCUPANT_NONE);
    const int t = b.terrainAt(h);
    if (t < 0 || static_cast<std::size_t>(t) >= s.terrainDefs.size()) return 0;
    return s.terrainDefs[t].defensePct;
}

bool builtHereThisTurn(const AiState& s, const Hex& h) {
    for (const Hex& b : s.builtThisTurn) if (hexEqual(b, h)) return true;
    return false;
}

// Every enemy of `side`, in canonical hex order -- so every scan below is ordered
// before it is scored, and a tie is broken by position rather than by vector order.
std::vector<const AiUnit*> enemiesOf(const AiState& s, int side) {
    std::vector<const AiUnit*> out;
    for (const AiUnit& u : s.units) if (u.side != side) out.push_back(&u);
    std::stable_sort(out.begin(), out.end(), [](const AiUnit* a, const AiUnit* b) {
        return hexLess(a->hex, b->hex);
    });
    return out;
}

// The side's own units that may still do SOMETHING, in canonical hex order. Turn.h
// decides; the AI only asks. Two flags mean two questions: a unit that has moved but
// not acted is still an attacker, and a unit that has attacked from where it stands
// may still move. Asking `canAct` alone -- which was the whole question while one
// shared flag existed -- re-offers a unit that has already moved and gets its second
// move refused, so the branches in `unitAction` gate on the matching flag too.
std::vector<const AiUnit*> actableOf(const AiState& s, int side) {
    std::vector<const AiUnit*> out;
    for (const AiUnit& u : s.units)
        if (u.side == side && (canAct(s.turn, u.id, u.side) ||
                               canMove(s.turn, u.id, u.side))) out.push_back(&u);
    std::stable_sort(out.begin(), out.end(), [](const AiUnit* a, const AiUnit* b) {
        return hexLess(a->hex, b->hex);
    });
    return out;
}

const AiUnit* occupantAt(const AiState& s, const Hex& h) {
    for (const AiUnit& u : s.units) if (hexEqual(u.hex, h)) return &u;
    return nullptr;
}

// §2.9's "undefended", which the GDD requires and does not define. Documented
// choice (spec/ai_spec.md): no enemy stands on the objective and none is adjacent
// to it. Occupancy alone would make the word do no work, since Move.h already
// refuses to enter an occupied hex.
bool undefended(const AiState& s, const Hex& objective, int side) {
    if (occupantAt(s, objective) != nullptr) return false;
    Hex adj[HEX_DIRECTIONS];
    const int n = neighbors(objective, s.bounds, adj);
    for (int i = 0; i < n; ++i) {
        const AiUnit* u = occupantAt(s, adj[i]);
        if (u != nullptr && u->side != side) return false;
    }
    return true;
}

// §2.9's unit phase for ONE unit, in the order §2.9 lists it. Returns false when
// this unit has nothing to do, so the caller offers the next one.
bool unitAction(const AiState& s, int side, const Board& board,
                const std::vector<const AiUnit*>& enemies,
                const AiUnit& unit, AiCommand& out) {
    const UnitDef& ud = s.unitDefs[unit.defIndex];

    // (1) an Infantry already standing on an objective it does not own HOLDS. The
    // capture completes at the turn boundary (§2.7); walking away would reset the
    // progress it is accruing (Q4).
    if (ud.canCapture) {
        const Objective* here = findObjective(s.economy, unit.hex);
        if (here != nullptr && here->owner != side) return false;
    }

    // (2) attack, if anything is in range from where the unit stands -- and only if
    // the act flag is unspent. §2.9 reaches an attack "after moving", but the flags
    // are independent, so a unit that already attacked and can still move falls
    // through to (3)/(4) rather than attacking twice.
    if (canAct(s.turn, unit.id, unit.side)) {
        const AiUnit* best = nullptr;
        int bestDamage = 0;
        for (const AiUnit* e : enemies) {                  // canonical order already
            const int dmg = expectedDamage(s, unit, *e);
            if (dmg <= 0) continue;
            if (isStrictlyLosing(s, unit, *e)) continue;   // §2.9's one guard
            // The enemy flag outright; otherwise the best expected damage, ties
            // broken by the target's canonical hex order -- which the ordered scan
            // already gives, since a later equal never displaces an earlier one (Q9).
            if (e->isFlag) { best = e; break; }
            if (dmg > bestDamage) { bestDamage = dmg; best = e; }
        }
        if (best != nullptr) {
            out = AiCommand();
            out.kind = AiCommandKind::Attack;
            out.unitId = unit.id;
            out.targetId = best->id;
            return true;
        }
    }

    // Everything below moves the unit, so a spent move flag ends its turn here.
    if (!canMove(s.turn, unit.id, unit.side)) return false;

    const std::vector<ReachEntry> reach =
        reachable(board, s.terrainDefs, unit.hex, ud.move);        // Move.h decides

    // (3) an Infantry moves onto the NEAREST uncaptured, undefended objective it can
    // reach. §2.9 says "near", so nearness decides and canonical order only breaks a
    // tie between two equally near ones -- picking the canonically first outright
    // would send a unit past a factory beside it to one across the map.
    if (ud.canCapture) {
        const Hex* target = nullptr;
        int bestCost = 0;
        for (const Objective& o : s.economy.objectives) {
            if (o.owner == side) continue;
            if (!undefended(s, o.hex, side)) continue;
            for (const ReachEntry& r : reach) {
                if (!hexEqual(r.hex, o.hex) || hexEqual(r.hex, unit.hex)) continue;
                // `reach` arrives in canonical order, so strictly-cheaper-only keeps
                // the canonically first among equals (Q9).
                if (target == nullptr || r.cost < bestCost) { bestCost = r.cost; target = &o.hex; }
            }
        }
        if (target != nullptr) {
            out = AiCommand();
            out.kind = AiCommandKind::Move;
            out.unitId = unit.id;
            out.hex = *target;
            return true;
        }
    }

    // (4) otherwise advance toward the enemy flag, or -- when no flag is designated
    // (Q10 open on exactness) -- toward the canonically first enemy.
    {
        const AiUnit* goal = nullptr;
        for (const AiUnit* e : enemies) if (e->isFlag) { goal = e; break; }
        if (goal == nullptr && !enemies.empty()) goal = enemies.front();
        if (goal == nullptr) return false;

        // The band this unit wants to fight from: a melee unit wants adjacency, an
        // Artillery its rangeMax and no closer, so it does not walk into a counter
        // (§2.9). Closing inside the band is penalised, not merely unrewarded.
        const int standoff = (ud.rangeMax > 1) ? ud.rangeMax : 1;
        auto score = [&](const Hex& h) {
            const int d = hexDistance(h, goal->hex);
            return (d >= standoff) ? (d - standoff) : (standoff - d) * 2;
        };
        const Hex* bestHex = nullptr;
        int bestScore = 0;
        for (const ReachEntry& r : reach) {
            if (hexEqual(r.hex, unit.hex)) continue;
            const int sc = score(r.hex);
            // Strictly-better only, and `reach` arrives in canonical order, so an
            // equal-scoring later hex never displaces an earlier one (Q9).
            if (bestHex == nullptr || sc < bestScore) { bestScore = sc; bestHex = &r.hex; }
        }
        if (bestHex != nullptr && bestScore < score(unit.hex)) {
            out = AiCommand();
            out.kind = AiCommandKind::Move;
            out.unitId = unit.id;
            out.hex = *bestHex;
            return true;
        }
    }
    return false;
}

} // namespace

const char* commandKindName(AiCommandKind k) {
    switch (k) {
        case AiCommandKind::Build:   return "Build";
        case AiCommandKind::Move:    return "Move";
        case AiCommandKind::Attack:  return "Attack";
        case AiCommandKind::EndTurn: return "EndTurn";
    }
    return "EndTurn";
}

const AiUnit* findAiUnit(const AiState& s, int id) {
    for (const AiUnit& u : s.units) if (u.id == id) return &u;
    return nullptr;
}

bool buildPriorityLess(const UnitDef& a, const UnitDef& b) {
    // Q9, ruled: Infantry > Recon > Artillery > Tank, which is ASCENDING §2.4 cost
    // (100/150/200/300) and NOT the order §2.4's table prints (Infantry, Tank,
    // Artillery, Recon). Cost is read from the table, so the priority follows the
    // data rather than a list rewritten here.
    if (a.costFame != b.costFame) return a.costFame < b.costFame;
    return static_cast<int>(a.type) < static_cast<int>(b.type);   // pinned enum order
}

int expectedDamage(const AiState& s, const AiUnit& attacker, const AiUnit& defender) {
    const UnitDef& ad = s.unitDefs[attacker.defIndex];
    const int dist = hexDistance(attacker.hex, defender.hex);
    if (dist < ad.rangeMin || dist > ad.rangeMax) return 0;
    // Combat.h decides damage. There is no damage arithmetic in this file.
    return resolveDamage(combatUnit(s, attacker), combatUnit(s, defender),
                         terrainDefPctAt(s, defender.hex));
}

int exchangeValueDealt(const AiState& s, const AiUnit& attacker, const AiUnit& defender) {
    const int dmg = expectedDamage(s, attacker, defender);
    if (dmg <= 0) return 0;
    const UnitDef& dd = s.unitDefs[defender.defIndex];
    const int applied = (dmg < defender.hp) ? dmg : defender.hp;   // no credit past the kill
    const int award = killAward(dd, defender.isFlag);              // Economy.h prices it
    if (dd.hpMax <= 0) return 0;
    return award * applied / dd.hpMax;                             // prorated by HP share
}

int exchangeValueLost(const AiState& s, const AiUnit& attacker) {
    return killAward(s.unitDefs[attacker.defIndex], attacker.isFlag);
}

bool isStrictlyLosing(const AiState& s, const AiUnit& attacker, const AiUnit& defender) {
    const int dmg = expectedDamage(s, attacker, defender);
    if (dmg <= 0) return false;                       // not a legal strike; not this guard's call
    const int defHpAfter = defender.hp - dmg;
    if (defHpAfter <= 0) return false;                // the defender dies: no counter, no loss

    Unit wounded = combatUnit(s, defender);
    wounded.hp = defHpAfter;
    const int dist = hexDistance(attacker.hex, defender.hex);
    if (!defenderCanCounter(wounded, dist)) return false;          // Combat.h decides
    const int counter = resolveDamage(wounded, combatUnit(s, attacker),
                                      terrainDefPctAt(s, attacker.hex));
    if (attacker.hp - counter > 0) return false;                   // we survive: not losing

    // We die -- and ONLY now does the second half apply. Dying is not by itself
    // strictly losing; trading down is (§2.9).
    return exchangeValueLost(s, attacker) > exchangeValueDealt(s, attacker, defender);
}

int chooseBuild(const AiState& s, int side) {
    if (!validSide(side)) return -1;
    std::vector<int> affordable;
    for (int idx : s.buildlist) {
        if (idx < 0 || static_cast<std::size_t>(idx) >= s.unitDefs.size()) continue;
        if (s.unitDefs[idx].costFame <= s.economy.side[side].fameTotal) affordable.push_back(idx);
    }
    if (affordable.empty()) return -1;
    // §2.9: it spends and replaces losses instead of hoarding, so the cheapest
    // affordable buildlist unit is bought rather than saving for a dearer one.
    int best = affordable.front();
    for (int idx : affordable)
        if (buildPriorityLess(s.unitDefs[idx], s.unitDefs[best])) best = idx;
    return best;
}

AiCommand nextCommand(const AiState& s, int side) {
    AiCommand end;                                   // EndTurn by default
    if (!validSide(side)) return end;
    if (!s.turn.running || s.turn.activeSide != side) return end;

    // --- economy phase, FIRST (§2.9) ------------------------------------------
    // Objectives are visited in canonical hex order, so which factory builds first
    // is a property of the board and not of vector order.
    {
        std::vector<Hex> mine;
        for (const Objective& o : s.economy.objectives) {
            if (o.owner != side) continue;
            if (o.terrainIndex < 0 ||
                static_cast<std::size_t>(o.terrainIndex) >= s.terrainDefs.size()) continue;
            if (!s.terrainDefs[o.terrainIndex].isSpawnPoint) continue;   // the table decides
            mine.push_back(o.hex);
        }
        sortCanonical(mine);
        for (const Hex& f : mine) {
            if (builtHereThisTurn(s, f)) continue;             // one build per factory per turn
            bool pending = false;
            for (const PendingBuild& p : s.economy.pending)
                if (hexEqual(p.factoryHex, f)) pending = true;
            if (pending) continue;
            const int defIndex = chooseBuild(s, side);
            if (defIndex < 0) continue;
            AiCommand c;
            c.kind = AiCommandKind::Build;
            c.hex = f;
            c.defIndex = defIndex;
            return c;
        }
    }

    // --- unit phase -----------------------------------------------------------
    // Every unit that may still act is offered an action, in canonical hex order,
    // and the FIRST that yields one is returned. Scanning only the leading unit
    // would end the turn as soon as any single unit had nothing to do.
    const Board board = boardOf(s);
    const std::vector<const AiUnit*> enemies = enemiesOf(s, side);
    for (const AiUnit* unit : actableOf(s, side)) {
        AiCommand c;
        if (unitAction(s, side, board, enemies, *unit, c)) return c;
    }

    return end;
}

} // namespace strat
