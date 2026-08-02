// Stratocracy — debug-command driver implementation (§4.4 week 1).
//
// CONTAINS NO RULES. Every rule decision below is a call into Hex.h, Data.h,
// Move.h or Combat.h. Where a question is not answerable by one of those four --
// ownership, whose turn it is, what a scenario file looks like -- the command is
// refused rather than decided, because rows 4-8 hold no code (spec/driver_spec.md).
#include "Driver.h"

#include <algorithm>
#include <sstream>

namespace strat {

namespace {

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string tok;
    while (in >> tok) out.push_back(tok);
    return out;
}

bool parseInt(const std::string& s, int& out) {
    if (s.empty()) return false;
    std::size_t i = (s[0] == '-') ? 1 : 0;
    if (i >= s.size()) return false;
    for (std::size_t k = i; k < s.size(); ++k)
        if (s[k] < '0' || s[k] > '9') return false;
    out = std::atoi(s.c_str());
    return true;
}

std::string num(int v) { return std::to_string(v); }

// One character per terrain, for `map`. Driven off the loaded row Id, so a terrain
// the table does not define renders as '?' rather than as a guess.
char glyphFor(const std::string& id) {
    if (id == "Plains")    return '.';
    if (id == "Woods")     return 'F';
    if (id == "Mountains") return 'M';
    if (id == "Water")     return '~';
    if (id == "Town")      return 'T';
    if (id == "Bridge")    return 'B';
    if (id == "Factory")   return 'X';
    return '?';
}

} // namespace

const DriverUnit* findUnitById(const Session& s, int id) {
    for (const DriverUnit& u : s.units) if (u.id == id) return &u;
    return nullptr;
}

static DriverUnit* mutableUnitById(Session& s, int id) {
    for (DriverUnit& u : s.units) if (u.id == id) return &u;
    return nullptr;
}

// ---------------------------------------------------------------------------
// delegation helpers -- the only places the driver touches module state
// ---------------------------------------------------------------------------
static Board buildBoard(const Session& s) {
    Board b;
    b.bounds  = s.bounds;
    b.terrain = s.terrain;
    b.occupant.assign(s.terrain.size(), OCCUPANT_NONE);
    for (const DriverUnit& u : s.units) {
        const int i = b.index(u.hex);
        if (i >= 0) b.occupant[i] = u.id;
    }
    return b;
}

// A Combat.h participant built from the loaded UnitDef plus current HP. The driver
// stores no stat of its own; every field is looked up.
static Unit combatUnit(const Session& s, const DriverUnit& u) {
    const UnitDef& d = s.unitDefs[u.defIndex];
    Unit c;
    c.atk = d.atk; c.def = d.def; c.hp = u.hp; c.hpMax = d.hpMax;
    c.rangeMin = d.rangeMin; c.rangeMax = d.rangeMax; c.type = d.type;
    return c;
}

static int terrainDefPctAt(const Session& s, const Hex& h) {
    Board b; b.bounds = s.bounds; b.terrain = s.terrain;
    b.occupant.assign(s.terrain.size(), OCCUPANT_NONE);
    const int t = b.terrainAt(h);
    if (t < 0 || static_cast<std::size_t>(t) >= s.terrainDefs.size()) return 0;
    return s.terrainDefs[t].defensePct;
}

static bool enemyAdjacent(const Session& s, const DriverUnit& u) {
    Hex adj[HEX_DIRECTIONS];
    const int n = neighbors(u.hex, s.bounds, adj);
    for (int i = 0; i < n; ++i)
        for (const DriverUnit& o : s.units)
            if (o.side != u.side && hexEqual(o.hex, adj[i])) return true;
    return false;
}

// ---------------------------------------------------------------------------
// THE attack computation. `forecast` and `attack` both call this and nothing else,
// so "the forecast is exactly what resolves" (§2.6) is structural here, not merely
// asserted by GATE-DRV-03.
// ---------------------------------------------------------------------------
struct AttackOutcome {
    bool legal = false;
    std::string reason;
    int  distance = 0;
    int  damage = 0;
    bool defenderDies = false;
    bool counterFires = false;
    int  counterDamage = 0;
    bool attackerDies = false;
};

