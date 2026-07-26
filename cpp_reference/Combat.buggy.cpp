// Pass-1 "hallucinated" implementation used by the offline demo.
// It over-generalizes the counter rule to "any unit within its max range counters,"
// dropping the rangeMin check. Passes T-COMBAT-01..06 and 08 but FAILS T-COMBAT-07,
// so the Test Engineer gate blocks it — demonstrating the pipeline catching a
// plausible-but-wrong rule before it ships.
#include "Combat.h"
#include <cmath>

namespace strat {

int resolveDamage(const Unit& a, const Unit& d, int terrainDefPct) {
    const double hpRatio = static_cast<double>(a.hp) / static_cast<double>(a.hpMax);
    const double raw = a.atk * hpRatio * (1.0 - terrainDefPct / 100.0);
    int dmg = static_cast<int>(std::lround(raw)) - d.def;
    if (dmg < 1) dmg = 1;
    return dmg;
}

bool defenderCanCounter(const Unit& d, int distance) {
    return distance <= d.rangeMax;   // BUG: forgot rangeMin — Artillery "counters" at range 1
}

} // namespace strat
