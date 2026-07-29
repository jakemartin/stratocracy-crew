// Test Engineer's gate — dependency-free (just <cstdio>). Exits non-zero on any
// failure so the crew's compile+run tool can block a merge mechanically.
#include "Combat.h"
#include <cstdio>

using namespace strat;

static int g_total = 0;
static int g_fail  = 0;

static void check(const char* name, bool cond) {
    ++g_total;
    if (cond) { std::printf("PASS  %s\n", name); }
    else      { ++g_fail; std::printf("FAIL  %s\n", name); }
}

int main() {
    // Unit definitions straight from GDD §2.4.
    const Unit inf {4, 2, 10, 10, 1, 1};
    const Unit tank{8, 5, 20, 20, 1, 1};
    const Unit arty{10, 1, 8,  8,  2, 3};
    const Unit wounded{8, 5, 10, 20, 1, 1}; // a Tank at half HP

    // 1 — determinism
    check("T-COMBAT-01 determinism",
          resolveDamage(inf, tank, 0) == resolveDamage(inf, tank, 0));

    // 2 — min-damage floor: inf(4) vs tank(def5) on plains -> round(4)-5 = -1 -> 1
    check("T-COMBAT-02 min-damage-floor",
          resolveDamage(inf, tank, 0) == 1);

    // 3 — defender terrain reduces damage
    check("T-COMBAT-03 terrain-reduces-damage",
          resolveDamage(tank, inf, 40) < resolveDamage(tank, inf, 0));

    // 4 — wounded attacker deals less
    check("T-COMBAT-04 wounded-attacker-weaker",
          resolveDamage(wounded, inf, 0) < resolveDamage(tank, inf, 0));

    // 5 — terrain param is the defender's hex (attacker HP, not attacker terrain,
    //     drives the ratio); sanity: raising defender terrain never raises damage.
    check("T-COMBAT-05 terrain-defender-only",
          resolveDamage(tank, inf, 20) <= resolveDamage(tank, inf, 0));

    // 6 — a range-1 unit counters at distance 1
    check("T-COMBAT-06 counter-within-range",
          defenderCanCounter(tank, 1) == true);

    // 7 — THE gate: Artillery (2-3) takes no counter from a range-1 attacker
    check("T-COMBAT-07 artillery-counter-immunity",
          defenderCanCounter(arty, 1) == false);

    // 8 — Artillery still counters inside its own band
    check("T-COMBAT-08 artillery-counters-at-range-2",
          defenderCanCounter(arty, 2) == true);

    // --- Addendum Part A: type-effectiveness (neutral this revision) -----------
    // 9 — the eff table ships all-1.0 (do NOT invent balance values)
    const UnitType TYPES[4] = {UnitType::Infantry, UnitType::Tank,
                               UnitType::Artillery, UnitType::Recon};
    bool allNeutral = true;
    for (UnitType a : TYPES)
        for (UnitType d : TYPES)
            if (effectiveness(a, d) != 1.0) allNeutral = false;
    check("T-COMBAT-09 eff-neutral-stub", allNeutral);

    // 10 — with the neutral table, resolveDamage is unchanged: tank->inf = round(8)-2 = 6
    check("T-COMBAT-10 eff-baseline-preserved",
          resolveDamage(tank, inf, 0) == 6);

    // --- Addendum Part B: repair (owned-objective healing) ---------------------
    const Unit tankHurt{8, 5, 10, 20, 1, 1, UnitType::Tank};  // Tank at half HP
    const Unit tankNick{8, 5, 18, 20, 1, 1, UnitType::Tank};  // Tank barely hurt
    const Unit tankFull{8, 5, 20, 20, 1, 1, UnitType::Tank};  // Tank at full HP
    const Unit small   {1, 1,  1,  3, 1, 1, UnitType::Infantry}; // tiny hpMax

    // 01 — no heal at full HP
    check("T-REPAIR-01 full-hp-no-heal",
          repairAmount(tankFull, true, false) == 0);
    // 02 — basic heal: floor(0.25*20)=5, room=10 -> 5
    check("T-REPAIR-02 basic-heal",
          repairAmount(tankHurt, true, false) == 5);
    // 03 — THE gate: an enemy-adjacent unit does not heal (anti-fortress)
    check("T-REPAIR-03 anti-fortress",
          repairAmount(tankHurt, true, true) == 0);
    // 04 — must be on an OWNED objective
    check("T-REPAIR-04 must-own",
          repairAmount(tankHurt, false, false) == 0);
    // 05 — never overheal: base 5 capped at room 2 -> 2
    check("T-REPAIR-05 no-overheal",
          repairAmount(tankNick, true, false) == 2);
    // 06 — min-1 floor: floor(0.25*3)=0 -> 1, room 2 -> 1
    check("T-REPAIR-06 min-1-floor",
          repairAmount(small, true, false) == 1);
    // 07 — determinism
    check("T-REPAIR-07 determinism",
          repairAmount(tankHurt, true, false) == repairAmount(tankHurt, true, false));

    std::printf("\n%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
