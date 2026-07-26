// Correct implementation — satisfies every invariant in combat_spec.md.
#include "Combat.h"
#include <cmath>

namespace strat {

int resolveDamage(const Unit& a, const Unit& d, int terrainDefPct) {
    const double hpRatio = static_cast<double>(a.hp) / static_cast<double>(a.hpMax);
    const double raw = a.atk * hpRatio * (1.0 - terrainDefPct / 100.0);
    int dmg = static_cast<int>(std::lround(raw)) - d.def;
    if (dmg < 1) dmg = 1;          // invariant 2: min-damage floor
    return dmg;
}

bool defenderCanCounter(const Unit& d, int distance) {
    // invariant 6 & 7: BOTH bounds of the range band must hold.
    return distance >= d.rangeMin && distance <= d.rangeMax;
}

} // namespace strat
