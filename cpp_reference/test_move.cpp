// Test Engineer's gate for §4.11 row 3 — Movement & pathfinding (T-MOVE-01..06).
// T-MOVE-07 is reserved and unwritten: Recon's terrain discount is blocked on the Q2
// movement-class ruling, and no gate is written until the rule exists.
//
// Row 3 depends on rows 1 and 2, and this gate depends on them the same way: it links
// Hex.cpp and Data.cpp and takes its move costs from data/terrain.csv and its unit
// Move values from data/units.csv. argv[1] overrides the data directory.
//
// T-MOVE-01 is checked against an INDEPENDENT shortest-path computation (repeated
// relaxation, written below), never against the module's own Dijkstra. Comparing a
// search to itself would assert nothing, and §2.5's promise — "the real move set, not
// an estimate" — is exactly the claim a self-comparison cannot test.
#include "Data.h"
#include "Hex.h"
#include "Move.h"

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

static const int kInf = 1000000000;

// --- the fixture map ------------------------------------------------------------
// 7 x 5. A Water column splits it in two and ONE Bridge crosses it, which is what
// makes §2.3's "the only hex a land unit crosses Water" assertable.
//   . Plains   F Woods   M Mountains   ~ Water   B Bridge   T Town   X Factory
static const char* kRows[5] = {
    "...~...",
    ".F.~...",
    "...B...",
    ".M.~...",
    "...~...",
};
static const int kCols = 7;
static const int kRowCount = 5;

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

// Builds the fixture. `swapBridgeForWater` closes the single crossing.
static bool buildBoard(const std::vector<TerrainDef>& terrain, Board& out,
                       bool swapBridgeForWater = false) {
    out.bounds.cols = kCols;
    out.bounds.rows = kRowCount;
    out.terrain.assign(static_cast<std::size_t>(kCols) * kRowCount, -1);
    out.occupant.assign(static_cast<std::size_t>(kCols) * kRowCount, OCCUPANT_NONE);
    for (int row = 0; row < kRowCount; ++row) {
        for (int col = 0; col < kCols; ++col) {
            char c = kRows[row][col];
            if (swapBridgeForWater && c == 'B') c = '~';
            const char* id = terrainIdFor(c);
            if (id == nullptr) return false;
            int index = -1;
            for (std::size_t i = 0; i < terrain.size(); ++i)
                if (terrain[i].id == id) index = static_cast<int>(i);
            if (index < 0) return false;
            out.terrain[static_cast<std::size_t>(row) * kCols + col] = index;
        }
    }
    return true;
}

static Hex at(int col, int row) { return offsetToAxial(col, row); }

// --- the independent oracle -----------------------------------------------------
// Repeated relaxation over every hex until nothing changes. Deliberately a different
// algorithm from the module's Dijkstra, and deliberately re-derives the two rules the
// module also applies (impassable terrain, Q3 occupancy) from the same data.
static std::vector<int> oracleCosts(const Board& board, const std::vector<TerrainDef>& terrain,
                                    const Hex& start) {
    std::vector<int> cost(board.terrain.size(), kInf);
    const int s = board.index(start);
    if (s < 0) return cost;
    cost[s] = 0;
    bool changed = true;
    while (changed) {
        changed = false;
        for (int row = 0; row < board.bounds.rows; ++row) {
            for (int col = 0; col < board.bounds.cols; ++col) {
                const Hex h = at(col, row);
                const int hi = board.index(h);
                if (hi < 0 || cost[hi] == kInf) continue;
                Hex adj[HEX_DIRECTIONS];
                const int n = neighbors(h, board.bounds, adj);
                for (int i = 0; i < n; ++i) {
                    const int ni = board.index(adj[i]);
                    if (ni < 0) continue;
                    if (board.occupant[ni] != OCCUPANT_NONE) continue;   // Q3 conservative
                    const int step = terrain[board.terrain[ni]].moveCost;
                    if (step <= 0) continue;                             // impassable
                    if (cost[hi] + step < cost[ni]) { cost[ni] = cost[hi] + step; changed = true; }
                }
            }
        }
    }
    return cost;
}