static AttackOutcome computeAttack(const Session& s, int atkId, int defId) {
    AttackOutcome o;
    const DriverUnit* a = findUnitById(s, atkId);
    const DriverUnit* d = findUnitById(s, defId);
    if (a == nullptr) { o.reason = "no unit " + num(atkId); return o; }
    if (d == nullptr) { o.reason = "no unit " + num(defId); return o; }
    if (a->id == d->id) { o.reason = "a unit cannot attack itself"; return o; }
    if (a->side == d->side) { o.reason = "units " + num(atkId) + " and " + num(defId) +
                                         " are on the same side"; return o; }

    o.distance = hexDistance(a->hex, d->hex);                 // Hex.h decides distance
    const UnitDef& ad = s.unitDefs[a->defIndex];
    if (o.distance < ad.rangeMin || o.distance > ad.rangeMax) {
        o.reason = "distance " + num(o.distance) + " is outside " +
                   s.unitDefs[a->defIndex].id + "'s range " +
                   num(ad.rangeMin) + "-" + num(ad.rangeMax);
        return o;
    }

    const Unit au = combatUnit(s, *a);
    const Unit du = combatUnit(s, *d);
    o.damage = resolveDamage(au, du, terrainDefPctAt(s, d->hex));  // Combat.h decides damage
    const int defHpAfter = d->hp - o.damage;
    o.defenderDies = defHpAfter <= 0;

    if (!o.defenderDies) {
        Unit duAfter = du;
        duAfter.hp = defHpAfter;                              // a wounded defender counters weaker
        if (defenderCanCounter(duAfter, o.distance)) {        // Combat.h decides eligibility
            o.counterFires  = true;
            o.counterDamage = resolveDamage(duAfter, au, terrainDefPctAt(s, a->hex));
            o.attackerDies  = (a->hp - o.counterDamage) <= 0;
        }
    }
    o.legal = true;
    return o;
}

// ---------------------------------------------------------------------------
// fixtures -- built in, no file format (Stub 7 is unbuilt)
// ---------------------------------------------------------------------------
namespace {
struct Fixture { const char* name; int cols; int rows; const char* const* glyphs; };

const char* const kRiver[5] = { "...~...", ".F.~...", "...B...", ".M.~...", "...~..." };
const char* const kOpen[3]  = { ".....", ".....", "....." };

const Fixture kFixtures[2] = {
    {"river", 7, 5, kRiver},   // a Water column crossed by one Bridge
    {"open",  5, 3, kOpen},    // flat plains, for range and counter cases
};
} // namespace

std::vector<std::string> fixtureNames() {
    std::vector<std::string> v;
    for (const Fixture& f : kFixtures) v.push_back(f.name);
    return v;
}

static int terrainIndexFor(const Session& s, char glyph) {
    for (std::size_t i = 0; i < s.terrainDefs.size(); ++i)
        if (glyphFor(s.terrainDefs[i].id) == glyph) return static_cast<int>(i);
    return -1;
}

bool loadFixture(Session& s, const std::string& name, std::string& err) {
    for (const Fixture& f : kFixtures) {
        if (name != f.name) continue;
        Session next = s;                       // build aside; commit only on success
        next.bounds.cols = f.cols;
        next.bounds.rows = f.rows;
        next.terrain.assign(static_cast<std::size_t>(f.cols) * f.rows, -1);
        for (int row = 0; row < f.rows; ++row) {
            for (int col = 0; col < f.cols; ++col) {
                const int t = terrainIndexFor(s, f.glyphs[row][col]);
                if (t < 0) { err = "fixture uses a terrain the table does not define"; return false; }
                next.terrain[static_cast<std::size_t>(row) * f.cols + col] = t;
            }
        }
        next.units.clear();
        next.nextUnitId = 1;
        next.loaded = true;
        s = next;
        return true;
    }
    err = "no fixture named '" + name + "'";
    return false;
}

bool sessionInit(Session& s, const std::string& dataDir, std::string& err) {
    double eff[UNIT_TYPE_COUNT][UNIT_TYPE_COUNT];
    if (!loadUnits(dataDir + "/units.csv", s.unitDefs, err)) return false;
    if (!loadTerrain(dataDir + "/terrain.csv", s.terrainDefs, err)) return false;
    if (!loadEffectiveness(dataDir + "/effectiveness.csv", eff, err)) return false;
    return true;
}

