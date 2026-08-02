// Stratocracy — headless movement & pathfinding (GDD §4.7 Stub 3, §4.11 row 3).
// Zero engine dependencies. Depends on rows 1 (Hex) and 2 (Data); defines no cost of
// its own. Cost is charged on ENTERING a hex, so the start hex costs 0.
#pragma once

#include <vector>

#include "Data.h"
#include "Hex.h"

namespace strat {

constexpr int OCCUPANT_NONE = -1;

// The board a move is computed against. Both vectors are offset-indexed
// (row * bounds.cols + col) and must be exactly bounds.cols * bounds.rows long.
struct Board {
    MapBounds bounds;
    std::vector<int> terrain;   // index into the loaded TerrainDef table
    std::vector<int> occupant;  // unit id, or OCCUPANT_NONE

    int  index(const Hex& h) const;          // -1 if out of bounds
    int  terrainAt(const Hex& h) const;      // -1 if out of bounds
    int  occupantAt(const Hex& h) const;     // OCCUPANT_NONE if out of bounds or empty
    bool valid() const;                      // vectors sized to the bounds
};

struct ReachEntry {
    Hex hex;
    int cost = 0;
};

// Every hex whose cheapest path cost is <= move, INCLUDING the start hex at cost 0
// (the null move — a unit is not blocked by itself). Returned in canonical hex order.
//
// Q3 is open, so the conservative reading is in force: a hex occupied by any other
// unit blocks pathing entirely, friendly or not, and so is neither entered nor
// crossed. Impassable terrain (moveCost == 0) is likewise never entered.
std::vector<ReachEntry> reachable(const Board& board,
                                  const std::vector<TerrainDef>& terrain,
                                  const Hex& start, int move);

// The cheapest route from `start` to `goal` within `move`. Among equal-cost paths the
// one chosen is the LEXICOGRAPHICALLY SMALLEST under canonical hex order, read
// start -> goal (move_spec.md). `outPath` includes both endpoints. Returns false and
// leaves the outputs untouched if the goal is not reachable.
bool findPath(const Board& board,
              const std::vector<TerrainDef>& terrain,
              const Hex& start, const Hex& goal, int move,
              std::vector<Hex>& outPath, int& outCost);

} // namespace strat
