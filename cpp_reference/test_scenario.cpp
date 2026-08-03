// Test Engineer's gate for §4.11 row 7 — Scenario file & validator.
//
// WHAT THIS SUITE DOES NOT COVER, stated up front because a suite that quietly omits
// a fixture reads as a complete pass and this one is DELIBERATELY INCOMPLETE. The
// Director's scope ruling (spec/scenario_spec.md) authors no scenario file for
// Longwater March (§2.13.5) or The Causeway (§2.13.6), not even as a validator
// fixture, so four things below never run. They are printed by name at the end of the
// run as NOT RUN lines, not folded into the tally.
//
// Row 7 depends on rows 1, 2 and 3, and this gate depends on them the same way: it
// links Hex.cpp, Data.cpp and Move.cpp, takes its move costs from data/terrain.csv
// and its Move allowances from data/units.csv, and reads the shipped scenario from
// data/ferrum_crossing.json. argv[1] overrides the data directory.
//
// Every measured integer asserted here is asserted TWICE: once as the number
// §2.13.1/§2.13.2/§4.7 print, and once against an INDEPENDENT relaxation pass written
// below over the transcribed terrain. Comparing the module's Dijkstra to itself would
// assert nothing, and "measured, not inferred" (T-SCN-08) is exactly the claim a
// self-comparison cannot test.
#include "Data.h"
#include "Hex.h"
#include "Move.h"
#include "Scenario.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <sstream>
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

static void why(const char* what) { std::printf("      (%s)\n", what); }
static void whyStr(const char* what, const std::string& v) {
    std::printf("      (%s: %s)\n", what, v.c_str());
}
static void whyInt(const char* what, int a, int b) {
    std::printf("      (%s: got %d, wanted %d)\n", what, a, b);
}

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// --- fixture construction --------------------------------------------------------
// . Plains   F Woods   M Mountains   ~ Water   B Bridge   T Town   X Factory
static const char* terrainIdFor(char c) {
    switch (c) {
        case '.': return "Plains";
        case 'F': return "Woods";
        case 'M': return "Mountains";
        case '~': return "Water";
        case 'B': return "Bridge";
        case 'T': return "Town";
        case 'X': return "Factory";
        default:  return nullptr;
    }
}

static Scenario baseFrom(const std::vector<std::string>& rows) {
    Scenario s;
    s.formatVersion = SCENARIO_FORMAT_VERSION;
    s.scenarioId    = "fixture";
    s.bounds.rows   = static_cast<int>(rows.size());
    s.bounds.cols   = rows.empty() ? 0 : static_cast<int>(rows[0].size());
    for (const std::string& r : rows)
        for (char c : r) {
            const char* id = terrainIdFor(c);
            s.terrainId.push_back(id == nullptr ? std::string("?") : std::string(id));
        }
    s.startingFame[0] = 200;
    s.startingFame[1] = 200;
    s.turnCap  = 20;
    s.symmetry = Symmetry::None;
    return s;
}

static void own(Scenario& s, int col, int row, int owner) {
    ScenarioOwner o;
    o.hex   = offsetToAxial(col, row);
    o.owner = owner;
    s.ownership.push_back(o);
}

static void place(Scenario& s, int side, const char* unitId, int col, int row, bool flag) {
    ScenarioPlacement p;
    p.side   = side;
    p.unitId = unitId;
    p.hex    = offsetToAxial(col, row);
    p.isFlag = flag;
    s.placements.push_back(p);
}

static void guide(Scenario& s, int side, int ic, int ir, int oc, int orow) {
    ScenarioGuided g;
    g.side      = side;
    g.infantry  = offsetToAxial(ic, ir);
    g.objective = offsetToAxial(oc, orow);
    s.guided.push_back(g);
}

// Moves the placement standing at one offset hex to another. Used to rebuild the
// shipped map's own PRE-FIX deployment (T-SCN-11 fixture (b)) -- one placement
// changed, nothing else.
static bool movePlacement(Scenario& s, int fromC, int fromR, int toC, int toR) {
    const Hex from = offsetToAxial(fromC, fromR);
    for (ScenarioPlacement& p : s.placements)
        if (hexEqual(p.hex, from)) { p.hex = offsetToAxial(toC, toR); return true; }
    return false;
}

static const ScenarioLane* laneOf(const ScenarioLoadResult& r, int side) {
    for (const ScenarioLane& l : r.lanes) if (l.side == side) return &l;
    return nullptr;
}

// --- the independent oracle ------------------------------------------------------
// Repeated relaxation to a goal, written from the odd-r OFFSET neighbour rule rather
// than from Hex.h's axial arithmetic, and re-deriving the two pricing rules from the
// same table: every hex entered is charged its MoveCost (including the goal), and the
// §4.8 sentinel MoveCost 0 is impassable. `allowBridge == false` additionally refuses
// to enter a Bridge (T-SCN-06's clause).
static const int kInf = 1000000000;

static void offsetNb(int col, int row, int out[6][2]) {
    if (row & 1) {
        const int o[6][2] = {{col + 1, row}, {col + 1, row - 1}, {col, row - 1},
                             {col - 1, row}, {col, row + 1}, {col + 1, row + 1}};
        for (int i = 0; i < 6; ++i) { out[i][0] = o[i][0]; out[i][1] = o[i][1]; }
    } else {
        const int e[6][2] = {{col + 1, row}, {col, row - 1}, {col - 1, row - 1},
                             {col - 1, row}, {col - 1, row + 1}, {col, row + 1}};
        for (int i = 0; i < 6; ++i) { out[i][0] = e[i][0]; out[i][1] = e[i][1]; }
    }
}

static int oracleCost(const Scenario& s, const std::vector<TerrainDef>& terrain,
                      int fromC, int fromR, int toC, int toR, bool allowBridge) {
    const int W = s.bounds.cols, H = s.bounds.rows;
    if (fromC < 0 || fromC >= W || fromR < 0 || fromR >= H) return -1;
    if (toC   < 0 || toC   >= W || toR   < 0 || toR   >= H) return -1;
    std::vector<int> cost(static_cast<std::size_t>(W) * H, kInf);
    cost[static_cast<std::size_t>(toR) * W + toC] = 0;
    bool changed = true;
    while (changed) {
        changed = false;
        for (int r = 0; r < H; ++r) {
            for (int c = 0; c < W; ++c) {
                int nb[6][2];
                offsetNb(c, r, nb);
                for (int k = 0; k < 6; ++k) {
                    const int nc = nb[k][0], nr = nb[k][1];
                    if (nc < 0 || nr < 0 || nc >= W || nr >= H) continue;
                    const std::size_t ni = static_cast<std::size_t>(nr) * W + nc;
                    if (cost[ni] == kInf) continue;
                    const TerrainDef* d = findTerrain(terrain, s.terrainId[ni]);
                    if (d == nullptr) continue;
                    if (!allowBridge && d->id == "Bridge") continue;
                    if (d->moveCost <= 0) continue;
                    const int cand = d->moveCost + cost[ni];
                    const std::size_t here = static_cast<std::size_t>(r) * W + c;
                    if (cand < cost[here]) { cost[here] = cand; changed = true; }
                }
            }
        }
    }
    const int v = cost[static_cast<std::size_t>(fromR) * W + fromC];
    return (v == kInf) ? -1 : v;
}