std::string stateHash(const Session& s) {
    std::vector<const DriverUnit*> ordered;
    for (const DriverUnit& u : s.units) ordered.push_back(&u);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const DriverUnit* a, const DriverUnit* b) {
                         if (!hexEqual(a->hex, b->hex)) return hexLess(a->hex, b->hex);
                         return a->id < b->id;
                     });
    std::string acc = num(s.bounds.cols) + "x" + num(s.bounds.rows) + "|";
    for (int t : s.terrain) acc += num(t) + ",";
    acc += "|";
    for (const DriverUnit* u : ordered) {
        int col = 0, row = 0;
        axialToOffset(u->hex, col, row);
        acc += num(u->id) + ":" + num(u->side) + ":" + num(u->defIndex) + ":" +
               num(col) + ":" + num(row) + ":" + num(u->hp) + ";";
    }
    unsigned long long h = 1469598103934665603ULL;          // FNV-1a, 64-bit
    for (char c : acc) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ULL; }
    std::string hex;
    for (int i = 15; i >= 0; --i) hex += "0123456789abcdef"[(h >> (i * 4)) & 0xF];
    return hex;
}

// ---------------------------------------------------------------------------
// command execution
// ---------------------------------------------------------------------------
namespace {

void renderMap(const Session& s, std::vector<std::string>& out) {
    Board b = buildBoard(s);
    for (int row = 0; row < s.bounds.rows; ++row) {
        std::string line = (row & 1) ? "  " : "";
        line += num(row) + " ";
        for (int col = 0; col < s.bounds.cols; ++col) {
            const Hex h = offsetToAxial(col, row);
            const int occ = b.occupantAt(h);
            if (occ != OCCUPANT_NONE) {
                const DriverUnit* u = findUnitById(s, occ);
                line += num(u->side) + std::string(1, s.unitDefs[u->defIndex].id[0]) + " ";
            } else {
                line += std::string(1, glyphFor(s.terrainDefs[s.terrain[
                    static_cast<std::size_t>(row) * s.bounds.cols + col]].id)) + "  ";
            }
        }
        out.push_back(line);
    }
    std::string cols = "   ";
    for (int col = 0; col < s.bounds.cols; ++col) cols += num(col) + "  ";
    out.push_back(cols);
}

} // namespace