// True when the module's reachable set matches the oracle exactly, hex for hex and
// cost for cost, and is returned in canonical order.
static bool reachableMatchesOracle(const Board& board, const std::vector<TerrainDef>& terrain,
                                   const Hex& start, int allowance) {
    const std::vector<ReachEntry> got = reachable(board, terrain, start, allowance);
    const std::vector<int> oracle = oracleCosts(board, terrain, start);

    std::size_t expected = 0;
    for (int i : oracle) if (i <= allowance) ++expected;
    if (got.size() != expected) return false;

    for (const ReachEntry& e : got) {
        const int i = board.index(e.hex);
        if (i < 0) return false;
        if (oracle[i] > allowance) return false;   // in the set but not truly reachable
        if (oracle[i] != e.cost) return false;     // right hex, wrong price
    }
    for (std::size_t i = 1; i < got.size(); ++i)
        if (!hexLess(got[i - 1].hex, got[i].hex)) return false;   // canonical order
    return true;
}

// Every minimal-cost path from start to goal, by exhaustive DFS. Used only by
// T-MOVE-04, and only on the small fixture.
static void enumerateMinPaths(const Board& board, const std::vector<TerrainDef>& terrain,
                              const Hex& goal, int minCost,
                              std::vector<Hex>& current, int currentCost,
                              std::vector<std::vector<Hex> >& out) {
    if (currentCost > minCost) return;
    if (hexEqual(current.back(), goal)) {
        if (currentCost == minCost) out.push_back(current);
        return;
    }
    Hex adj[HEX_DIRECTIONS];
    const int n = neighbors(current.back(), board.bounds, adj);
    for (int i = 0; i < n; ++i) {
        const int ni = board.index(adj[i]);
        if (ni < 0 || board.occupant[ni] != OCCUPANT_NONE) continue;
        const int step = terrain[board.terrain[ni]].moveCost;
        if (step <= 0) continue;
        bool visited = false;
        for (const Hex& h : current) if (hexEqual(h, adj[i])) visited = true;
        if (visited) continue;
        current.push_back(adj[i]);
        enumerateMinPaths(board, terrain, goal, minCost, current, currentCost + step, out);
        current.pop_back();
    }
}

static bool pathLexLess(const std::vector<Hex>& a, const std::vector<Hex>& b) {
    const std::size_t n = (a.size() < b.size()) ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (hexLess(a[i], b[i])) return true;
        if (hexLess(b[i], a[i])) return false;
    }
    return a.size() < b.size();
}

static bool samePath(const std::vector<Hex>& a, const std::vector<Hex>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (!hexEqual(a[i], b[i])) return false;
    return true;
}

