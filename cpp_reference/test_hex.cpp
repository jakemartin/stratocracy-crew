// Test Engineer's gate for §4.11 row 1 — Hex grid & math (T-HEX-01..07).
// Dependency-free apart from <cstdio>/<vector>. Exits non-zero on any failure so the
// crew's compile+run tool can block a merge mechanically.
//
// It links Combat.cpp as well, because T-HEX-06 asserts that combat's range checks
// consume THIS distance function — the invariant only means something if both are in
// the same binary.
#include "Combat.h"
#include "Hex.h"

#include <cstdio>
#include <vector>

using namespace strat;

static int g_total = 0;
static int g_fail  = 0;

static void check(const char* name, bool cond) {
    ++g_total;
    if (cond) { std::printf("PASS  %s\n", name); }
    else      { ++g_fail; std::printf("FAIL  %s\n", name); }
}

// Ferrum Crossing ships 11 x 9 (§2.13.2). Q1 is ruled: bounds are per-scenario data,
// so this is the gate's fixture, not a constant the module knows.
static const MapBounds kMap{11, 9};

// The documented enumeration order, restated here independently of the module so the
// test pins it rather than echoes it (hex_spec.md): E, NE, NW, W, SW, SE.
static const int kExpectQ[6] = { +1, +1,  0, -1, -1,  0 };
static const int kExpectR[6] = {  0, -1, -1,  0, +1, +1 };

static std::vector<Hex> allInBounds() {
    std::vector<Hex> v;
    for (int row = 0; row < kMap.rows; ++row)
        for (int col = 0; col < kMap.cols; ++col)
            v.push_back(offsetToAxial(col, row));
    return v;
}

