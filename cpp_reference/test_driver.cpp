// Gate for the debug-command driver (GATE-DRV-01..07). See spec/driver_spec.md.
//
// These are NOT GDD acceptance IDs and flip no §3 ledger row -- the driver builds no
// rules system. They assert one property: that the driver decides nothing itself. So
// every check compares the driver's OUTPUT against a direct call into the module that
// owns the decision, never against a hardcoded expectation.
#include "Driver.h"

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

static std::vector<std::string> run(Session& s, const std::string& cmd) {
    std::vector<std::string> out;
    execute(s, cmd, out);
    return out;
}

// Pull every integer out of a line, so a check reads the numbers the user is shown.
static std::vector<int> ints(const std::string& line) {
    std::vector<int> v;
    std::size_t i = 0;
    while (i < line.size()) {
        if (line[i] == '-' || (line[i] >= '0' && line[i] <= '9')) {
            const bool neg = line[i] == '-';
            std::size_t j = neg ? i + 1 : i;
            int n = 0;
            bool any = false;
            while (j < line.size() && line[j] >= '0' && line[j] <= '9') {
                n = n * 10 + (line[j] - '0'); ++j; any = true;
            }
            if (any) { v.push_back(neg ? -n : n); i = j; continue; }
        }
        ++i;
    }
    return v;
}

static bool contains(const std::vector<std::string>& out, const std::string& needle) {
    for (const std::string& l : out)
        if (l.find(needle) != std::string::npos) return true;
    return false;
}