// Prices one route through the MODULE, in offset coordinates, for readability below.
static int modCost(const Scenario& s, const std::vector<TerrainDef>& terrain,
                   int fromC, int fromR, int toC, int toR, bool allowBridge) {
    int c = 0;
    if (!laneCost(s, terrain, offsetToAxial(fromC, fromR), offsetToAxial(toC, toR),
                  allowBridge, c)) return -1;
    return c;
}

// Asserts the module and the oracle agree, and that both equal the number the GDD
// prints. Returns false (and says which of the three disagreed) otherwise.
static bool priced(const Scenario& s, const std::vector<TerrainDef>& terrain,
                   int fromC, int fromR, int toC, int toR, bool allowBridge,
                   int expected, const char* label) {
    const int m = modCost(s, terrain, fromC, fromR, toC, toR, allowBridge);
    const int o = oracleCost(s, terrain, fromC, fromR, toC, toR, allowBridge);
    if (m == expected && o == expected) return true;
    std::printf("      (%s: module %d, oracle %d, document %d)\n", label, m, o, expected);
    return false;
}

// --- fixtures ---------------------------------------------------------------------
// (c) for T-SCN-08: "a scenario whose lanes BOTH COST 7". The stub names no map for
// this one, which is exactly the case in which a synthetic file is legitimate -- it
// is a ceiling refusal, not a map. 20 x 3, open Plains, four factories.
static Scenario sevenLaneScenario() {
    std::vector<std::string> rows;
    rows.push_back("....................");
    rows.push_back("X.......X..X.......X");
    rows.push_back("....................");
    Scenario s = baseFrom(rows);
    s.scenarioId = "seven_mp_lanes";
    own(s, 0, 1, 0); own(s, 19, 1, 1); own(s, 8, 1, OWNER_NEUTRAL); own(s, 11, 1, OWNER_NEUTRAL);
    place(s, 0, "Tank", 1, 0, true);      place(s, 0, "Infantry", 1, 1, false);
    place(s, 1, "Tank", 18, 0, true);     place(s, 1, "Infantry", 18, 1, false);
    guide(s, 0, 1, 1, 8, 1);
    guide(s, 1, 18, 1, 11, 1);
    return s;
}

// A map a Water column cuts in two, for T-SCN-04. `bridged` re-opens the crossing,
// which is the same map passing -- a control, so the refusal below is attributable to
// the wall and not to anything else about the fixture.
static Scenario bisectedScenario(bool bridged) {
    std::vector<std::string> rows;
    rows.push_back(".....~.....");
    rows.push_back(bridged ? "X..X.B.X..X" : "X..X.~.X..X");
    rows.push_back(".....~.....");
    Scenario s = baseFrom(rows);
    s.scenarioId = bridged ? "bisected_bridged" : "bisected";
    own(s, 0, 1, 0); own(s, 10, 1, 1); own(s, 3, 1, OWNER_NEUTRAL); own(s, 7, 1, OWNER_NEUTRAL);
    place(s, 0, "Tank", 0, 0, true);      place(s, 0, "Infantry", 1, 1, false);
    place(s, 1, "Tank", 10, 0, true);     place(s, 1, "Infantry", 9, 1, false);
    guide(s, 0, 1, 1, 3, 1);
    guide(s, 1, 9, 1, 7, 1);
    return s;
}

// --- the minimal parse fixture ----------------------------------------------------
// Parses; asserts nothing. Every GATE-SCN-PARSE case below is this text with one
// thing wrong, so a refusal is attributable to that one thing.
static const char* kTinyJson = R"J({
  "formatVersion": 1,
  "scenarioId": "tiny",
  "map": {"width": 2, "height": 1, "terrain": [["Plains", "Plains"]]},
  "ownership": [],
  "placements": [{"side": 0, "unitId": "Tank", "hex": [0, 0], "isFlag": true}],
  "startingFame": {"side0": 200, "side1": 200},
  "turnCap": 20,
  "guidedOpening": [],
  "symmetry": "none"
})J";

static std::string tinyWith(const char* from, const char* to) {
    std::string s(kTinyJson);
    const std::size_t at = s.find(from);
    if (at == std::string::npos) return "<<fixture edit did not apply>>";
    return s.substr(0, at) + to + s.substr(at + std::string(from).size());
}

static bool refusesParse(const std::string& text, const char* label) {
    Scenario junk;
    const ScenarioLoadResult r = parseScenario(text, "probe", junk);
    if (!r.ok && r.failedId == "GATE-SCN-PARSE") return true;
    std::printf("      (%s: parser did not refuse -- ok=%d id='%s')\n",
                label, r.ok ? 1 : 0, r.failedId.c_str());
    return false;
}

