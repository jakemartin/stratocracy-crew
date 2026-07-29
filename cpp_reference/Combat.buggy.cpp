// Pass-1 "hallucinated" implementation used by the offline demo. It bundles TWO
// plausible-but-wrong rules the gate must catch:
//   1) the counter rule drops the rangeMin check ("any unit within max range
//      counters") — Artillery "counters" at range 1 → FAILS T-COMBAT-07.
//   2) repair drops the !enemyAdjacent clause ("heal on any owned objective") →
//      a unit in enemy contact still heals → FAILS T-REPAIR-03 (anti-fortress).
// Everything else is correct, so it passes T-COMBAT-01..06, 08..10 and T-REPAIR
// -01,02,04,05,06,07 — the gate blocks it on exactly the two hallucinated rules.
#include "Combat.h"
#include <cmath>

namespace strat {

double effectiveness(UnitType /*attacker*/, UnitType /*defender*/) {
    return 1.0;   // neutral (correct) — the bugs are in the counter + repair rules
}

int resolveDamage(const Unit& a, const Unit& d, int terrainDefPct) {
    const double hpRatio = static_cast<double>(a.hp) / static_cast<double>(a.hpMax);
    const double eff = effectiveness(a.type, d.type);
    const double raw = a.atk * eff * hpRatio * (1.0 - terrainDefPct / 100.0);
    int dmg = static_cast<int>(std::lround(raw)) - d.def;
    if (dmg < 1) dmg = 1;
    return dmg;
}

bool defenderCanCounter(const Unit& d, int distance) {
    return distance <= d.rangeMax;   // BUG 1: forgot rangeMin — Artillery "counters" at range 1
}

int repairAmount(const Unit& u, bool onOwnedObjective, bool /*enemyAdjacent*/) {
    // BUG 2: dropped the !enemyAdjacent clause — a unit in enemy contact still heals,
    // turning a front-line factory into an unkillable fortress. Fails T-REPAIR-03.
    if (!onOwnedObjective || u.hp >= u.hpMax) return 0;
    int base = static_cast<int>(std::floor(0.25 * u.hpMax));
    if (base < 1) base = 1;
    const int room = u.hpMax - u.hp;
    return base < room ? base : room;
}

} // namespace strat
