// Stratocracy — movement & pathfinding implementation (GDD §4.7 Stub 3).
// Dijkstra over terrain cost (§4.1). Cost is charged on ENTERING a hex, so the start
// hex costs 0. All tie-breaks are canonical: among equal-cost paths the search keeps
// the lexicographically smallest under canonical hex order, read start -> goal.
#include "Move.h"

#include <algorithm>
#include <queue>

namespace strat {

int Board::index(const Hex& h) const {
    if (!inBounds(h, bounds)) return -1;
    int col = 0, row = 0;
    axialToOffset(h, col, row);
    return row * bounds.cols + col;
}

bool Board::valid() const {
    if (bounds.cols <= 0 || bounds.rows <= 0) return false;
    const std::size_t n = static_cast<std::size_t>(bounds.cols) * static_cast<std::size_t>(bounds.rows);
    return terrain.size() == n && occupant.size() == n;
}

int Board::terrainAt(const Hex& h) const {
    const int i = index(h);
    return (i < 0 || static_cast<std::size_t>(i) >= terrain.size()) ? -1 : terrain[i];
}

int Board::occupantAt(const Hex& h) const {
    const int i = index(h);
    return (i < 0 || static_cast<std::size_t>(i) >= occupant.size()) ? OCCUPANT_NONE : occupant[i];
}

namespace {

// Lexicographic comparison of two hex paths under canonical hex order. A proper
// prefix sorts first. This is a total order over distinct paths, which is what makes
// the queue's pop order — and therefore the chosen route — reproducible.
bool pathLess(const std::vector<Hex>& a, const std::vector<Hex>& b) {
    const std::size_t n = (a.size() < b.size()) ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (hexLess(a[i], b[i])) return true;
        if (hexLess(b[i], a[i])) return false;
    }
    return a.size() < b.size();
}

struct State {
    int cost = 0;
    std::vector<Hex> path;   // start -> this hex, inclusive
};

// std::priority_queue is a max-heap, so "greater" puts the smallest on top.
struct StateGreater {
    bool operator()(const State& a, const State& b) const {
        if (a.cost != b.cost) return a.cost > b.cost;
        return pathLess(b.path, a.path);
    }
};

struct Settled {
    bool settled = false;
    int  cost = 0;
    std::vector<Hex> path;
};

// One Dijkstra pass, bounded by `move`. Every settled entry holds the minimal cost
// and, among minimal-cost routes, the lexicographically smallest path.
//
// Correctness of the tie-break: every passable hex costs >= 1 to enter, so cost is
// strictly increasing along a path and every proper prefix of a minimal-cost path
// settles strictly before its extension is popped. The lex-smallest minimal path to
// a hex is therefore always in the queue by the time that hex is popped.
std::vector<Settled> search(const Board& board, const std::vector<TerrainDef>& terrain,
                            const Hex& start, int move) {
    std::vector<Settled> done;
    if (!board.valid()) return done;
    const int startIdx = board.index(start);
    if (startIdx < 0) return done;      // out of bounds is never trusted (T-HEX-05)
    done.resize(board.terrain.size());

    std::priority_queue<State, std::vector<State>, StateGreater> open;
    State first;
    first.cost = 0;
    first.path.push_back(start);
    open.push(first);

    while (!open.empty()) {
        const State cur = open.top();
        open.pop();
        const Hex here = cur.path.back();
        const int hereIdx = board.index(here);
        if (hereIdx < 0) continue;
        if (done[hereIdx].settled) continue;
        done[hereIdx].settled = true;
        done[hereIdx].cost    = cur.cost;
        done[hereIdx].path    = cur.path;

        Hex adj[HEX_DIRECTIONS];
        const int count = neighbors(here, board.bounds, adj);
        for (int i = 0; i < count; ++i) {
            const int idx = board.index(adj[i]);
            if (idx < 0 || done[idx].settled) continue;
            // Q3, conservative reading in force: any other unit's hex blocks pathing
            // entirely — it is neither entered nor crossed. The start hex is never
            // re-entered, so a unit is never blocked by itself.
            if (board.occupant[idx] != OCCUPANT_NONE) continue;
            const int t = board.terrain[idx];
            if (t < 0 || static_cast<std::size_t>(t) >= terrain.size()) continue;
            const int step = terrain[t].moveCost;
            if (step <= 0) continue;                 // 0 == impassable (§4.8 sentinel)
            const int next = cur.cost + step;
            if (next > move) continue;
            State s;
            s.cost = next;
            s.path = cur.path;
            s.path.push_back(adj[i]);
            open.push(s);
        }
    }
    return done;
}

} // namespace

std::vector<ReachEntry> reachable(const Board& board, const std::vector<TerrainDef>& terrain,
                                  const Hex& start, int move) {
    std::vector<ReachEntry> out;
    const std::vector<Settled> done = search(board, terrain, start, move);
    for (int row = 0; row < board.bounds.rows; ++row) {
        for (int col = 0; col < board.bounds.cols; ++col) {
            const std::size_t idx = static_cast<std::size_t>(row) * board.bounds.cols + col;
            if (idx >= done.size() || !done[idx].settled) continue;
            ReachEntry e;
            e.hex  = offsetToAxial(col, row);
            e.cost = done[idx].cost;
            out.push_back(e);
        }
    }
    // Canonical order (r asc, then q asc). Row-major offset enumeration already
    // yields it — r == row, and within a row q is col minus a constant — so this
    // sort is a guard, not a fix: it keeps the guarantee true if the enumeration
    // above is ever reordered. T-HEX-07 asserts the order itself.
    std::stable_sort(out.begin(), out.end(),
                     [](const ReachEntry& a, const ReachEntry& b) { return hexLess(a.hex, b.hex); });
    return out;
}

bool findPath(const Board& board, const std::vector<TerrainDef>& terrain,
              const Hex& start, const Hex& goal, int move,
              std::vector<Hex>& outPath, int& outCost) {
    const std::vector<Settled> done = search(board, terrain, start, move);
    const int idx = board.index(goal);
    if (idx < 0 || static_cast<std::size_t>(idx) >= done.size() || !done[idx].settled)
        return false;
    outPath = done[idx].path;
    outCost = done[idx].cost;
    return true;
}

} // namespace strat
