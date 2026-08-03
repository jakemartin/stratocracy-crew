// Gate for the debug-command driver (GATE-DRV-01..09). See spec/driver_spec.md.
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
            // row 5's surfaces refuse on the same terms
            "match", "match 0", "match 0 0", "match 0 -1", "match 2 5",
            "endturn", "flag", "flag 0 99", "flag 1 1", "flag 3 1",
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
            "match 0 3", "standings", "endturn", "endturn", "standings", "result",
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

    // --- GATE-DRV-08 ---------------------------------------------------------
    // Turn ownership is Turn.h's. With no match running the board is the same free
    // sandbox it was before row 5 -- which is why GATE-DRV-01..07 above are
    // unaffected -- and once a match starts, every refusal the driver issues is one
    // canAct/markActed has already decided.
    bool ok08 = true;
    {
        Session t;
        std::string e;
        sessionInit(t, dir, e);
        loadFixture(t, "open", e);
        run(t, "place 0 Infantry 1 1");        // #1
        run(t, "place 1 Tank 3 1");            // #2
        if (t.match.running) ok08 = false;

        // Sandbox: either side moves, and a unit moves twice, because nothing owns
        // the turn yet.
        if (!contains(run(t, "move 2 3 0"), "moved")) ok08 = false;
        if (!contains(run(t, "move 2 3 1"), "moved")) ok08 = false;
        if (!contains(run(t, "move 1 1 0"), "moved")) ok08 = false;

        Session m = t;
        if (!contains(run(m, "match 0 20"), "match started")) ok08 = false;
        if (!m.match.running || m.match.activeSide != 0) ok08 = false;

        // The inactive side is refused, and the refusal agrees with canAct.
        if (canAct(m.match, 2, 1)) ok08 = false;
        std::string before = stateHash(m);
        if (!contains(run(m, "move 2 3 0"), "refused")) ok08 = false;
        if (stateHash(m) != before) ok08 = false;

        // The active side acts once, and only once.
        if (!canAct(m.match, 1, 0)) ok08 = false;
        if (!contains(run(m, "move 1 1 1"), "moved")) ok08 = false;
        if (!hasActed(m.match, 1)) ok08 = false;
        if (canAct(m.match, 1, 0)) ok08 = false;
        before = stateHash(m);
        if (!contains(run(m, "move 1 1 0"), "refused")) ok08 = false;
        if (stateHash(m) != before) ok08 = false;

        // The debug setter stands down while the loop owns the number.
        before = stateHash(m);
        if (!contains(run(m, "turn 5"), "refused")) ok08 = false;
        if (stateHash(m) != before) ok08 = false;

        // Alternation is read off Turn.h, and the economy commands follow the same
        // activeSide rather than a second notion of whose turn it is.
        run(m, "endturn");
        if (m.match.activeSide != 1) ok08 = false;
        if (!canAct(m.match, 2, 1)) ok08 = false;
        if (canAct(m.match, 1, 0)) ok08 = false;
        if (!contains(run(m, "income 0"), "refused")) ok08 = false;
        if (contains(run(m, "income 1"), "refused")) ok08 = false;
    }
    check("GATE-DRV-08 turn-ownership-is-the-turn-modules", ok08);

    // --- GATE-DRV-09 ---------------------------------------------------------
    // What the driver displays and what the match records are the same module call:
    // the standings leader is resolveAtCap on the live board, and the recorded
    // result is resolveAtCap on the board the match ended on.
    bool ok09 = true;
    {
        Session t;
        std::string e;
        sessionInit(t, dir, e);
        loadFixture(t, "contested", e);
        run(t, "place 0 Infantry 0 1");
        run(t, "place 1 Infantry 6 1");
        run(t, "match 0 2");

        const MatchResult lead = resolveAtCap(snapshotOf(t));
        const std::vector<std::string> st = run(t, "standings");
        if (lead.cause == ResultCause::PassivityGuard) {
            if (!contains(st, "no engagements")) ok09 = false;   // §2.11.4's row
        } else if (!contains(st, "leader")) ok09 = false;
        if (!contains(st, "turn " + std::to_string(t.match.turnNumber) + "/" +
                          std::to_string(t.match.turnCap))) ok09 = false;

        for (int i = 0; i < 4 && t.match.running; ++i) run(t, "endturn");
        if (t.match.running) ok09 = false;
        const MatchResult expect = resolveAtCap(snapshotOf(t));
        if (t.match.result.tier != expect.tier) ok09 = false;
        if (t.match.result.cause != expect.cause) ok09 = false;
        if (t.match.result.winner != expect.winner) ok09 = false;
        if (t.match.result.decidedByKey != expect.decidedByKey) ok09 = false;
        if (!contains(run(t, "result"), std::string(tierName(expect.tier)))) ok09 = false;

        // A designated flag ends the match at once, and NOT through the tiebreak.
        Session f;
        sessionInit(f, dir, e);
        loadFixture(f, "open", e);
        run(f, "place 0 Tank 1 1");            // #1
        run(f, "place 1 Infantry 2 1");        // #2
        run(f, "hp 2 1");                      // one hit will finish it
        const UnitDef victim = *defOf(f, 2);   // captured before it leaves the board
        if (!contains(run(f, "flag 1 2"), "debug designation")) ok09 = false;
        run(f, "match 0 20");
        if (!contains(run(f, "attack 1 2"), "match over")) ok09 = false;
        if (f.match.result.cause != ResultCause::FlagDestroyed) ok09 = false;
        if (f.match.result.tier != ResultTier::Decisive) ok09 = false;
        if (f.match.result.winner != 0) ok09 = false;
        if (f.match.result.decidedByKey != 0) ok09 = false;      // no key was read
        // The flag award replaced the ordinary one (Q5), decided by Economy.h.
        if (f.economy.side[0].fameCombat != killAward(victim, true)) ok09 = false;
    }
    check("GATE-DRV-09 standings-and-result-are-the-turn-modules", ok09);

    // --- GATE-DRV-10 ---------------------------------------------------------
    // The `ai` command adds nothing of its own: the turn it plays is exactly the
    // sequence of ordinary command lines it prints, and replaying those lines by
    // hand on an identical session reaches a byte-identical state. If the AI could
    // reach a state a typed command cannot, these two hashes would differ.
    bool ok10 = true;
    {
        const char* setup[] = {"place 0 Infantry 0 1", "place 1 Infantry 6 1",
                               "place 0 Tank 1 2",     "place 1 Recon 5 2",
                               "ai buildlist Infantry Recon", "match 0 20"};
        Session a, b;
        std::string e;
        sessionInit(a, dir, e); loadFixture(a, "contested", e);
        sessionInit(b, dir, e); loadFixture(b, "contested", e);
        for (const char* c : setup) { run(a, c); run(b, c); }
        if (stateHash(a) != stateHash(b)) ok10 = false;

        // Session a: let the AI play. Session b: type what it printed.
        const std::vector<std::string> out = run(a, "ai");
        std::vector<std::string> issued;
        for (const std::string& l : out) {
            const std::size_t p = l.find("  ai> ");
            if (p != std::string::npos) issued.push_back(l.substr(p + 6));
        }
        if (issued.empty()) ok10 = false;                 // a silent turn proves nothing
        for (const std::string& c : issued) {
            const std::vector<std::string> r = run(b, c);
            if (contains(r, "refused")) ok10 = false;     // a player could not type it
        }
        if (stateHash(a) != stateHash(b)) ok10 = false;

        // Without a match there is no turn to play, and the refusal changes nothing.
        Session c;
        sessionInit(c, dir, e); loadFixture(c, "open", e);
        const std::string before = stateHash(c);
        if (!contains(run(c, "ai"), "refused")) ok10 = false;
        if (stateHash(c) != before) ok10 = false;
    }
    check("GATE-DRV-10 ai-turn-equals-typed-commands", ok10);

    std::printf("\n%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
