// Stratocracy — headless combat rules (GDD §4.1). Zero engine dependencies.
#pragma once

namespace strat {

// A combat participant. Pure data; no engine types.
struct Unit {
    int atk;       // attack power
    int def;       // defense (flat mitigation)
    int hp;        // current hit points
    int hpMax;     // maximum hit points
    int rangeMin;  // minimum attack range (hexes)
    int rangeMax;  // maximum attack range (hexes)
};

// Damage a single strike deals.
// terrainDefPct is the DEFENDER's hex defense (0,10,20,40). Deterministic.
int resolveDamage(const Unit& attacker, const Unit& defender, int terrainDefPct);

// Whether a surviving defender may counterattack an attacker at `distance`.
// True only when distance is inside the defender's [rangeMin, rangeMax] band.
bool defenderCanCounter(const Unit& defender, int distance);

} // namespace strat