int main() {
    const std::vector<Hex> board = allInBounds();

    // --- T-HEX-01 --------------------------------------------------------------
    // Six candidates, in the fixed order, always. Filtering removes ONLY the
    // out-of-bounds ones and preserves the survivors' relative order.
    bool ok01 = !board.empty();
    for (const Hex& h : board) {
        Hex cand[6];
        for (int d = 0; d < 6; ++d) {
            cand[d] = neighborCandidate(h, d);
            if (cand[d].q != h.q + kExpectQ[d] || cand[d].r != h.r + kExpectR[d]) ok01 = false;
        }
        for (int a = 0; a < 6; ++a)          // the six candidates are distinct
            for (int b = a + 1; b < 6; ++b)
                if (hexEqual(cand[a], cand[b])) ok01 = false;

        std::vector<Hex> expected;           // the in-bounds subsequence, in order
        for (int d = 0; d < 6; ++d)
            if (inBounds(cand[d], kMap)) expected.push_back(cand[d]);

        Hex got[6];
        const int n = neighbors(h, kMap, got);
        if (n != static_cast<int>(expected.size())) { ok01 = false; continue; }
        for (int i = 0; i < n; ++i)
            if (!hexEqual(got[i], expected[i])) ok01 = false;
    }
    check("T-HEX-01 six-candidates-fixed-order-filter-only-oob", ok01);

    // --- T-HEX-02 --------------------------------------------------------------
    // Distance is a metric: identity, symmetry, triangle inequality. Checked over
    // every ordered triple on the fixture board.
    bool ok02 = true;
    for (const Hex& a : board) {
        if (hexDistance(a, a) != 0) ok02 = false;
        for (const Hex& b : board) {
            if (hexDistance(a, b) != hexDistance(b, a)) ok02 = false;
            if (hexDistance(a, b) < 0) ok02 = false;
            if (hexDistance(a, b) == 0 && !hexEqual(a, b)) ok02 = false;
        }
    }
    for (const Hex& a : board)
        for (const Hex& b : board)
            for (const Hex& c : board)
                if (hexDistance(a, c) > hexDistance(a, b) + hexDistance(b, c)) ok02 = false;
    check("T-HEX-02 distance-is-a-metric", ok02);

    // --- T-HEX-03 --------------------------------------------------------------
    // d(a,b) == 1  <=>  b is a neighbor of a. Both directions, every pair.
    bool ok03 = true;
    for (const Hex& a : board) {
        Hex adj[6];
        const int n = neighbors(a, kMap, adj);
        for (const Hex& b : board) {
            bool isNeighbor = false;
            for (int i = 0; i < n; ++i) if (hexEqual(adj[i], b)) isNeighbor = true;
            if ((hexDistance(a, b) == 1) != isNeighbor) ok03 = false;
        }
    }
    check("T-HEX-03 distance-1-iff-neighbor", ok03);

    // --- T-HEX-04 --------------------------------------------------------------
    // Direction fairness: all six unit steps cost exactly 1 — no direction is cheap
    // the way diagonals are on a square grid (§2.2).
    bool ok04 = true;
    for (const Hex& h : board)
        for (int d = 0; d < 6; ++d)
            if (hexDistance(h, neighborCandidate(h, d)) != 1) ok04 = false;
    check("T-HEX-04 direction-fairness", ok04);

    // --- T-HEX-05 --------------------------------------------------------------
    // inBounds agrees with the Q1 dimensions, and nothing outside them is trusted.
    bool ok05 = true;
    for (int row = 0; row < kMap.rows; ++row)
        for (int col = 0; col < kMap.cols; ++col) {
            const Hex h = offsetToAxial(col, row);
            if (!inBounds(h, kMap)) ok05 = false;
            int backCol = 0, backRow = 0;              // round-trip is exact
            axialToOffset(h, backCol, backRow);
            if (backCol != col || backRow != row) ok05 = false;
        }
    // The ring just outside the rectangle is out of bounds in every direction.
    for (int row = -1; row <= kMap.rows; ++row) {
        if (inBounds(offsetToAxial(-1, row), kMap)) ok05 = false;
        if (inBounds(offsetToAxial(kMap.cols, row), kMap)) ok05 = false;
    }
    for (int col = -1; col <= kMap.cols; ++col) {
        if (inBounds(offsetToAxial(col, -1), kMap)) ok05 = false;
        if (inBounds(offsetToAxial(col, kMap.rows), kMap)) ok05 = false;
    }
    // A different scenario declares different bounds — nothing is hardcoded.
    const MapBounds small{3, 3};
    if (inBounds(offsetToAxial(5, 5), small)) ok05 = false;
    if (!inBounds(offsetToAxial(2, 2), small)) ok05 = false;
    check("T-HEX-05 inbounds-matches-Q1-dimensions", ok05);

    // --- T-HEX-06 --------------------------------------------------------------
    // ONE distance definition. Combat's range check (verified @ 5ffa8d6) is fed this
    // function's output on real hex pairs: Artillery adjacent to its attacker takes
    // no counter, and does counter across two hexes. No second metric exists to drift.
    const Unit arty{10, 1, 8, 8, 2, 3, UnitType::Artillery};
    const Unit tank{8, 5, 20, 20, 1, 1, UnitType::Tank};
    bool ok06 = true;
    for (const Hex& defender : board) {
        Hex adj[6];
        const int n = neighbors(defender, kMap, adj);
        for (int i = 0; i < n; ++i) {
            const int d = hexDistance(defender, adj[i]);
            if (d != 1) ok06 = false;
            if (defenderCanCounter(arty, d)) ok06 = false;   // T-COMBAT-07, via hex math
            if (!defenderCanCounter(tank, d)) ok06 = false;  // T-COMBAT-06, via hex math
        }
    }
    // Two hexes out: inside Artillery's band, so the counter is live.
    {
        const Hex a = offsetToAxial(2, 2);
        const Hex b = offsetToAxial(4, 2);
        if (hexDistance(a, b) != 2) ok06 = false;
        if (!defenderCanCounter(arty, hexDistance(a, b))) ok06 = false;
    }
    check("T-HEX-06 combat-consumes-this-distance", ok06);

    // --- T-HEX-07 --------------------------------------------------------------
    // Canonical order is total and the sort is reproducible. Feed the sorter a
    // deliberately scrambled copy and require the canonical enumeration back.
    bool ok07 = true;
    {
        // Strict weak ordering: irreflexive, asymmetric, transitive.
        for (const Hex& a : board) {
            if (hexLess(a, a)) ok07 = false;
            for (const Hex& b : board) {
                if (hexLess(a, b) && hexLess(b, a)) ok07 = false;
                if (!hexEqual(a, b) && !hexLess(a, b) && !hexLess(b, a)) ok07 = false;  // total
            }
        }
        for (std::size_t i = 0; i < board.size(); i += 7)
            for (std::size_t j = 0; j < board.size(); j += 5)
                for (std::size_t k = 0; k < board.size(); k += 3)
                    if (hexLess(board[i], board[j]) && hexLess(board[j], board[k]) &&
                        !hexLess(board[i], board[k])) ok07 = false;

        std::vector<Hex> scrambled(board.rbegin(), board.rend());
        sortCanonical(scrambled);
        if (scrambled.size() != board.size()) ok07 = false;
        else for (std::size_t i = 0; i < board.size(); ++i)
            if (!hexEqual(scrambled[i], board[i])) ok07 = false;

        // A second, differently-scrambled copy lands on the same sequence, and
        // sorting an already-sorted set is a fixed point.
        std::vector<Hex> other;
        for (std::size_t step = 0; step < 7; ++step)
            for (std::size_t i = step; i < board.size(); i += 7) other.push_back(board[i]);
        sortCanonical(other);
        if (other.size() != board.size()) ok07 = false;
        else for (std::size_t i = 0; i < board.size(); ++i)
            if (!hexEqual(other[i], board[i])) ok07 = false;
        sortCanonical(other);
        for (std::size_t i = 0; i < other.size() && i < board.size(); ++i)
            if (!hexEqual(other[i], board[i])) ok07 = false;

        // neighbors() returns the same sequence on every call.
        for (const Hex& h : board) {
            Hex a[6], b[6];
            const int na = neighbors(h, kMap, a);
            const int nb = neighbors(h, kMap, b);
            if (na != nb) ok07 = false;
            for (int i = 0; i < na; ++i) if (!hexEqual(a[i], b[i])) ok07 = false;
        }
    }
    check("T-HEX-07 canonical-order-and-determinism", ok07);

    std::printf("\n%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
