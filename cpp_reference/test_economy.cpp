// Test Engineer's gate for §4.11 row 4 — Capture & Fame economy (T-FAME-01..09).
//
// Four of these nine encode a RULED open question, and the gate asserts the ruling
// rather than the intuition it overturned: Q8 (no accrual on turn 1; Fame committed
// at queue time), Q4 (capture progress is tile-held, resets, never transfers),
// Q5 (flag award REPLACES the ordinary one), Q6 (no undamaged-strike bonus -- so
// its ABSENCE is asserted). Unit and terrain values come from data/*.csv through
// row 2, never from constants written here.
#include "Economy.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace strat;

static int g_total = 0;
static int g_fail  = 0;

static void check(const char* name, bool cond) {
    ++g_total;
    if (cond) { std::printf("PASS  %s\n", name); }
    else      { ++g_fail; std::printf("FAIL  %s\n", name); }
}

static int defIndexOf(const std::vector<UnitDef>& units, const std::string& id) {
    for (std::size_t i = 0; i < units.size(); ++i) if (units[i].id == id) return static_cast<int>(i);
    return -1;
}
static int terrainIndexOf(const std::vector<TerrainDef>& t, const std::string& id) {
    for (std::size_t i = 0; i < t.size(); ++i) if (t[i].id == id) return static_cast<int>(i);
    return -1;
}

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? std::string(argv[1]) : std::string("../data");

    std::vector<UnitDef> units;
    std::vector<TerrainDef> terrain;
    std::string err;
    if (!loadUnits(dir + "/units.csv", units, err) ||
        !loadTerrain(dir + "/terrain.csv", terrain, err)) {
        std::printf("FAIL  T-FAME-00 load-row-2-tables (%s)\n\n0/1 passed\n", err.c_str());
        return 1;
    }
    const int FACTORY = terrainIndexOf(terrain, "Factory");
    const int TOWN    = terrainIndexOf(terrain, "Town");
    const int INF     = defIndexOf(units, "Infantry");
    const int TANK    = defIndexOf(units, "Tank");
    const int ARTY    = defIndexOf(units, "Artillery");
    const int RECON   = defIndexOf(units, "Recon");
    if (FACTORY < 0 || TOWN < 0 || INF < 0 || TANK < 0 || ARTY < 0 || RECON < 0) {
        std::printf("FAIL  T-FAME-00 table-lookup\n\n0/1 passed\n");
        return 1;
    }
    const MapBounds bounds{7, 5};

    // A board with one factory and one town per side's reach.
    auto freshState = [&]() {
        EconomyState s;
        s.captureTurns = 1;                       // N = 1, shipped scenario
        Objective homeA; homeA.hex = offsetToAxial(1, 2); homeA.owner = 0; homeA.terrainIndex = FACTORY;
        Objective homeB; homeB.hex = offsetToAxial(5, 2); homeB.owner = 1; homeB.terrainIndex = FACTORY;
        Objective neutral; neutral.hex = offsetToAxial(3, 2); neutral.owner = OWNER_NEUTRAL;
        neutral.terrainIndex = FACTORY;
        Objective town; town.hex = offsetToAxial(3, 0); town.owner = OWNER_NEUTRAL;
        town.terrainIndex = TOWN;
        s.objectives.push_back(homeA);
        s.objectives.push_back(homeB);
        s.objectives.push_back(neutral);
        s.objectives.push_back(town);
        initSide(s, 0, 200);
        initSide(s, 1, 200);
        return s;
    };

    // --- T-FAME-01 -------------------------------------------------------------
    // One pool for everything; fameCombat moves ONLY on combat awards.
    bool ok01 = true;
    {
        EconomyState s = freshState();
        const int startTotal  = s.side[0].fameTotal;
        const int startCombat = s.side[0].fameCombat;

        const int income = accrueIncome(s, terrain, 0, 2);
        if (income <= 0) ok01 = false;
        if (s.side[0].fameTotal != startTotal + income) ok01 = false;
        if (s.side[0].fameCombat != startCombat) ok01 = false;   // income never touches it

        const int beforeTotal = s.side[0].fameTotal;
        awardKill(s, 0, units[TANK], false);
        const int award = killAward(units[TANK], false);
        if (s.side[0].fameTotal  != beforeTotal + award) ok01 = false;
        if (s.side[0].fameCombat != startCombat + award) ok01 = false;  // combat moves both

        // Spending mutates the same pool.
        const int spendBefore = s.side[0].fameTotal;
        std::string e;
        if (!queueBuild(s, units, terrain, 0, offsetToAxial(1, 2), INF, e)) ok01 = false;
        if (s.side[0].fameTotal != spendBefore - units[INF].costFame) ok01 = false;
        if (s.side[0].fameCombat != startCombat + award) ok01 = false;  // spending leaves it alone
    }
    check("T-FAME-01 single-pool-and-separate-combat-counter", ok01);

    // --- T-FAME-02 -------------------------------------------------------------
    // Factory +100, Town +25, at the START of the owner's turn, NO accrual turn 1,
    // and each side's CONFIGURED opening value -- never a literal 200 (Q8).
    bool ok02 = true;
    {
        EconomyState s = freshState();
        if (accrueIncome(s, terrain, 0, 1) != 0) ok02 = false;         // turn 1: nothing
        if (s.side[0].fameTotal != 200) ok02 = false;                  // starting Fame alone

        const int t2 = accrueIncome(s, terrain, 0, 2);                 // one factory held
        if (t2 != terrain[FACTORY].incomeFame) ok02 = false;
        if (terrain[FACTORY].incomeFame != 100) ok02 = false;          // §2.7 via the table
        if (terrain[TOWN].incomeFame != 25) ok02 = false;

        // A town as well: income is the sum of held objectives.
        EconomyState s2 = freshState();
        for (Objective& o : s2.objectives)
            if (o.terrainIndex == TOWN) o.owner = 0;
        if (accrueIncome(s2, terrain, 0, 3) !=
            terrain[FACTORY].incomeFame + terrain[TOWN].incomeFame) ok02 = false;

        // The handicap moves the PLAYER's opening only; the gate asserts whatever
        // was configured, so no literal 200 is baked in (Q8).
        EconomyState easy = freshState();
        initSide(easy, 0, 350);                                        // player, Easy
        initSide(easy, 1, 200);                                        // AI, every tier
        if (easy.side[0].fameTotal != 350 || easy.side[1].fameTotal != 200) ok02 = false;
        EconomyState hard = freshState();
        initSide(hard, 0, 100);                                        // player, Hard
        if (hard.side[0].fameTotal != 100) ok02 = false;
        if (accrueIncome(hard, terrain, 0, 1) != 0) ok02 = false;      // still nothing on turn 1
    }
    check("T-FAME-02 income-values-timing-and-configured-start", ok02);

    // --- T-FAME-03 -------------------------------------------------------------
    // Exact §2.4 costs; unaffordable is refused; fameTotal never goes negative.
    bool ok03 = true;
    {
        const int expect[4] = {100, 300, 200, 150};                    // Inf, Tank, Arty, Recon
        const int idx[4]    = {INF, TANK, ARTY, RECON};
        for (int i = 0; i < 4; ++i) if (units[idx[i]].costFame != expect[i]) ok03 = false;

        EconomyState s = freshState();
        std::string e;
        if (!queueBuild(s, units, terrain, 0, offsetToAxial(1, 2), INF, e)) ok03 = false;
        if (s.side[0].fameTotal != 200 - 100) ok03 = false;

        // Unaffordable: a Tank on 100 left. Refused, and nothing moves.
        EconomyState poor = freshState();
        initSide(poor, 0, 100);
        const int before = poor.side[0].fameTotal;
        if (queueBuild(poor, units, terrain, 0, offsetToAxial(1, 2), TANK, e)) ok03 = false;
        if (poor.side[0].fameTotal != before) ok03 = false;
        if (!poor.pending.empty()) ok03 = false;
        if (poor.side[0].fameTotal < 0) ok03 = false;
    }
    check("T-FAME-03 exact-costs-refusal-never-negative", ok03);

    // --- T-FAME-04 -------------------------------------------------------------
    // Factory hex if free, else adjacent free, else WAIT and keep the slot. Fame is
    // committed at queue time and is not refundable (Q8).
    bool ok04 = true;
    {
        const Hex factory = offsetToAxial(1, 2);

        // (a) factory free -> spawns there
        EconomyState s = freshState();
        std::string e;
        queueBuild(s, units, terrain, 0, factory, INF, e);
        std::vector<Hex> occupied;
        std::vector<SpawnResult> r = resolveBuilds(s, bounds, occupied);
        if (r.size() != 1 || !r[0].spawned || !hexEqual(r[0].hex, factory)) ok04 = false;
        if (!s.pending.empty()) ok04 = false;

        // (b) factory occupied -> an adjacent free hex
        EconomyState s2 = freshState();
        queueBuild(s2, units, terrain, 0, factory, INF, e);
        std::vector<Hex> occ2;
        occ2.push_back(factory);
        std::vector<SpawnResult> r2 = resolveBuilds(s2, bounds, occ2);
        if (r2.size() != 1 || !r2[0].spawned) ok04 = false;
        if (hexEqual(r2[0].hex, factory)) ok04 = false;
        if (hexDistance(r2[0].hex, factory) != 1) ok04 = false;

        // (c) boxed in -> waits, keeps the slot, and the Fame is NOT refunded
        EconomyState s3 = freshState();
        const int afterQueue = [&]{
            queueBuild(s3, units, terrain, 0, factory, INF, e);
            return s3.side[0].fameTotal;
        }();
        std::vector<Hex> boxed;
        boxed.push_back(factory);
        Hex adj[HEX_DIRECTIONS];
        const int n = neighbors(factory, bounds, adj);
        for (int i = 0; i < n; ++i) boxed.push_back(adj[i]);
        std::vector<SpawnResult> r3 = resolveBuilds(s3, bounds, boxed);
        if (r3.size() != 1 || r3[0].spawned) ok04 = false;
        if (s3.pending.size() != 1) ok04 = false;                  // slot still held
        if (s3.side[0].fameTotal != afterQueue) ok04 = false;      // not refunded (Q8)
        // and the held slot refuses a second queue at that factory
        if (queueBuild(s3, units, terrain, 0, factory, INF, e)) ok04 = false;
    }
    check("T-FAME-04 spawn-fallback-wait-and-committed-fame", ok04);

    // --- T-FAME-05 -------------------------------------------------------------
    // Infantry only; N turns; tile-held; resets on leave or death; never transfers.
    bool ok05 = true;
    {
        const Hex neutral = offsetToAxial(3, 2);

        // A Tank cannot capture at all.
        EconomyState s = freshState();
        std::vector<CaptureOccupant> occ;
        CaptureOccupant tank; tank.hex = neutral; tank.unitId = 9; tank.side = 0;
        tank.canCapture = units[TANK].canCapture;
        occ.push_back(tank);
        if (!captureTick(s, occ, 0).empty()) ok05 = false;
        if (findObjective(s, neutral)->owner != OWNER_NEUTRAL) ok05 = false;

        // Infantry captures at N = 1.
        EconomyState s2 = freshState();
        std::vector<CaptureOccupant> occ2;
        CaptureOccupant inf; inf.hex = neutral; inf.unitId = 1; inf.side = 0;
        inf.canCapture = units[INF].canCapture;
        occ2.push_back(inf);
        const std::vector<Hex> flipped = captureTick(s2, occ2, 0);
        if (flipped.size() != 1 || !hexEqual(flipped[0], neutral)) ok05 = false;
        if (findObjective(s2, neutral)->owner != 0) ok05 = false;

        // N = 2: leaving resets progress to zero, and a NEW unit does not inherit it.
        EconomyState s3 = freshState();
        s3.captureTurns = 2;
        if (!captureTick(s3, occ2, 0).empty()) ok05 = false;          // 1 of 2
        if (s3.captures.size() != 1 || s3.captures[0].turnsHeld != 1) ok05 = false;
        const std::vector<CaptureOccupant> empty;
        captureTick(s3, empty, 0);                                     // it left (or died)
        if (!s3.captures.empty()) ok05 = false;                        // reset to zero
        if (findObjective(s3, neutral)->owner != OWNER_NEUTRAL) ok05 = false;

        // Progress never transfers: unit 1 holds one turn, unit 2 replaces it.
        EconomyState s4 = freshState();
        s4.captureTurns = 2;
        captureTick(s4, occ2, 0);                                      // unit 1: 1 of 2
        std::vector<CaptureOccupant> occ3;
        CaptureOccupant other; other.hex = neutral; other.unitId = 2; other.side = 0;
        other.canCapture = units[INF].canCapture;
        occ3.push_back(other);
        const std::vector<Hex> f2 = captureTick(s4, occ3, 0);
        if (!f2.empty()) ok05 = false;                                 // must NOT complete
        if (s4.captures.size() != 1 || s4.captures[0].turnsHeld != 1) ok05 = false;
        if (s4.captures[0].unitId != 2) ok05 = false;
    }
    check("T-FAME-05 capture-infantry-tile-held-resets-never-transfers", ok05);

    // --- T-FAME-06 -------------------------------------------------------------
    // Income follows ownership.
    bool ok06 = true;
    {
        EconomyState s = freshState();
        const Hex neutral = offsetToAxial(3, 2);
        const int before0 = accrueIncome(s, terrain, 0, 2);
        std::vector<CaptureOccupant> occ;
        CaptureOccupant inf; inf.hex = neutral; inf.unitId = 1; inf.side = 0;
        inf.canCapture = units[INF].canCapture;
        occ.push_back(inf);
        captureTick(s, occ, 0);
        const int after0 = accrueIncome(s, terrain, 0, 3);
        if (after0 != before0 + terrain[FACTORY].incomeFame) ok06 = false;

        // And it leaves the previous owner: side 1 takes side 0's home factory.
        EconomyState s2 = freshState();
        const int b1 = accrueIncome(s2, terrain, 0, 2);
        std::vector<CaptureOccupant> occ2;
        CaptureOccupant enemy; enemy.hex = offsetToAxial(1, 2); enemy.unitId = 7; enemy.side = 1;
        enemy.canCapture = units[INF].canCapture;
        occ2.push_back(enemy);
        captureTick(s2, occ2, 1);
        const int a1 = accrueIncome(s2, terrain, 0, 3);
        if (a1 != b1 - terrain[FACTORY].incomeFame) ok06 = false;
    }
    check("T-FAME-06 income-flips-with-ownership", ok06);

    // --- T-FAME-07 -------------------------------------------------------------
    // Half cost; flag pays a flat 500 REPLACING the ordinary award (Q5); no
    // undamaged-strike bonus exists at all (Q6) -- asserted by its absence.
    bool ok07 = true;
    {
        if (killAward(units[INF],   false) != 50)  ok07 = false;
        if (killAward(units[RECON], false) != 75)  ok07 = false;
        if (killAward(units[ARTY],  false) != 100) ok07 = false;
        if (killAward(units[TANK],  false) != 150) ok07 = false;

        // The flag REPLACES: a flag Tank pays 500, not 650.
        if (killAward(units[TANK], true) != 500) ok07 = false;
        if (killAward(units[TANK], true) == 500 + 150) ok07 = false;

        // Every award is exactly half cost, derived not hardcoded.
        for (const UnitDef& u : units)
            if (killAward(u, false) != u.costFame / 2) ok07 = false;

        // Q6: no bonus for a clean strike. The award is a function of the VICTIM
        // alone -- there is no attacker-condition parameter to pass, so an
        // undamaged-strike bonus cannot be expressed, let alone paid.
        EconomyState s = freshState();
        awardKill(s, 0, units[ARTY], false);
        if (s.side[0].fameCombat != 100) ok07 = false;
        EconomyState s2 = freshState();
        awardKill(s2, 0, units[ARTY], false);
        if (s2.side[0].fameCombat != s.side[0].fameCombat) ok07 = false;
    }
    check("T-FAME-07 kill-awards-flag-replaces-no-bonus", ok07);

    // --- T-FAME-08 -------------------------------------------------------------
    // No cap: fameTotal keeps climbing.
    bool ok08 = true;
    {
        EconomyState s = freshState();
        for (Objective& o : s.objectives) o.owner = 0;
        int last = s.side[0].fameTotal;
        for (int turn = 2; turn < 200; ++turn) {
            accrueIncome(s, terrain, 0, turn);
            if (s.side[0].fameTotal <= last) ok08 = false;
            last = s.side[0].fameTotal;
        }
        if (s.side[0].fameTotal < 20000) ok08 = false;      // far past any plausible cap
        for (int i = 0; i < 100; ++i) awardKill(s, 0, units[TANK], false);
        if (s.side[0].fameCombat != 100 * 150) ok08 = false;
    }
    check("T-FAME-08 no-fame-cap", ok08);

    // --- T-FAME-09 -------------------------------------------------------------
    // Same state + command -> identical deltas and identical state.
    bool ok09 = true;
    {
        auto runScript = [&](EconomyState& s) {
            std::string e;
            accrueIncome(s, terrain, 0, 2);
            queueBuild(s, units, terrain, 0, offsetToAxial(1, 2), INF, e);
            std::vector<Hex> occ;
            occ.push_back(offsetToAxial(1, 2));
            resolveBuilds(s, bounds, occ);
            std::vector<CaptureOccupant> co;
            CaptureOccupant inf; inf.hex = offsetToAxial(3, 2); inf.unitId = 1; inf.side = 0;
            inf.canCapture = units[INF].canCapture;
            co.push_back(inf);
            captureTick(s, co, 0);
            awardKill(s, 0, units[RECON], false);
        };
        EconomyState a = freshState(), b = freshState();
        runScript(a);
        runScript(b);
        if (a.side[0].fameTotal != b.side[0].fameTotal) ok09 = false;
        if (a.side[0].fameCombat != b.side[0].fameCombat) ok09 = false;
        if (a.objectives.size() != b.objectives.size()) ok09 = false;
        else for (std::size_t i = 0; i < a.objectives.size(); ++i)
            if (a.objectives[i].owner != b.objectives[i].owner ||
                !hexEqual(a.objectives[i].hex, b.objectives[i].hex)) ok09 = false;
        if (a.pending.size() != b.pending.size()) ok09 = false;
        if (a.captures.size() != b.captures.size()) ok09 = false;

        // The spawn fallback is reproducible, not merely non-random.
        EconomyState c = freshState(), d = freshState();
        std::string e;
        queueBuild(c, units, terrain, 0, offsetToAxial(1, 2), INF, e);
        queueBuild(d, units, terrain, 0, offsetToAxial(1, 2), INF, e);
        std::vector<Hex> occ;
        occ.push_back(offsetToAxial(1, 2));
        const std::vector<SpawnResult> rc = resolveBuilds(c, bounds, occ);
        const std::vector<SpawnResult> rd = resolveBuilds(d, bounds, occ);
        if (rc.size() != 1 || rd.size() != 1) ok09 = false;
        else if (!hexEqual(rc[0].hex, rd[0].hex)) ok09 = false;
    }
    check("T-FAME-09 determinism", ok09);

    std::printf("\n%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