static const UnitDef* defOf(const Session& s, int id) {
    const DriverUnit* u = findUnitById(s, id);
    return u ? &s.unitDefs[u->defIndex] : nullptr;
}

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? std::string(argv[1]) : std::string("../data");

    Session s;
    std::string err;
    if (!sessionInit(s, dir, err)) {
        std::printf("FAIL  GATE-DRV-00 session-init (%s)\n\n0/1 passed\n", err.c_str());
        return 1;
    }
    if (!loadFixture(s, "river", err)) {
        std::printf("FAIL  GATE-DRV-00 fixture (%s)\n\n0/1 passed\n", err.c_str());
        return 1;
    }

    run(s, "place 0 Infantry 0 2");     // #1 left bank
    run(s, "place 1 Tank 2 2");         // #2 two hexes east, adjacent-ish
    run(s, "place 0 Artillery 0 0");    // #3 ranged
    if (s.units.size() != 3) {
        std::printf("FAIL  GATE-DRV-00 placement\n\n0/1 passed\n");
        return 1;
    }

    // --- GATE-DRV-01 ---------------------------------------------------------
    // `reach` equals Move.h::reachable, hex for hex and cost for cost, in order.
    bool ok01 = true;
    {
        const DriverUnit* u = findUnitById(s, 1);
        Board b;
        b.bounds = s.bounds; b.terrain = s.terrain;
        b.occupant.assign(s.terrain.size(), OCCUPANT_NONE);
        for (const DriverUnit& o : s.units) b.occupant[b.index(o.hex)] = o.id;
        const std::vector<ReachEntry> direct =
            reachable(b, s.terrainDefs, u->hex, defOf(s, 1)->move);

        const std::vector<std::string> out = run(s, "reach 1");
        std::vector<std::pair<std::pair<int,int>,int> > shown;
        for (const std::string& l : out) {
            if (l.size() < 3 || l[0] != ' ' || l[1] != ' ' || l[2] != '(') continue;
            const std::vector<int> n = ints(l);
            if (n.size() >= 3) shown.push_back(std::make_pair(std::make_pair(n[0], n[1]), n[2]));
        }
        if (shown.size() != direct.size()) ok01 = false;
        else for (std::size_t i = 0; i < direct.size(); ++i) {
            int col = 0, row = 0;
            axialToOffset(direct[i].hex, col, row);
            if (shown[i].first.first != col || shown[i].first.second != row ||
                shown[i].second != direct[i].cost) ok01 = false;
        }
        if (direct.empty()) ok01 = false;             // a vacuous pass is not a pass
    }
    check("GATE-DRV-01 reach-equals-module-reachable", ok01);

    // --- GATE-DRV-02 ---------------------------------------------------------
    // `move` lands exactly on findPath's endpoint; a refused move changes nothing.
    bool ok02 = true;
    {
        Session t = s;
        Board b;
        b.bounds = t.bounds; b.terrain = t.terrain;
        b.occupant.assign(t.terrain.size(), OCCUPANT_NONE);
        for (const DriverUnit& o : t.units) b.occupant[b.index(o.hex)] = o.id;
        const Hex goal = offsetToAxial(1, 2);
        std::vector<Hex> route;
        int cost = 0;
        const bool reachableGoal =
            findPath(b, t.terrainDefs, findUnitById(t, 1)->hex, goal, defOf(t, 1)->move, route, cost);
        if (!reachableGoal) ok02 = false;

        const std::vector<std::string> out = run(t, "move 1 1 2");
        if (!contains(out, "moved")) ok02 = false;
        if (!hexEqual(findUnitById(t, 1)->hex, goal)) ok02 = false;
        const std::vector<int> n = ints(out.empty() ? std::string() : out[0]);
        if (n.size() < 2 || n[1] != cost) ok02 = false;      // reported cost is findPath's

        // Refused move: unreachable target leaves the state untouched.
        const std::string before = stateHash(t);
        const std::vector<std::string> bad = run(t, "move 1 6 2");
        if (!contains(bad, "refused")) ok02 = false;
        if (stateHash(t) != before) ok02 = false;
    }
    check("GATE-DRV-02 move-follows-module-path", ok02);

    // --- GATE-DRV-03 ---------------------------------------------------------
    // Forecast equals resolution: what `forecast` predicts is what `attack` applies,
    // and both equal a direct Combat.h computation (§2.6, no hidden roll).
    bool ok03 = true;
    {
        Session t = s;
        run(t, "move 1 1 2");                        // Infantry #1 adjacent to Tank #2
        const DriverUnit* a = findUnitById(t, 1);
        const DriverUnit* d = findUnitById(t, 2);
        const int dist = hexDistance(a->hex, d->hex);
        if (dist != 1) ok03 = false;

        Unit au; const UnitDef& ad = *defOf(t, 1);
        au.atk = ad.atk; au.def = ad.def; au.hp = a->hp; au.hpMax = ad.hpMax;
        au.rangeMin = ad.rangeMin; au.rangeMax = ad.rangeMax; au.type = ad.type;
        Unit du; const UnitDef& dd = *defOf(t, 2);
        du.atk = dd.atk; du.def = dd.def; du.hp = d->hp; du.hpMax = dd.hpMax;
        du.rangeMin = dd.rangeMin; du.rangeMax = dd.rangeMax; du.type = dd.type;

        int defTerrain = 0;
        {
            Board b; b.bounds = t.bounds; b.terrain = t.terrain;
            b.occupant.assign(t.terrain.size(), OCCUPANT_NONE);
            defTerrain = t.terrainDefs[b.terrainAt(d->hex)].defensePct;
        }
        const int expected = resolveDamage(au, du, defTerrain);

        const std::vector<std::string> fc = run(t, "forecast 1 2");
        bool forecastMatches = false;
        for (const std::string& l : fc) {
            const std::vector<int> n = ints(l);
            for (int v : n) if (v == expected) forecastMatches = true;
        }
        if (!forecastMatches) ok03 = false;

        const int hpBefore = findUnitById(t, 2)->hp;
        run(t, "attack 1 2");
        const DriverUnit* after = findUnitById(t, 2);
        if (after == nullptr) {
            if (hpBefore > expected) ok03 = false;    // died only if the damage killed it
        } else if (hpBefore - after->hp != expected) {
            ok03 = false;                             // applied exactly the forecast damage
        }
    }
    check("GATE-DRV-03 forecast-equals-resolution", ok03);

    // --- GATE-DRV-04 ---------------------------------------------------------
    // Range is Combat.h's, not the driver's: an Artillery (2-3) refuses at 1.
    bool ok04 = true;
    {
        Session t = s;
        if (!loadFixture(t, "open", err)) ok04 = false;
        run(t, "place 0 Artillery 1 1");
        run(t, "place 1 Tank 2 1");
        const DriverUnit* a = findUnitById(t, 1);
        const DriverUnit* d = findUnitById(t, 2);
        if (a == nullptr || d == nullptr || hexDistance(a->hex, d->hex) != 1) ok04 = false;
        const std::string before = stateHash(t);
        const std::vector<std::string> out = run(t, "attack 1 2");
        if (!contains(out, "refused")) ok04 = false;       // 1 is outside 2-3
        if (stateHash(t) != before) ok04 = false;

        // At distance 2 it is legal, and a range-1 defender takes no counter --
        // T-COMBAT-07's rule, reached through the driver.
        run(t, "remove 2");
        run(t, "place 1 Tank 3 1");
        const DriverUnit* d2 = findUnitById(t, 3);
        if (d2 == nullptr || hexDistance(findUnitById(t, 1)->hex, d2->hex) != 2) ok04 = false;
        const std::vector<std::string> fc = run(t, "forecast 1 3");
        if (contains(fc, "refused")) ok04 = false;
        if (!contains(fc, "counter: none")) ok04 = false;  // Tank cannot reach back at 2
    }
    check("GATE-DRV-04 range-band-is-combats", ok04);

    // --- GATE-DRV-05 ---------------------------------------------------------
    // No second source of truth: every distance the driver reports equals
    // hexDistance, and terrain defense equals the loaded DefensePct.
    bool ok05 = true;
    {
        Session t = s;
        for (int r1 = 0; r1 < t.bounds.rows; ++r1)
            for (int c1 = 0; c1 < t.bounds.cols; ++c1)
                for (int r2 = 0; r2 < t.bounds.rows; ++r2)
                    for (int c2 = 0; c2 < t.bounds.cols; ++c2) {
                        const std::vector<std::string> out =
                            run(t, "dist " + std::to_string(c1) + " " + std::to_string(r1) + " " +
                                   std::to_string(c2) + " " + std::to_string(r2));
                        if (out.empty()) { ok05 = false; continue; }
                        const std::vector<int> n = ints(out[0]);
                        const int expect = hexDistance(offsetToAxial(c1, r1), offsetToAxial(c2, r2));
                        if (n.empty() || n.back() != expect) ok05 = false;
                    }
        // Bridge's negative defense reaches the driver intact (§2.3, -10).
        const TerrainDef* bridge = findTerrain(t.terrainDefs, "Bridge");
        if (bridge == nullptr || bridge->defensePct >= 0) ok05 = false;
    }
    check("GATE-DRV-05 no-second-source-of-truth", ok05);

    // --- GATE-DRV-06 ---------------------------------------------------------
    // Refusal safety: malformed and illegal commands change nothing and say why.
    bool ok06 = true;
    {
        Session t = s;
        const char* bad[] = {
            "", "nonsense", "move", "move 99 1 1", "move 1 99 99", "attack 1 1",
            "attack 1 3", "place 2 Infantry 1 1", "place 0 Sniper 1 1",
            "place 0 Infantry 3 0", "hp 1 999", "repair 1 7", "dist 0 0 99 99",
            "reach 42", "remove 42", "fixture nope",
        };
        for (const char* c : bad) {
            const std::string before = stateHash(t);
            const std::vector<std::string> out = run(t, c);
            if (stateHash(t) != before) ok06 = false;                 // nothing moved
            if (std::string(c).empty()) continue;                     // blank line: silence is fine
            if (!contains(out, "refused")) ok06 = false;              // and it said why
        }
        // "place 0 Infantry 3 0" is the Water hex on the river fixture -- refused by
        // PassLand from the table, not by a rule written here.
        const TerrainDef* water = findTerrain(t.terrainDefs, "Water");
        if (water == nullptr || water->passLand) ok06 = false;
    }
    check("GATE-DRV-06 refusal-changes-nothing", ok06);

    // --- GATE-DRV-07 ---------------------------------------------------------
    // Determinism: the same sequence from the same fixture, twice, byte for byte.
    bool ok07 = true;
    {
        const char* script[] = {
            "fixture river", "place 0 Infantry 0 2", "place 1 Tank 2 2",
            "reach 1", "path 1 1 2", "move 1 1 2", "forecast 1 2", "attack 1 2",
            "units", "map", "repair 2 1",
        };
        std::vector<std::string> a, b;
        for (int pass = 0; pass < 2; ++pass) {
            Session t;
            std::string e;
            sessionInit(t, dir, e);
            std::vector<std::string>& sink = (pass == 0) ? a : b;
            for (const char* c : script) {
                const std::vector<std::string> out = run(t, c);
                for (const std::string& l : out) sink.push_back(l);
            }
        }
        if (a.empty() || a.size() != b.size()) ok07 = false;
        else for (std::size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) ok07 = false;
    }
    check("GATE-DRV-07 determinism", ok07);

    std::printf("\n%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
