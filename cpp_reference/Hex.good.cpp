// Stratocracy — hex grid & math implementation (GDD §4.7 Stub 1).
// Pure integer math; no state, no I/O, no floating point.
#include "Hex.h"

#include <algorithm>
#include <cstdlib>

namespace strat {

// The fixed enumeration order, documented in spec/hex_spec.md and now contract:
//   0 E   1 NE   2 NW   3 W   4 SW   5 SE
static const int kDirQ[HEX_DIRECTIONS] = { +1, +1,  0, -1, -1,  0 };
static const int kDirR[HEX_DIRECTIONS] = {  0, -1, -1,  0, +1, +1 };

bool hexEqual(const Hex& a, const Hex& b) {
    return a.q == b.q && a.r == b.r;
}

bool hexLess(const Hex& a, const Hex& b) {
    if (a.r != b.r) return a.r < b.r;   // ascending r first
    return a.q < b.q;                   // then ascending q
}

int hexDistance(const Hex& a, const Hex& b) {
    const int dq = a.q - b.q;
    const int dr = a.r - b.r;
    return (std::abs(dq) + std::abs(dr) + std::abs(dq + dr)) / 2;
}

Hex offsetToAxial(int col, int row) {
    Hex h;
    h.q = col - (row - (row & 1)) / 2;   // row - (row & 1) is always even: exact
    h.r = row;
    return h;
}

void axialToOffset(const Hex& h, int& col, int& row) {
    row = h.r;
    col = h.q + (h.r - (h.r & 1)) / 2;
}

bool inBounds(const Hex& h, const MapBounds& b) {
    int col = 0, row = 0;
    axialToOffset(h, col, row);
    return col >= 0 && col < b.cols && row >= 0 && row < b.rows;
}

Hex neighborCandidate(const Hex& h, int dir) {
    if (dir < 0 || dir >= HEX_DIRECTIONS) return h;
    Hex n;
    n.q = h.q + kDirQ[dir];
    n.r = h.r + kDirR[dir];
    return n;
}

int neighbors(const Hex& h, const MapBounds& b, Hex out[HEX_DIRECTIONS]) {
    int count = 0;
    for (int dir = 0; dir < HEX_DIRECTIONS; ++dir) {
        const Hex n = neighborCandidate(h, dir);
        if (inBounds(n, b)) out[count++] = n;   // survivors keep their relative order
    }
    return count;
}

void sortCanonical(std::vector<Hex>& hexes) {
    std::stable_sort(hexes.begin(), hexes.end(), hexLess);
}

} // namespace strat