static const ReachEntry* entryFor(const std::vector<ReachEntry>& set, const Hex& h) {
    for (const ReachEntry& e : set) if (hexEqual(e.hex, h)) return &e;
    return nullptr;
}

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? std::string(argv[1]) : std::string("../data");

    std::vector<UnitDef> units;
    std::vector<TerrainDef> terrain;
    std::string err;
    if (!loadUnits(dir + "/units.csv", units, err) ||
        !loadTerrain(dir + "/terrain.csv", terrain, err)) {
        std::printf("FAIL  T-MOVE-00 load-row-2-tables (%s)\n", err.c_str());
        std::printf("\n0/1 passed\n");
        return 1;
    }
    const UnitDef* infantry = findUnit(units, "Infantry");
    const UnitDef* tank     = findUnit(units, "Tank");
    const UnitDef* recon    = findUnit(units, "Recon");
    if (infantry == nullptr || tank == nullptr || recon == nullptr) {
        std::printf("FAIL  T-MOVE-00 units-missing\n\n0/1 passed\n");
        return 1;
    }

    Board board;
    if (!buildBoard(terrain, board) || !board.valid()) {
        std::printf("FAIL  T-MOVE-00 fixture-build\n\n0/1 passed\n");
        return 1;
    }

    // --- T-MOVE-01 -------------------------------------------------------------
    // The set is EXACT, against the oracle, from every start on the board and at
    // each of the three real unit allowances (§2.4: Infantry 3, Tank 5, Recon 7).
    bool ok01 = true;
    const int allowances[3] = { infantry->move, tank->move, recon->move };
    for (int row = 0; row < kRowCount; ++row) {
        for (int col = 0; col < kCols; ++col) {
            if (terrain[board.terrain[static_cast<std::size_t>(row) * kCols + col]].moveCost <= 0)
                continue;                                   // no land unit starts in Water
            for (int a = 0; a < 3; ++a)
                if (!reachableMatchesOracle(board, terrain, at(col, row), allowances[a]))
                    ok01 = false;
        }
    }
    // The start hex is in the set, at cost 0 — the null move (move_spec.md reading 2).
    {
        const std::vector<ReachEntry> set = reachable(board, terrain, at(0, 2), infantry->move);
        const ReachEntry* self = entryFor(set, at(0, 2));
        if (self == nullptr || self->cost != 0) ok01 = false;
    }
    check("T-MOVE-01 reachable-set-is-exact", ok01);

    // --- T-MOVE-02 -------------------------------------------------------------
    // Costs are §2.3's, and Water is crossed only on a Bridge.
    bool ok02 = true;
    {
        // A single step onto each terrain costs exactly what §2.3 says.
        const std::vector<ReachEntry> fromWoodsEdge = reachable(board, terrain, at(0, 1), infantry->move);
        const ReachEntry* woods = entryFor(fromWoodsEdge, at(1, 1));
        if (woods == nullptr || woods->cost != 2) ok02 = false;             // Woods 2

        const std::vector<ReachEntry> fromMountEdge = reachable(board, terrain, at(0, 3), infantry->move);
        const ReachEntry* mountain = entryFor(fromMountEdge, at(1, 3));
        if (mountain == nullptr || mountain->cost != 3) ok02 = false;       // Mountains 3

        const std::vector<ReachEntry> fromBridgeEdge = reachable(board, terrain, at(2, 2), infantry->move);
        const ReachEntry* bridge = entryFor(fromBridgeEdge, at(3, 2));
        if (bridge == nullptr || bridge->cost != 1) ok02 = false;           // Bridge 1

        // No Water hex is ever entered, at any allowance, from anywhere.
        for (int row = 0; row < kRowCount; ++row)
            for (int col = 0; col < kCols; ++col) {
                if (terrain[board.terrain[static_cast<std::size_t>(row) * kCols + col]].moveCost <= 0)
                    continue;
                const std::vector<ReachEntry> set = reachable(board, terrain, at(col, row), 99);
                for (const ReachEntry& e : set) {
                    const int t = board.terrain[board.index(e.hex)];
                    if (!terrain[t].passLand) ok02 = false;
                }
            }

        // The biconditional. WITH the bridge: the far bank is reachable and the route
        // crosses on the Bridge hex.
        std::vector<Hex> path;
        int cost = 0;
        const bool crossed = findPath(board, terrain, at(0, 2), at(6, 2), 99, path, cost);
        if (!crossed) ok02 = false;
        bool usesBridge = false;
        for (const Hex& h : path)
            if (terrain[board.terrain[board.index(h)]].id == "Bridge") usesBridge = true;
        if (!usesBridge) ok02 = false;

        // WITHOUT it: the far bank is unreachable at any allowance — Water is closed,
        // so the Bridge is the only crossing rather than merely the cheapest one.
        Board severed;
        if (!buildBoard(terrain, severed, /*swapBridgeForWater=*/true)) ok02 = false;
        std::vector<Hex> none;
        int noCost = 0;
        if (findPath(severed, terrain, at(0, 2), at(6, 2), 99, none, noCost)) ok02 = false;
        const std::vector<ReachEntry> leftOnly = reachable(severed, terrain, at(0, 2), 99);
        for (const ReachEntry& e : leftOnly) {
            int col = 0, row = 0;
            axialToOffset(e.hex, col, row);
            if (col > 2) ok02 = false;              // nothing past the water column
        }
    }
    check("T-MOVE-02 terrain-costs-and-bridge-only-crossing", ok02);

    // --- T-MOVE-03 -------------------------------------------------------------
    // A move never ends on an occupied hex, and under the conservative Q3 reading it
    // never crosses one either.
    bool ok03 = true;
    {
        Board occupied = board;
        occupied.occupant[static_cast<std::size_t>(2) * kCols + 2] = 7;   // (2,2)
        const std::vector<ReachEntry> set = reachable(occupied, terrain, at(0, 2), infantry->move);
        if (entryFor(set, at(2, 2)) != nullptr) ok03 = false;

        std::vector<Hex> path;
        int cost = 0;
        if (findPath(occupied, terrain, at(0, 2), at(2, 2), infantry->move, path, cost)) ok03 = false;

        // Pass-through: block the one crossing and the far bank goes with it.
        Board blocked = board;
        blocked.occupant[static_cast<std::size_t>(2) * kCols + 3] = 7;    // the Bridge (3,2)
        std::vector<Hex> across;
        int acrossCost = 0;
        if (findPath(blocked, terrain, at(0, 2), at(6, 2), 99, across, acrossCost)) ok03 = false;

        // Friendly or enemy makes no difference while Q3 is unruled — the reading is
        // "occupied", not "hostile", and that is what ships.
        Board friendlyOccupant = board;
        Board enemyOccupant    = board;
        friendlyOccupant.occupant[static_cast<std::size_t>(2) * kCols + 2] = 1;
        enemyOccupant.occupant[static_cast<std::size_t>(2) * kCols + 2]    = 99;
        const std::vector<ReachEntry> f = reachable(friendlyOccupant, terrain, at(0, 2), infantry->move);
        const std::vector<ReachEntry> e = reachable(enemyOccupant, terrain, at(0, 2), infantry->move);
        if (f.size() != e.size()) ok03 = false;
        else for (std::size_t i = 0; i < f.size(); ++i)
            if (!hexEqual(f[i].hex, e[i].hex) || f[i].cost != e[i].cost) ok03 = false;

        // And the oracle agrees about all of it.
        if (!reachableMatchesOracle(occupied, terrain, at(0, 2), infantry->move)) ok03 = false;
        if (!reachableMatchesOracle(blocked,  terrain, at(0, 2), 99))             ok03 = false;
    }
    check("T-MOVE-03 occupancy-blocks-end-and-crossing", ok03);

    // --- T-MOVE-04 -------------------------------------------------------------
    // Minimal cost, and the tie broken by canonical hex order. The fixture is chosen
    // so that a tie genuinely exists: reaching the Woods hex (1,1) from (0,0) costs 3
    // by two different routes. The test asserts the tie exists before asserting how
    // it is broken — a tie-break rule no fixture exercises is not tested.
    bool ok04 = true;
    {
        const Hex start = at(0, 0);
        const Hex goal  = at(1, 1);
        const std::vector<int> oracle = oracleCosts(board, terrain, start);
        const int minCost = oracle[board.index(goal)];

        std::vector<std::vector<Hex> > all;
        std::vector<Hex> current;
        current.push_back(start);
        enumerateMinPaths(board, terrain, goal, minCost, current, 0, all);
        if (all.size() < 2) ok04 = false;            // the tie must be real

        std::vector<Hex> lexMin = all.empty() ? std::vector<Hex>() : all[0];
        for (const std::vector<Hex>& p : all) if (pathLexLess(p, lexMin)) lexMin = p;

        std::vector<Hex> chosen;
        int chosenCost = 0;
        if (!findPath(board, terrain, start, goal, infantry->move, chosen, chosenCost)) ok04 = false;
        if (chosenCost != minCost) ok04 = false;     // minimal cost
        if (!samePath(chosen, lexMin)) ok04 = false; // and the canonical one among equals

        // Endpoints are included, and consecutive hexes are adjacent.
        if (chosen.size() < 2 || !hexEqual(chosen.front(), start) || !hexEqual(chosen.back(), goal))
            ok04 = false;
        for (std::size_t i = 1; i < chosen.size(); ++i)
            if (hexDistance(chosen[i - 1], chosen[i]) != 1) ok04 = false;

        // The stated cost is the sum of the steps' ENTRY costs, start free.
        int summed = 0;
        for (std::size_t i = 1; i < chosen.size(); ++i)
            summed += terrain[board.terrain[board.index(chosen[i])]].moveCost;
        if (summed != chosenCost) ok04 = false;

        // Out of allowance is a refusal, not a truncated path.
        std::vector<Hex> tooFar;
        int tooFarCost = 0;
        if (findPath(board, terrain, at(0, 2), at(6, 2), 2, tooFar, tooFarCost)) ok04 = false;
    }
    check("T-MOVE-04 minimal-cost-canonical-tiebreak", ok04);

    // --- T-MOVE-05 -------------------------------------------------------------
    // No zones of control. The enemy sits on the Bridge, whose cost is exactly the
    // allowance, so nothing routes THROUGH it and the only legitimate difference is
    // that its own hex is gone. Everything else — including the hex adjacent to the
    // enemy — must be priced identically.
    bool ok05 = true;
    {
        const std::vector<ReachEntry> before = reachable(board, terrain, at(0, 2), infantry->move);
        const ReachEntry* bridgeBefore = entryFor(before, at(3, 2));
        if (bridgeBefore == nullptr || bridgeBefore->cost != infantry->move) ok05 = false;

        Board withEnemy = board;
        withEnemy.occupant[static_cast<std::size_t>(2) * kCols + 3] = 99;
        const std::vector<ReachEntry> after = reachable(withEnemy, terrain, at(0, 2), infantry->move);

        if (after.size() + 1 != before.size()) ok05 = false;
        for (const ReachEntry& b : before) {
            if (hexEqual(b.hex, at(3, 2))) {
                if (entryFor(after, b.hex) != nullptr) ok05 = false;   // enemy hex removed
                continue;
            }
            const ReachEntry* a = entryFor(after, b.hex);
            if (a == nullptr || a->cost != b.cost) ok05 = false;       // nothing else moved
        }
        // The hex adjacent to the enemy is neither more expensive nor frozen.
        const ReachEntry* adjacent = entryFor(after, at(2, 2));
        if (adjacent == nullptr || adjacent->cost != 2) ok05 = false;
        if (hexDistance(at(2, 2), at(3, 2)) != 1) ok05 = false;
    }
    check("T-MOVE-05 no-zones-of-control", ok05);

    // --- T-MOVE-06 -------------------------------------------------------------
    // Same state in, identical set and identical path out — including a board rebuilt
    // from scratch rather than copied.
    bool ok06 = true;
    {
        const std::vector<ReachEntry> a1 = reachable(board, terrain, at(0, 2), tank->move);
        const std::vector<ReachEntry> a2 = reachable(board, terrain, at(0, 2), tank->move);
        Board rebuilt;
        buildBoard(terrain, rebuilt);
        const std::vector<ReachEntry> a3 = reachable(rebuilt, terrain, at(0, 2), tank->move);
        if (a1.size() != a2.size() || a1.size() != a3.size()) ok06 = false;
        else for (std::size_t i = 0; i < a1.size(); ++i) {
            if (!hexEqual(a1[i].hex, a2[i].hex) || a1[i].cost != a2[i].cost) ok06 = false;
            if (!hexEqual(a1[i].hex, a3[i].hex) || a1[i].cost != a3[i].cost) ok06 = false;
        }

        std::vector<Hex> p1, p2, p3;
        int c1 = 0, c2 = 0, c3 = 0;
        const bool r1 = findPath(board,   terrain, at(0, 2), at(6, 2), 99, p1, c1);
        const bool r2 = findPath(board,   terrain, at(0, 2), at(6, 2), 99, p2, c2);
        const bool r3 = findPath(rebuilt, terrain, at(0, 2), at(6, 2), 99, p3, c3);
        if (!r1 || !r2 || !r3) ok06 = false;
        if (c1 != c2 || c1 != c3) ok06 = false;
        if (!samePath(p1, p2) || !samePath(p1, p3)) ok06 = false;
    }
    check("T-MOVE-06 determinism", ok06);

    std::printf("\n%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
