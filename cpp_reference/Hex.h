// Stratocracy — headless hex grid & math (GDD §4.7 Stub 1, §4.11 row 1).
// Zero engine dependencies. Pure integer math, no state, no floating point.
#pragma once

#include <vector>

namespace strat {

// Axial coordinate, pointy-top (§2.2). The module stores ONLY axial; odd-r offset
// (col, row) exists solely as the argument/result of the two conversions below.
struct Hex {
    int q = 0;
    int r = 0;
};

// The odd-r offset rectangle a scenario declares (Q1: bounds are per-scenario data,
// never a global constant). Ferrum Crossing ships 11 x 9 (§2.13.2).
struct MapBounds {
    int cols = 0;
    int rows = 0;
};

bool hexEqual(const Hex& a, const Hex& b);

// Canonical hex order (§4.7 shared conventions): ascending r, then ascending q.
// A strict weak ordering, and the single total order used anywhere enumeration could
// leak into behavior or bytes.
bool hexLess(const Hex& a, const Hex& b);

// Standard axial hex metric: (|dq| + |dr| + |dq + dr|) / 2. Pure integer.
int hexDistance(const Hex& a, const Hex& b);

bool inBounds(const Hex& h, const MapBounds& b);

// The six directions, in the FIXED documented enumeration order (hex_spec.md):
//   0 E (+1,0)  1 NE (+1,-1)  2 NW (0,-1)  3 W (-1,0)  4 SW (-1,+1)  5 SE (0,+1)
constexpr int HEX_DIRECTIONS = 6;

// Applies the direction offset and nothing else — NOT bounds-checked, so the six
// candidates always exist and T-HEX-01 can assert filtering removes only
// out-of-bounds hexes. `dir` outside 0..5 returns `h` unchanged.
Hex neighborCandidate(const Hex& h, int dir);

// The in-bounds neighbors, written into `out` in the fixed order with the survivors'
// relative order preserved. Returns how many were written (0..6).
int neighbors(const Hex& h, const MapBounds& b, Hex out[HEX_DIRECTIONS]);

// odd-r offset <-> axial (§4.7): q = col - (row - (row & 1)) / 2, r = row.
Hex  offsetToAxial(int col, int row);
void axialToOffset(const Hex& h, int& col, int& row);

// Sorts in place into canonical order. Deterministic and platform-independent.
void sortCanonical(std::vector<Hex>& hexes);

} // namespace strat