// =================================================================================
int main(int argc, char** argv) {
    const std::string dataDir = (argc > 1) ? std::string(argv[1]) : std::string("../data");

    std::vector<UnitDef>    units;
    std::vector<TerrainDef> terrain;
    std::string err;
    if (!loadUnits(dataDir + "/units.csv", units, err) ||
        !loadTerrain(dataDir + "/terrain.csv", terrain, err)) {
        std::printf("FAIL  GATE-SCN-00 data-tables (%s)\n\n0/1 passed\n", err.c_str());
        return 1;
    }

    const std::string shippedPath = dataDir + "/ferrum_crossing.json";
    Scenario shipped;
    const ScenarioLoadResult shippedResult = loadScenario(shippedPath, units, terrain, shipped);
    if (!shippedResult.ok) {
        std::printf("FAIL  GATE-SCN-00 shipped-scenario-loads (%s: %s)\n\n0/1 passed\n",
                    shippedResult.failedId.c_str(), shippedResult.reason.c_str());
        return 1;
    }
    std::printf("      (data/ferrum_crossing.json loaded: %s, %dx%d, symmetry %s, "
                "turnCap %d, hash %s)\n",
                shipped.scenarioId.c_str(), shipped.bounds.cols, shipped.bounds.rows,
                symmetryName(shipped.symmetry), shipped.turnCap,
                scenarioHash(shipped).c_str());

    const int capIdx = captureRowIndex(units);
    if (capIdx < 0) {
        std::printf("FAIL  GATE-SCN-00 one-capture-row\n\n0/1 passed\n");
        return 1;
    }
    const UnitDef& cap = units[static_cast<std::size_t>(capIdx)];

    // --- T-SCN-01 -----------------------------------------------------------------
    // Exactly one isFlag placement per side, and it is a Tank. The flag is a scenario
    // PLACEMENT fact, not a fifth unit row -- §2.4 makes it "a designated Tank".
    bool ok01 = true;
    {
        if (!validateScenario(shipped, units, terrain).ok) { ok01 = false; why("shipped map"); }

        // the flag Ids in the shipped file are Tanks, and the table says so
        for (const ScenarioPlacement& p : shipped.placements) {
            if (!p.isFlag) continue;
            const UnitDef* d = findUnit(units, p.unitId);
            if (d == nullptr || d->type != UnitType::Tank) { ok01 = false; why("shipped flag is not a Tank"); }
        }

        Scenario onInfantry = shipped;      // the flag designated on a non-Tank
        for (ScenarioPlacement& p : onInfantry.placements)
            if (p.isFlag && p.side == 0) p.unitId = cap.id;
        ScenarioLoadResult a = validateScenario(onInfantry, units, terrain);
        if (a.ok || a.failedId != "T-SCN-01") { ok01 = false; whyStr("flag on an Infantry", a.failedId); }

        Scenario two = shipped;             // a SECOND flag Tank on one side
        place(two, 0, "Tank", 0, 6, true);
        ScenarioLoadResult b = validateScenario(two, units, terrain);
        if (b.ok || b.failedId != "T-SCN-01") { ok01 = false; whyStr("two flags on one side", b.failedId); }

        Scenario none = shipped;            // a side with no flag
        for (ScenarioPlacement& p : none.placements) if (p.side == 1) p.isFlag = false;
        ScenarioLoadResult c = validateScenario(none, units, terrain);
        if (c.ok || c.failedId != "T-SCN-01") { ok01 = false; whyStr("no flag on a side", c.failedId); }
    }
    check("T-SCN-01 one-flag-per-side-and-it-is-a-Tank", ok01);

    // --- T-SCN-02 -----------------------------------------------------------------
    bool ok02 = true;
    {
        Scenario oob = shipped;             // a placement off the board
        oob.placements[0].hex = offsetToAxial(11, 9);
        ScenarioLoadResult a = validateScenario(oob, units, terrain);
        if (a.ok || a.failedId != "T-SCN-02") { ok02 = false; whyStr("out-of-bounds placement", a.failedId); }

        Scenario badTerrain = shipped;      // a terrain Id no §4.8 row defines
        badTerrain.terrainId[0] = "Swamp";
        ScenarioLoadResult b = validateScenario(badTerrain, units, terrain);
        if (b.ok || b.failedId != "T-SCN-02") { ok02 = false; whyStr("unknown terrain Id", b.failedId); }

        Scenario badUnit = shipped;         // a unit Id no §4.8 row defines
        badUnit.placements[1].unitId = "Mech";
        ScenarioLoadResult c = validateScenario(badUnit, units, terrain);
        if (c.ok || c.failedId != "T-SCN-02") { ok02 = false; whyStr("unknown unit Id", c.failedId); }

        Scenario shared = shipped;          // two placements on one hex (§2.5)
        shared.placements[1].hex = shared.placements[0].hex;
        ScenarioLoadResult d = validateScenario(shared, units, terrain);
        if (d.ok || d.failedId != "T-SCN-02") { ok02 = false; whyStr("two placements one hex", d.failedId); }

        Scenario ownPlains = shipped;       // ownership naming a hex nobody can capture
        ownPlains.ownership[0].hex = offsetToAxial(0, 0);
        ScenarioLoadResult e = validateScenario(ownPlains, units, terrain);
        if (e.ok || e.failedId != "T-SCN-02") { ok02 = false; whyStr("ownership on Plains", e.failedId); }

        Scenario shortMap = shipped;        // dimensions and payload disagree
        shortMap.terrainId.pop_back();
        ScenarioLoadResult f = validateScenario(shortMap, units, terrain);
        if (f.ok || f.failedId != "T-SCN-02") { ok02 = false; whyStr("map payload short", f.failedId); }
    }
    check("T-SCN-02 structural-validity", ok02);

    // --- T-SCN-03 -----------------------------------------------------------------
    // One home factory per side, at least two neutral factories (§2.7's "~4 total").
    bool ok03 = true;
    {
        Scenario noHome = shipped;          // East's home factory left neutral
        for (ScenarioOwner& o : noHome.ownership)
            if (hexEqual(o.hex, offsetToAxial(9, 4))) o.owner = OWNER_NEUTRAL;
        ScenarioLoadResult a = validateScenario(noHome, units, terrain);
        if (a.ok || a.failedId != "T-SCN-03") { ok03 = false; whyStr("no East home factory", a.failedId); }

        Scenario twoHome = shipped;         // West owning a second factory
        for (ScenarioOwner& o : twoHome.ownership)
            if (hexEqual(o.hex, offsetToAxial(6, 2))) o.owner = 0;
        ScenarioLoadResult b = validateScenario(twoHome, units, terrain);
        if (b.ok || b.failedId != "T-SCN-03") { ok03 = false; whyStr("West owning two", b.failedId); }

        Scenario oneNeutral = shipped;      // South demoted to Plains: one neutral left
        {
            int c = 0, r = 0;
            axialToOffset(offsetToAxial(5, 7), c, r);
            oneNeutral.terrainId[static_cast<std::size_t>(r) * oneNeutral.bounds.cols + c] = "Plains";
            for (std::size_t i = 0; i < oneNeutral.ownership.size(); ++i)
                if (hexEqual(oneNeutral.ownership[i].hex, offsetToAxial(5, 7))) {
                    oneNeutral.ownership.erase(oneNeutral.ownership.begin() +
                                               static_cast<std::ptrdiff_t>(i));
                    break;
                }
        }
        ScenarioLoadResult c2 = validateScenario(oneNeutral, units, terrain);
        if (c2.ok || c2.failedId != "T-SCN-03") { ok03 = false; whyStr("one neutral factory", c2.failedId); }
    }
    check("T-SCN-03 economy-validity", ok03);

    // --- T-SCN-04 -----------------------------------------------------------------
    // A scenario cannot be born stalemated. The control is the SAME map with the
    // crossing re-opened, so the refusal is attributable to the wall.
    bool ok04 = true;
    {
        const ScenarioLoadResult walled = validateScenario(bisectedScenario(false), units, terrain);
        if (walled.ok || walled.failedId != "T-SCN-04") { ok04 = false; whyStr("bisected map", walled.failedId); }

        const ScenarioLoadResult bridged = validateScenario(bisectedScenario(true), units, terrain);
        if (!bridged.ok) { ok04 = false; whyStr("bridged control refused", bridged.failedId + ": " + bridged.reason); }

        if (!validateScenario(shipped, units, terrain).ok) { ok04 = false; why("shipped map"); }
    }
    check("T-SCN-04 playability-flags-mutually-reachable", ok04);

    // --- T-SCN-05 -----------------------------------------------------------------
    // odd-r (col,row) -> axial round-trips over the DECLARED dimensions, and the
    // loaded map's adjacency matches the authored grid's. The oracle here is the
    // offset neighbour rule, written from the convention rather than from Hex.h.
    bool ok05 = true;
    {
        for (int row = 0; row < shipped.bounds.rows && ok05; ++row) {
            for (int col = 0; col < shipped.bounds.cols && ok05; ++col) {
                const Hex h = offsetToAxial(col, row);
                int bc = 0, br = 0;
                axialToOffset(h, bc, br);
                if (bc != col || br != row) { ok05 = false; why("round-trip"); break; }
                if (!inBounds(h, shipped.bounds)) { ok05 = false; why("declared bounds reject a hex"); break; }

                Hex adj[HEX_DIRECTIONS];
                const int n = neighbors(h, shipped.bounds, adj);
                int expect[6][2];
                offsetNb(col, row, expect);
                int wanted = 0;
                for (int i = 0; i < 6; ++i) {
                    const int c = expect[i][0], r = expect[i][1];
                    if (c < 0 || r < 0 || c >= shipped.bounds.cols || r >= shipped.bounds.rows) continue;
                    ++wanted;
                    bool seen = false;
                    for (int k = 0; k < n; ++k) {
                        int ac = 0, ar = 0;
                        axialToOffset(adj[k], ac, ar);
                        if (ac == c && ar == r) { seen = true; break; }
                    }
                    if (!seen) { ok05 = false; why("adjacency drops an authored neighbour"); break; }
                }
                if (ok05 && wanted != n) { ok05 = false; whyInt("neighbour count", n, wanted); }
            }
        }
        // The invariant's other half is structural and not assertable at runtime: no
        // field on Scenario, ScenarioLane or ScenarioLoadResult has type (col,row) --
        // every one of them is a Hex, converted once at parse time.
        if (!validateScenario(shipped, units, terrain).ok) { ok05 = false; why("shipped map"); }
        // ... and on a differently-shaped grid, so nothing here is 11 x 9's accident.
        const ScenarioLoadResult wide = validateScenario(sevenLaneScenario(), units, terrain);
        if (wide.failedId == "T-SCN-05") { ok05 = false; why("a 20 x 3 grid fails the conversion"); }
    }
    check("T-SCN-05 odd-r-axial-round-trip-and-adjacency", ok05);

    // --- T-SCN-06 -----------------------------------------------------------------
    // The ceiling is DERIVED from the loaded table, never a literal, and the
    // existential is asserted over the NAMED hex.
    bool ok06 = true;
    {
        if (shippedResult.ceiling != 2 * cap.move) {
            ok06 = false; whyInt("ceiling", shippedResult.ceiling, 2 * cap.move); }
        if (shippedResult.captureMove != cap.move) {
            ok06 = false; whyInt("capture Move", shippedResult.captureMove, cap.move); }

        // Cost counts EVERY HEX ENTERED INCLUDING THE OBJECTIVE (T-MOVE-01's
        // accounting). West's lane is 5 hexes of Plains; East's is 4 hexes with one
        // mandatory Woods ring hex at 2 (§2.13.1). Drop the objective and they read
        // 4 and 4 instead, which is the arithmetic this pins.
        if (!priced(shipped, terrain, 1, 5, 5, 7, false, 5, "West guided lane, Bridge-free")) ok06 = false;
        if (!priced(shipped, terrain, 9, 3, 6, 2, false, 5, "East guided lane, Bridge-free")) ok06 = false;

        // A §2.4 Move change RE-PRICES the gate instead of silently passing it.
        std::vector<UnitDef> slower = units;
        slower[static_cast<std::size_t>(capIdx)].move = 2;      // ceiling 4, lanes 5
        ScenarioLoadResult a = validateScenario(shipped, slower, terrain);
        if (a.ok || a.failedId != "T-SCN-06") { ok06 = false; whyStr("Move 2 re-prices", a.failedId); }
        else if (a.ceiling != 4 || !contains(a.reason, "4 MP ceiling")) {
            ok06 = false; whyStr("re-priced reason", a.reason); }

        // The existential is over the NAMED hex. Move West's OTHER Infantry to (0,0)
        // and name it: (1,5) still qualifies at 5 MP, and the map must still be
        // refused, because the guided lane must be the one turn-1a actually marks.
        Scenario named = shipped;
        if (!movePlacement(named, 1, 3, 0, 0)) { ok06 = false; why("fixture edit"); }
        for (ScenarioGuided& g : named.guided)
            if (g.side == 0) g.infantry = offsetToAxial(0, 0);
        ScenarioLoadResult b = validateScenario(named, units, terrain);
        if (b.ok || b.failedId != "T-SCN-06") { ok06 = false; whyStr("named-hex quantifier", b.failedId); }
        if (modCost(named, terrain, 1, 5, 5, 7, false) != 5) {
            ok06 = false; why("the qualifying hex it must NOT use stopped qualifying"); }

        // Bridge-free is a property of the GUIDED lane. West's road to North costs 6
        // over the north Bridge (5,1) and 14 without it, from (1,3) (§4.7 T-SCN-11
        // asymmetry (ii)) -- so the clause is load-bearing, not decorative.
        if (!priced(shipped, terrain, 1, 3, 6, 2, true,   6, "West (1,3) -> North, Bridges allowed")) ok06 = false;
        if (!priced(shipped, terrain, 1, 3, 6, 2, false, 14, "West (1,3) -> North, Bridge-free")) ok06 = false;

        // Arrival only: nothing here asserts the turn the tile flips (Q4).
        const ScenarioLane* w = laneOf(shippedResult, 0);
        const ScenarioLane* e = laneOf(shippedResult, 1);
        if (w == nullptr || e == nullptr || !w->laneFound || !e->laneFound) {
            ok06 = false; why("lanes not reported"); }
        else if (w->laneCost > shippedResult.ceiling || e->laneCost > shippedResult.ceiling) {
            ok06 = false; why("a shipped lane exceeds its own ceiling"); }
    }
    check("T-SCN-06 opening-capture-lane-derived-ceiling", ok06);

    // --- T-SCN-07 -----------------------------------------------------------------
    bool ok07 = true;
    {
        Scenario notInfantry = shipped;     // the guided hex naming a Tank
        for (ScenarioGuided& g : notInfantry.guided)
            if (g.side == 0) g.infantry = offsetToAxial(0, 4);
        ScenarioLoadResult a = validateScenario(notInfantry, units, terrain);
        if (a.ok || a.failedId != "T-SCN-07") { ok07 = false; whyStr("guided unit is a Tank", a.failedId); }

        Scenario wrongSide = shipped;       // the guided hex naming the OTHER seat's unit
        for (ScenarioGuided& g : wrongSide.guided)
            if (g.side == 0) g.infantry = offsetToAxial(9, 3);
        ScenarioLoadResult b = validateScenario(wrongSide, units, terrain);
        if (b.ok || b.failedId != "T-SCN-07") { ok07 = false; whyStr("guided unit is the enemy's", b.failedId); }

        Scenario town = shipped;            // an objective that is a Town, not a Factory
        for (ScenarioGuided& g : town.guided)
            if (g.side == 0) g.objective = offsetToAxial(2, 7);
        ScenarioLoadResult c = validateScenario(town, units, terrain);
        if (c.ok || c.failedId != "T-SCN-07") { ok07 = false; whyStr("objective is a Town", c.failedId); }

        Scenario ownFactory = shipped;      // an objective that is not neutral
        for (ScenarioGuided& g : ownFactory.guided)
            if (g.side == 0) g.objective = offsetToAxial(1, 4);
        ScenarioLoadResult d = validateScenario(ownFactory, units, terrain);
        if (d.ok || d.failedId != "T-SCN-07") { ok07 = false; whyStr("objective is a home factory", d.failedId); }

        Scenario same = shipped;            // both seats naming ONE objective
        for (ScenarioGuided& g : same.guided) g.objective = offsetToAxial(6, 2);
        ScenarioLoadResult e = validateScenario(same, units, terrain);
        if (e.ok || e.failedId != "T-SCN-07") { ok07 = false; whyStr("shared objective", e.failedId); }

        Scenario doubled = shipped;         // two entries for one side
        doubled.guided.push_back(doubled.guided[0]);
        ScenarioLoadResult f = validateScenario(doubled, units, terrain);
        if (f.ok || f.failedId != "T-SCN-07") { ok07 = false; whyStr("two entries one side", f.failedId); }

        // Distinctness is a FLOOR, not the requirement: fixture (b) below names
        // different objectives and is still refused, by T-SCN-11 and nothing else.
    }
    check("T-SCN-07 opening-capture-naming", ok07);

    // --- T-SCN-08 -----------------------------------------------------------------
    // MEASURED, NOT INFERRED. The declared symmetry flag is not an input: the shipped
    // map declares `none` and offers nothing to infer from at all, and every integer
    // below is recomputed by the oracle before it is believed.
    bool ok08 = true;
    {
        const ScenarioLane* w = laneOf(shippedResult, 0);
        const ScenarioLane* e = laneOf(shippedResult, 1);
        if (w == nullptr || e == nullptr) { ok08 = false; why("no lane report"); }
        else {
            if (w->laneCost != 5) { ok08 = false; whyInt("West lane", w->laneCost, 5); }
            if (e->laneCost != 5) { ok08 = false; whyInt("East lane", e->laneCost, 5); }
            if (w->side != 0 || e->side != 1) { ok08 = false; why("lanes not in side order"); }
        }

        // §2.13.2's EIGHT-ROUTE table, in full, priced as opposing routes (Bridges
        // permitted, terrain alone, objective counted). A map edit that lengthens a
        // lane surfaces here as a changed number rather than a still-green boolean.
        if (!priced(shipped, terrain, 1, 3, 6, 2, true, 6, "West (1,3) -> North")) ok08 = false;
        if (!priced(shipped, terrain, 1, 3, 5, 7, true, 6, "West (1,3) -> South")) ok08 = false;
        if (!priced(shipped, terrain, 1, 5, 6, 2, true, 7, "West (1,5) -> North")) ok08 = false;
        if (!priced(shipped, terrain, 1, 5, 5, 7, true, 5, "West (1,5) -> South")) ok08 = false;
        if (!priced(shipped, terrain, 9, 3, 6, 2, true, 5, "East (9,3) -> North")) ok08 = false;
        if (!priced(shipped, terrain, 9, 3, 5, 7, true, 6, "East (9,3) -> South")) ok08 = false;
        if (!priced(shipped, terrain, 9, 1, 6, 2, true, 5, "East (9,1) -> North")) ok08 = false;
        if (!priced(shipped, terrain, 9, 1, 5, 7, true, 7, "East (9,1) -> South")) ok08 = false;

        // Fixture (c): a scenario whose lanes BOTH cost 7. It fails the T-SCN-06
        // ceiling, and the refusal carries the MEASURED integer against the CEILING,
        // named as a ceiling and never as a bare integer.
        const Scenario seven = sevenLaneScenario();
        const ScenarioLoadResult r = validateScenario(seven, units, terrain);
        if (r.ok || r.failedId != "T-SCN-06") { ok08 = false; whyStr("fixture (c)", r.failedId + ": " + r.reason); }
        if (r.lanes.size() != 2 || r.lanes[0].laneCost != 7 || r.lanes[1].laneCost != 7) {
            ok08 = false; why("fixture (c) did not report 7 and 7"); }
        if (!contains(r.reason, "costs 7") || !contains(r.reason, "6 MP ceiling")) {
            ok08 = false; whyStr("fixture (c) reason", r.reason); }
        if (oracleCost(seven, terrain, 1, 1, 8, 1, false) != 7 ||
            oracleCost(seven, terrain, 18, 1, 11, 1, false) != 7) {
            ok08 = false; why("fixture (c) oracle disagrees"); }

        // The flag cannot substitute for a measurement: re-declaring the shipped map
        // symmetric changes no integer, because nothing reads the flag to price a
        // lane. (It is refused by T-SCN-09, which is a different failure.)
        Scenario relabelled = shipped;
        relabelled.symmetry = Symmetry::Rot180;
        if (modCost(relabelled, terrain, 1, 5, 5, 7, false) != 5 ||
            modCost(relabelled, terrain, 9, 3, 6, 2, false) != 5) {
            ok08 = false; why("a lane price moved with the declaration"); }
    }
    check("T-SCN-08 lane-costs-measured-not-inferred", ok08);

    // --- T-SCN-09 -----------------------------------------------------------------
    // Only the REFUSAL branch runs. See the NOT RUN block at the end for the
    // asserting branch, which the scope ruling leaves without a fixture.
    bool ok09 = true;
    {
        // `none` asserts nothing and is always well-formed -- the shipped map sits on
        // a geometry that HAS a horizontal axis and is deliberately not drawn to it.
        if (shipped.symmetry != Symmetry::None) { ok09 = false; why("shipped map no longer declares none"); }
        if (!validateScenario(shipped, units, terrain).ok) { ok09 = false; why("none refused something"); }

        // rot180 on an ODD row count: the axial constant W - H/2 is a half-integer, so
        // no hex has a hex image and the file is refused BEFORE any comparison runs.
        // 11 x 9 is the shipped map's own dimensions; nothing else is mutated.
        Scenario odd = shipped;
        odd.symmetry = Symmetry::Rot180;
        const ScenarioLoadResult r = validateScenario(odd, units, terrain);
        if (r.ok || r.failedId != "T-SCN-09") { ok09 = false; whyStr("rot180 on 11x9", r.failedId); }
        if (!contains(r.reason, "row count is odd") || !contains(r.reason, "half-integer")) {
            ok09 = false; whyStr("refusal reason", r.reason); }
        // Refused BEFORE any comparison: the reason names the precondition, never a
        // terrain or ownership mismatch it could not meaningfully have computed.
        if (contains(r.reason, "different terrain") || contains(r.reason, "images to")) {
            ok09 = false; whyStr("refused by comparison, not precondition", r.reason); }
        // ... and nothing else about the map moved: it still passes as declared.
        if (!validateScenario(shipped, units, terrain).ok) { ok09 = false; why("mutation leaked"); }

        // Per Q25, guidedOpening is NOT bound by the declaration.
        if (r.lanes.size() != 0 && r.lanes.size() != 2) { ok09 = false; why("lane report shape"); }
    }
    check("T-SCN-09 declared-symmetry-verified-refusal-branch", ok09);

    // --- T-SCN-11 -----------------------------------------------------------------
    // NON-CONTENTION. The quantifier IS the invariant: the opposing route is minimised
    // over EVERY CanCapture-row unit that seat deploys (Q28), not over that seat's
    // guidedOpening.infantry alone.
    bool ok11 = true;
    {
        // (a) Ferrum Crossing passes in BOTH seats, reporting 5 against 6 each way.
        const ScenarioLane* w = laneOf(shippedResult, 0);
        const ScenarioLane* e = laneOf(shippedResult, 1);
        if (w == nullptr || e == nullptr) { ok11 = false; why("no lane report"); }
        else {
            if (w->laneCost != 5 || w->opposingCost != 6 || !w->opposingFound) {
                ok11 = false; whyInt("West: opposing", w->opposingCost, 6); }
            if (e->laneCost != 5 || e->opposingCost != 6 || !e->opposingFound) {
                ok11 = false; whyInt("East: opposing", e->opposingCost, 6); }
            // and the achieving hexes are the ones §2.13.2 names
            if (w != nullptr && !hexEqual(w->opposingFrom, offsetToAxial(9, 3))) {
                ok11 = false; whyStr("West's opposing minimiser", hexLabel(w->opposingFrom)); }
            if (e != nullptr && !hexEqual(e->opposingFrom, offsetToAxial(1, 3))) {
                ok11 = false; whyStr("East's opposing minimiser", hexLabel(e->opposingFrom)); }
        }

        // (b) THE FAILING FIXTURE IS REAL, NOT CONSTRUCTED. The same map with East's
        // second Infantry at its PRE-FIX hex (9,5) -- one placement changed, nothing
        // else -- must FAIL, reporting 5 against 5.
        Scenario preFix = shipped;
        if (!movePlacement(preFix, 9, 1, 9, 5)) { ok11 = false; why("fixture (b) edit"); }
        const ScenarioLoadResult b = validateScenario(preFix, units, terrain);
        if (b.ok) { ok11 = false; why("fixture (b) PASSED; it must be refused"); }
        else if (b.failedId != "T-SCN-11") { ok11 = false; whyStr("fixture (b) refused by", b.failedId); }
        if (!contains(b.reason, "5 against 5")) { ok11 = false; whyStr("fixture (b) reason", b.reason); }
        const ScenarioLane* bw = laneOf(b, 0);
        if (bw == nullptr || bw->laneCost != 5 || bw->opposingCost != 5) {
            ok11 = false; why("fixture (b) did not report 5 and 5"); }
        if (bw != nullptr && !hexEqual(bw->opposingFrom, offsetToAxial(9, 5))) {
            ok11 = false; whyStr("fixture (b) minimiser", hexLabel(bw->opposingFrom)); }
        // The five cost-1 hexes, and the axial distance, so no implementation detail
        // can make the number anything else.
        if (!priced(preFix, terrain, 9, 5, 5, 7, true, 5, "pre-fix (9,5) -> South")) ok11 = false;
        if (hexDistance(offsetToAxial(9, 5), offsetToAxial(5, 7)) != 5) {
            ok11 = false; why("axial distance from (9,5) to (5,7)"); }
        // (b) PINS THE QUANTIFIER, not merely the comparison: under the REFUSED
        // reading -- minimise over the opposing seat's guidedOpening.infantry alone --
        // it passes at 5 against 6, so an implementation that reads it that way fails
        // this fixture and nothing else in the suite.
        if (modCost(preFix, terrain, 9, 3, 5, 7, true) != 6) {
            ok11 = false; why("the refused reading no longer reads 6"); }

        // EQUALITY FAILS. A tie is precisely the race the rule exists to forbid.
        if (b.ok) { ok11 = false; why("a tie was accepted"); }

        // Asymmetry (i) NO CEILING: the opposing route may cost anything at all. On
        // the bisected control there is no route at all, and that is not a failure.
        const Scenario bis = bisectedScenario(true);
        const ScenarioLoadResult bisR = validateScenario(bis, units, terrain);
        if (!bisR.ok) { ok11 = false; whyStr("bridged control", bisR.failedId + ": " + bisR.reason); }
        const Scenario walled = bisectedScenario(false);
        const ScenarioLoadResult walledR = validateScenario(walled, units, terrain);
        for (const ScenarioLane& l : walledR.lanes)
            if (l.opposingFound) { ok11 = false; why("a walled map found an opposing route"); }

        // Asymmetry (ii) BRIDGES ARE ALLOWED on the opposing route, and EXCLUDING THEM
        // MOVES THE MINIMISER. With the Bridges, West's set minimum to North is 6,
        // achieved by (1,3), and (1,5) alone costs 7. Without them the set minimum is
        // 13, achieved by (1,5), and (1,3) alone costs 14. The achieving unit is not
        // the same unit under the two readings.
        if (!priced(shipped, terrain, 1, 5, 6, 2, false, 13, "West (1,5) -> North, Bridge-free")) ok11 = false;
        if (!priced(shipped, terrain, 1, 3, 6, 2, false, 14, "West (1,3) -> North, Bridge-free")) ok11 = false;
        // East's opposing route to South was already Bridge-free and does not move.
        if (!priced(shipped, terrain, 9, 3, 5, 7, false, 6, "East (9,3) -> South, Bridge-free")) ok11 = false;

        // Asymmetry (iii) THE UNIT SET IS BROADER THAN THE LANE. On the shipped map as
        // drawn the two readings agree in West's seat and disagree in East's: West's
        // set minimum to North is 6 from (1,3), while West's guided (1,5) alone is 7.
        if (modCost(shipped, terrain, 1, 5, 6, 2, true) != 7) {
            ok11 = false; why("West's guided-unit-alone figure to North"); }
        if (e != nullptr && e->opposingCost == modCost(shipped, terrain, 1, 5, 6, 2, true)) {
            ok11 = false; why("East's opposing term is the guided unit's cost, not the set minimum"); }

        // Both sides of the comparison are priced IDENTICALLY -- same pathfinder, same
        // accounting -- so the inequality means something.
        if (w != nullptr && modCost(shipped, terrain, 1, 5, 5, 7, false) != w->laneCost) {
            ok11 = false; why("owning lane priced two ways"); }
        if (w != nullptr && modCost(shipped, terrain, 9, 3, 5, 7, true) != w->opposingCost) {
            ok11 = false; why("opposing route priced two ways"); }
    }
    check("T-SCN-11 non-contention-minimised-over-every-capturer", ok11);

    // --- GATE-SCN-PARSE ------------------------------------------------------------
    // Not a §4.7 invariant and not a ledger ID: it gates the FILE FORMAT. No
    // third-party JSON is vendored, and the parser refuses malformed input rather
    // than tolerating it -- the same posture Data.h takes on the §4.8 CSVs.
    bool okParse = true;
    {
        Scenario tiny;
        if (!parseScenario(kTinyJson, "tiny", tiny).ok) { okParse = false; why("the valid fixture did not parse"); }

        // symmetry is REQUIRED: absent or unrecognized is a hard load failure, never a
        // silent default of `none`.
        if (!refusesParse(tinyWith(",\n  \"symmetry\": \"none\"", ""), "symmetry absent")) okParse = false;
        if (!refusesParse(tinyWith("\"symmetry\": \"none\"", "\"symmetry\": \"mirror\""), "symmetry mirror")) okParse = false;
        if (!refusesParse(tinyWith("\"symmetry\": \"none\"", "\"symmetry\": \"rot90\""), "symmetry rot90")) okParse = false;
        {   // and a scenario that forgets it does NOT come back declaring none
            Scenario probe;
            probe.symmetry = Symmetry::Rot180;
            parseScenario(tinyWith(",\n  \"symmetry\": \"none\"", ""), "probe", probe);
            if (probe.symmetry != Symmetry::Rot180) { okParse = false; why("a refused parse wrote to out"); }
        }

        if (!refusesParse(tinyWith("\"formatVersion\": 1", "\"formatVersion\": 2"), "unknown version")) okParse = false;
        if (!refusesParse(tinyWith("\"turnCap\": 20", "\"turnCap\": 20.5"), "non-integer")) okParse = false;
        if (!refusesParse(tinyWith("\"turnCap\": 20", "\"turnCap\": 020"), "leading zero")) okParse = false;
        if (!refusesParse(tinyWith("\"turnCap\": 20", "\"turnCap\": 1e3"), "exponent")) okParse = false;
        if (!refusesParse(tinyWith("\"turnCap\": 20", "\"turnCap\": null"), "null")) okParse = false;
        if (!refusesParse(tinyWith("\"turnCap\": 20", "\"turnCap\": 20, \"turnCap\": 21"), "duplicate key")) okParse = false;
        if (!refusesParse(tinyWith("\"turnCap\": 20", "\"turnCapp\": 20"), "unknown field")) okParse = false;
        if (!refusesParse(tinyWith("\"turnCap\": 20,", ""), "missing field")) okParse = false;
        if (!refusesParse(tinyWith("\"ownership\": []", "\"ownership\": [{\"hex\": [0,0], \"owner\": 0},]"), "trailing comma")) okParse = false;
        if (!refusesParse(tinyWith("\"scenarioId\": \"tiny\"", "\"scenarioId\": \"ti\\u0041ny\""), "\\u escape")) okParse = false;
        if (!refusesParse(tinyWith("\"scenarioId\": \"tiny\"", "\"scenarioId\": \"tiny"), "unterminated string")) okParse = false;
        if (!refusesParse(std::string(kTinyJson) + " {}", "trailing content")) okParse = false;
        if (!refusesParse(tinyWith("[[\"Plains\", \"Plains\"]]", "[[\"Plains\", \"Plains\", \"Plains\"]]"), "row longer than width")) okParse = false;
        if (!refusesParse(tinyWith("[[\"Plains\", \"Plains\"]]", "[[\"Plains\"], [\"Plains\"]]"), "more rows than height")) okParse = false;
        if (!refusesParse(tinyWith("\"ownership\": []", "\"ownership\": [{\"hex\": [0,0], \"owner\": 7}]"), "owner out of range")) okParse = false;
        if (!refusesParse(tinyWith("\"hex\": [0, 0]", "\"hex\": [0]"), "hex is not [col,row]")) okParse = false;
        if (!refusesParse(tinyWith("\"isFlag\": true", "\"isFlag\": 1"), "isFlag is not a boolean")) okParse = false;
        if (!refusesParse("", "empty file")) okParse = false;
        if (!refusesParse("[]", "root is an array")) okParse = false;

        // A refused parse leaves the caller's Scenario untouched -- no half-parsed file.
        Scenario keep = shipped;
        parseScenario("{ nonsense", "probe", keep);
        if (keep.scenarioId != shipped.scenarioId || keep.terrainId.size() != shipped.terrainId.size()) {
            okParse = false; why("a refused parse half-wrote its out parameter"); }
    }
    check("GATE-SCN-PARSE malformed-input-is-refused", okParse);

    // --- GATE-SCN-HASH -------------------------------------------------------------
    // The canonical serialization: fields in the §4.7 Stub 7 order, hexes in canonical
    // hex order, so the digest is content-only and platform-stable.
    bool okHash = true;
    {
        const std::string h = scenarioHash(shipped);
        if (h.size() != 16) { okHash = false; whyStr("digest width", h); }
        if (scenarioHash(shipped) != h) { okHash = false; why("not stable across calls"); }

        // The shipped file declares its own hash, and it matches. That is the check
        // that catches a transcription edit that forgot to re-derive it.
        if (!shipped.hasDeclaredHash) { okHash = false; why("the shipped file declares no hash"); }
        else if (shipped.declaredHash != h) { okHash = false; whyStr("declared hash", shipped.declaredHash); }

        // Authoring ORDER is not content: reversing every array leaves the hash alone.
        Scenario shuffled = shipped;
        std::reverse(shuffled.ownership.begin(), shuffled.ownership.end());
        std::reverse(shuffled.placements.begin(), shuffled.placements.end());
        std::reverse(shuffled.guided.begin(), shuffled.guided.end());
        if (scenarioHash(shuffled) != h) { okHash = false; why("authoring order moved the hash"); }

        // ... but content is. Every field in the preimage moves it.
        Scenario m1 = shipped; m1.turnCap = 21;
        Scenario m2 = shipped; m2.terrainId[0] = "Woods";
        Scenario m3 = shipped; m3.symmetry = Symmetry::Rot180;
        Scenario m4 = shipped; m4.startingFame[1] = 350;
        Scenario m5 = shipped; m5.placements[0].isFlag = false;
        Scenario m6 = shipped; m6.ownership[0].owner = OWNER_NEUTRAL;
        Scenario m7 = shipped; m7.guided[0].objective = offsetToAxial(6, 2);
        Scenario m8 = shipped; m8.scenarioId = "ferrum_crossing_b";
        const Scenario* moved[8] = {&m1, &m2, &m3, &m4, &m5, &m6, &m7, &m8};
        for (int i = 0; i < 8; ++i)
            if (scenarioHash(*moved[i]) == h) {
                okHash = false; std::printf("      (mutation %d did not move the hash)\n", i + 1); }

        // A file whose declared hash disagrees with its own content is REFUSED, and
        // the refusal names both digests so an author can re-derive rather than guess.
        {
            std::ifstream in(shippedPath.c_str(), std::ios::binary);
            std::ostringstream buf;
            buf << in.rdbuf();
            std::string text = buf.str();
            const std::size_t at = text.find(h);
            if (at == std::string::npos) { okHash = false; why("the file does not carry its digest verbatim"); }
            else {
                text.replace(at, h.size(), "deadbeefdeadbeef");
                const char* probe = "scn_hash_probe.json";
                { std::ofstream out(probe, std::ios::binary); out << text; }
                Scenario junk;
                const ScenarioLoadResult r = loadScenario(probe, units, terrain, junk);
                if (r.ok || r.failedId != "GATE-SCN-HASH") {
                    okHash = false; whyStr("wrong declared hash", r.failedId); }
                else if (!contains(r.reason, "deadbeefdeadbeef") || !contains(r.reason, h)) {
                    okHash = false; whyStr("refusal reason", r.reason); }
                std::remove(probe);
            }
        }
    }
    check("GATE-SCN-HASH canonical-serialization-is-content-only", okHash);

    // --- what did NOT run ------------------------------------------------------------
    // Printed by name and with a reason. A suite that quietly omits a fixture reads as
    // a complete pass, and this one is deliberately not complete -- so row 7's ledger
    // row does not flip on it (Q29).
    std::printf("\n");
    std::printf("NOT RUN  T-SCN-08 (a) -- The Causeway, reporting 3 and 3. Needs §2.13.6 as a\n");
    std::printf("         scenario file; the Director's scope ruling authors none, not even as\n");
    std::printf("         a fixture. No synthetic map may stand in: a lost fixture is reported,\n");
    std::printf("         never replaced.\n");
    std::printf("NOT RUN  T-SCN-08 (b) -- Longwater March, rot180 on 13 x 8, reporting 4 and 4.\n");
    std::printf("         Same reason (§2.13.5).\n");
    std::printf("NOT RUN  T-SCN-09 asserting branch -- rho asserts hex by hex, and the only\n");
    std::printf("         scenario file that exists declares `none`, which asserts nothing. The\n");
    std::printf("         refusal branch above IS reachable from the shipped map's own\n");
    std::printf("         declaration and does run.\n");
    std::printf("NOT RUN  T-SCN-11 (c) -- The Causeway, 3 against 5 with the crossing permitted.\n");
    std::printf("         Same reason (§2.13.6). Asymmetry (ii) is exercised above on the\n");
    std::printf("         shipped map instead, where excluding the Bridges MOVES THE MINIMISER\n");
    std::printf("         from (1,3) to (1,5) -- which is a weaker witness than a bisected map,\n");
    std::printf("         because no gate here fails under the Bridge-free reading.\n");
    std::printf("NOT RUN  T-SCN-10 -- reserved and UNWRITTEN on Q26 (ruled): the enum stays at\n");
    std::printf("         rot180 | none, so a horizontal mirror is undeclarable and there is\n");
    std::printf("         nothing for a gate to verify. Nothing is asserted, so nothing is\n");
    std::printf("         waiting -- a different state from T-MOVE-07, which IS blocked.\n");

    std::printf("\n%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