bool execute(Session& s, const std::string& line, std::vector<std::string>& out) {
    const std::vector<std::string> t = tokenize(line);
    if (t.empty()) return true;
    const std::string& cmd = t[0];

    if (cmd == "quit" || cmd == "exit") { out.push_back("bye"); return false; }

    if (cmd == "help") {
        out.push_back("map | units | fixture <name> | place <side> <Type> <col> <row>");
        out.push_back("remove <id> | hp <id> <v> | dist <c1> <r1> <c2> <r2>");
        out.push_back("reach <id> | path <id> <col> <row> | move <id> <col> <row>");
        out.push_back("forecast <atk> <def> | attack <atk> <def> | repair <id> <owned 0|1>");
        out.push_back("NOTE: no turns, no capture, no Fame, no AI -- rows 4-8 hold no code.");
        return true;
    }

    if (cmd == "fixture") {
        if (t.size() == 1 || t[1] == "list") {
            std::string names;
            for (const std::string& n : fixtureNames()) names += n + " ";
            out.push_back("fixtures: " + names);
            return true;
        }
        std::string err;
        if (!loadFixture(s, t[1], err)) { out.push_back("refused: " + err); return true; }
        out.push_back("loaded fixture '" + t[1] + "' (" + num(s.bounds.cols) + "x" +
                      num(s.bounds.rows) + ")");
        return true;
    }

    if (!s.loaded) { out.push_back("refused: no board -- run 'fixture <name>' first"); return true; }

    if (cmd == "map")  { renderMap(s, out); return true; }

    if (cmd == "units") {
        if (s.units.empty()) { out.push_back("(no units)"); return true; }
        for (const DriverUnit& u : s.units) {
            int col = 0, row = 0;
            axialToOffset(u.hex, col, row);
            const UnitDef& d = s.unitDefs[u.defIndex];
            out.push_back("#" + num(u.id) + " side " + num(u.side) + " " + d.id +
                          " at (" + num(col) + "," + num(row) + ") hp " + num(u.hp) +
                          "/" + num(d.hpMax) + " move " + num(d.move) +
                          " range " + num(d.rangeMin) + "-" + num(d.rangeMax));
        }
        return true;
    }

    if (cmd == "place") {
        int side = 0, col = 0, row = 0;
        if (t.size() != 5 || !parseInt(t[1], side) || !parseInt(t[3], col) || !parseInt(t[4], row)) {
            out.push_back("refused: usage: place <side> <Type> <col> <row>"); return true;
        }
        if (side != 0 && side != 1) { out.push_back("refused: side must be 0 or 1"); return true; }
        int defIndex = -1;
        for (std::size_t i = 0; i < s.unitDefs.size(); ++i)
            if (s.unitDefs[i].id == t[2]) defIndex = static_cast<int>(i);
        if (defIndex < 0) { out.push_back("refused: no unit type '" + t[2] + "'"); return true; }
        const Hex h = offsetToAxial(col, row);
        if (!inBounds(h, s.bounds)) { out.push_back("refused: (" + num(col) + "," + num(row) +
                                                    ") is out of bounds"); return true; }
        Board b = buildBoard(s);
        if (b.occupantAt(h) != OCCUPANT_NONE) { out.push_back("refused: hex is occupied"); return true; }
        const TerrainDef& td = s.terrainDefs[b.terrainAt(h)];
        if (!td.passLand) { out.push_back("refused: " + td.id + " is not passable to land"); return true; }
        DriverUnit u;
        u.id = s.nextUnitId++;
        u.side = side; u.defIndex = defIndex; u.hex = h; u.hp = s.unitDefs[defIndex].hpMax;
        s.units.push_back(u);
        out.push_back("placed #" + num(u.id) + " " + s.unitDefs[defIndex].id +
                      " side " + num(side) + " at (" + num(col) + "," + num(row) + ")");
        return true;
    }

    if (cmd == "remove") {
        int id = 0;
        if (t.size() != 2 || !parseInt(t[1], id)) { out.push_back("refused: usage: remove <id>"); return true; }
        if (findUnitById(s, id) == nullptr) { out.push_back("refused: no unit " + num(id)); return true; }
        s.units.erase(std::remove_if(s.units.begin(), s.units.end(),
                       [id](const DriverUnit& u) { return u.id == id; }), s.units.end());
        out.push_back("removed #" + num(id));
        return true;
    }

    if (cmd == "hp") {
        int id = 0, v = 0;
        if (t.size() != 3 || !parseInt(t[1], id) || !parseInt(t[2], v)) {
            out.push_back("refused: usage: hp <id> <value>"); return true; }
        DriverUnit* u = mutableUnitById(s, id);
        if (u == nullptr) { out.push_back("refused: no unit " + num(id)); return true; }
        const int hpMax = s.unitDefs[u->defIndex].hpMax;
        if (v < 1 || v > hpMax) { out.push_back("refused: hp must be 1.." + num(hpMax)); return true; }
        u->hp = v;
        out.push_back("#" + num(id) + " hp = " + num(v));
        return true;
    }

    if (cmd == "dist") {
        int c1 = 0, r1 = 0, c2 = 0, r2 = 0;
        if (t.size() != 5 || !parseInt(t[1], c1) || !parseInt(t[2], r1) ||
            !parseInt(t[3], c2) || !parseInt(t[4], r2)) {
            out.push_back("refused: usage: dist <c1> <r1> <c2> <r2>"); return true; }
        const Hex a = offsetToAxial(c1, r1), b = offsetToAxial(c2, r2);
        if (!inBounds(a, s.bounds) || !inBounds(b, s.bounds)) {
            out.push_back("refused: a hex is out of bounds"); return true; }
        out.push_back("distance = " + num(hexDistance(a, b)));
        return true;
    }

    if (cmd == "reach") {
        int id = 0;
        if (t.size() != 2 || !parseInt(t[1], id)) { out.push_back("refused: usage: reach <id>"); return true; }
        const DriverUnit* u = findUnitById(s, id);
        if (u == nullptr) { out.push_back("refused: no unit " + num(id)); return true; }
        const Board b = buildBoard(s);
        const std::vector<ReachEntry> set =
            reachable(b, s.terrainDefs, u->hex, s.unitDefs[u->defIndex].move);
        out.push_back("reachable (" + num(static_cast<int>(set.size())) + " hexes, cost <= " +
                      num(s.unitDefs[u->defIndex].move) + "):");
        for (const ReachEntry& e : set) {
            int col = 0, row = 0;
            axialToOffset(e.hex, col, row);
            out.push_back("  (" + num(col) + "," + num(row) + ") cost " + num(e.cost));
        }
        return true;
    }

    if (cmd == "path" || cmd == "move") {
        int id = 0, col = 0, row = 0;
        if (t.size() != 4 || !parseInt(t[1], id) || !parseInt(t[2], col) || !parseInt(t[3], row)) {
            out.push_back("refused: usage: " + cmd + " <id> <col> <row>"); return true; }
        const DriverUnit* u = findUnitById(s, id);
        if (u == nullptr) { out.push_back("refused: no unit " + num(id)); return true; }
        const Hex goal = offsetToAxial(col, row);
        if (!inBounds(goal, s.bounds)) { out.push_back("refused: (" + num(col) + "," + num(row) +
                                                       ") is out of bounds"); return true; }
        const Board b = buildBoard(s);
        std::vector<Hex> route;
        int cost = 0;
        if (!findPath(b, s.terrainDefs, u->hex, goal, s.unitDefs[u->defIndex].move, route, cost)) {
            out.push_back("refused: (" + num(col) + "," + num(row) +
                          ") is not reachable within " + num(s.unitDefs[u->defIndex].move) +
                          " movement");
            return true;
        }
        std::string desc;
        for (const Hex& h : route) {
            int c = 0, r = 0;
            axialToOffset(h, c, r);
            desc += "(" + num(c) + "," + num(r) + ")";
        }
        if (cmd == "path") { out.push_back("cost " + num(cost) + ": " + desc); return true; }
        mutableUnitById(s, id)->hex = goal;
        out.push_back("#" + num(id) + " moved, cost " + num(cost) + ": " + desc);
        return true;
    }

    if (cmd == "forecast" || cmd == "attack") {
        int a = 0, d = 0;
        if (t.size() != 3 || !parseInt(t[1], a) || !parseInt(t[2], d)) {
            out.push_back("refused: usage: " + cmd + " <attackerId> <defenderId>"); return true; }
        const AttackOutcome o = computeAttack(s, a, d);
        if (!o.legal) { out.push_back("refused: " + o.reason); return true; }
        const std::string atkName = s.unitDefs[findUnitById(s, a)->defIndex].id;
        const std::string defName = s.unitDefs[findUnitById(s, d)->defIndex].id;
        if (cmd == "forecast") {
            out.push_back("at distance " + num(o.distance) + ": " + atkName + " deals " +
                          num(o.damage) + (o.defenderDies ? " (kills " + defName + ")" : ""));
            out.push_back(o.counterFires
                ? "  counter: " + defName + " returns " + num(o.counterDamage) +
                  (o.attackerDies ? " (kills " + atkName + ")" : "")
                : std::string("  counter: none"));
            return true;
        }
        // Resolution applies exactly what the forecast above reported (GATE-DRV-03).
        out.push_back(atkName + " hits " + defName + " for " + num(o.damage));
        if (o.defenderDies) {
            s.units.erase(std::remove_if(s.units.begin(), s.units.end(),
                           [d](const DriverUnit& u) { return u.id == d; }), s.units.end());
            out.push_back(defName + " #" + num(d) + " destroyed");
            return true;
        }
        mutableUnitById(s, d)->hp -= o.damage;
        if (o.counterFires) {
            out.push_back(defName + " counters for " + num(o.counterDamage));
            if (o.attackerDies) {
                s.units.erase(std::remove_if(s.units.begin(), s.units.end(),
                               [a](const DriverUnit& u) { return u.id == a; }), s.units.end());
                out.push_back(atkName + " #" + num(a) + " destroyed");
            } else {
                mutableUnitById(s, a)->hp -= o.counterDamage;
            }
        }
        return true;
    }

    if (cmd == "repair") {
        int id = 0, owned = 0;
        if (t.size() != 3 || !parseInt(t[1], id) || !parseInt(t[2], owned)) {
            out.push_back("refused: usage: repair <id> <owned 0|1>"); return true; }
        if (owned != 0 && owned != 1) { out.push_back("refused: owned must be 0 or 1"); return true; }
        DriverUnit* u = mutableUnitById(s, id);
        if (u == nullptr) { out.push_back("refused: no unit " + num(id)); return true; }
        const bool adj = enemyAdjacent(s, *u);
        const int amount = repairAmount(combatUnit(s, *u), owned == 1, adj);  // Combat.h decides
        u->hp += amount;
        out.push_back("#" + num(id) + " repaired " + num(amount) + " (owned=" + num(owned) +
                      ", enemyAdjacent=" + (adj ? "1" : "0") + ") -> hp " + num(u->hp));
        return true;
    }

    out.push_back("refused: unknown command '" + cmd + "' -- try 'help'");
    return true;
}

} // namespace strat
