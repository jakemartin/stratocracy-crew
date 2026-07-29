// Correct implementation — satisfies every invariant in combat_spec.md AND the
// addendum (spec/combat_spec_addendum.md): neutral type-effectiveness + repair.
#include "Combat.h"
#include <cmath>

namespace strat {

double effectiveness(UnitType /*attacker*/, UnitType /*defender*/) {
    // T-COMBAT-09: ships neutral — 1.0 for every pair. Do NOT invent balance values;
    // populate only when self-play requests it (GDD §2.4).
    return 1.0;
}

int resolveDamage(const Unit& a, const Unit& d, int terrainDefPct) {
    const double hpRatio = static_cast<double>(a.hp) / static_cast<double>(a.hpMax);
    const double eff = effectiveness(a.type, d.type);   // 1.0 this revision
    const double raw = a.atk * eff * hpRatio * (1.0 - terrainDefPct / 100.0);
    int dmg = static_cast<int>(std::lround(raw)) - d.def;
    if (dmg < 1) dmg = 1;          // invariant 2: min-damage floor
    return dmg;
}

bool defenderCanCounter(const Unit& d, int distance) {
    // invariant 6 & 7: BOTH bounds of the range band must hold.
    return distance >= d.rangeMin && distance <= d.rangeMax;
}

int repairAmount(const Unit& u, bool onOwnedObjective, bool enemyAdjacent) {
    // T-REPAIR-01/03/04: ineligible if not on an owned objective, in enemy contact,
    // or already at full HP. The !enemyAdjacent clause is the anti-fortress lock.
    if (!onOwnedObjective || enemyAdjacent || u.hp >= u.hpMax) return 0;
    int base = static_cast<int>(std::floor(0.25 * u.hpMax));  // +25% of max HP
    if (base < 1) base = 1;                                   // T-REPAIR-06: min-1 floor
    const int room = u.hpMax - u.hp;
    return base < room ? base : room;                         // T-REPAIR-05: never overheal
}

} // namespace strat
