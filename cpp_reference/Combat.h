// Stratocracy — headless combat rules (GDD §4.1). Zero engine dependencies.
#pragma once

namespace strat {

// Unit type — order fixed, matches GDD §2.4. Used by effectiveness().
enum class UnitType { Infantry, Tank, Artillery, Recon };

// A combat participant. Pure data; no engine types.
struct Unit {
    int atk;       // attack power
    int def;       // defense (flat mitigation)
    int hp;        // current hit points
    int hpMax;     // maximum hit points
    int rangeMin;  // minimum attack range (hexes)
    int rangeMax;  // maximum attack range (hexes)
    // `type` is LAST with a default so existing 6-field aggregate initializers
    // ({atk,def,hp,hpMax,rangeMin,rangeMax}) still compile under -std=c++17.
    UnitType type = UnitType::Infantry;
};

// Damage a single strike deals.
// terrainDefPct is the DEFENDER's hex defense (0,10,20,40). Deterministic.
int resolveDamage(const Unit& attacker, const Unit& defender, int terrainDefPct);

// Whether a surviving defender may counterattack an attacker at `distance`.
// True only when distance is inside the defender's [rangeMin, rangeMax] band.
bool defenderCanCounter(const Unit& defender, int distance);

// Type-effectiveness multiplier for an attacker type vs a defender type.
// THIS REVISION ships neutral: returns 1.0 for every pair (RPS stays positional,
// GDD §2.4). Populate only when self-play shows the triangle too weak. Pure.
double effectiveness(UnitType attacker, UnitType defender);

// HP a unit recovers at the start of its turn (GDD §2.7). 0 when ineligible.
// Caller supplies the board facts:
//   onOwnedObjective — unit ends its turn on a Town/Factory it owns
//   enemyAdjacent    — an enemy unit occupies an adjacent hex
// Pure/deterministic; never overheals past hpMax.
int repairAmount(const Unit& u, bool onOwnedObjective, bool enemyAdjacent);

} // namespace strat
