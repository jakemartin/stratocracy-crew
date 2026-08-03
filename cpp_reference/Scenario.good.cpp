// Stratocracy — scenario file & validator implementation (GDD §4.7 Stub 7).
//
// Two halves, deliberately separable:
//   parseScenario     the file format. Strict JSON, written here -- no third-party
//                     library is vendored -- and it REFUSES malformed input.
//   validateScenario  the invariants. Runs on a Scenario, not on a path, so one
//                     field of a loaded scenario can be mutated and re-checked.
//
// Serialization order is NOT validation order: `symmetry` serializes last and is
// validated after the map, ownership and placements it quantifies over; the guided
// lanes serialize before it and are priced last because they cost a Stub-3 path.
#include "Scenario.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace strat {

namespace {

// The path budget for a MEASUREMENT. T-SCN-08 measures before T-SCN-06 compares, so
// the search must not be bounded by the ceiling it is about to be judged against --
// fixture (c) needs the 7 printed, not "unreachable".
constexpr int kUnbounded = 1 << 20;

// Guards a hand-written recursive-descent parser against an adversarial file. A
// scenario nests three deep; 32 is slack, not a limit anyone authors into.
constexpr int kMaxDepth = 32;

std::string num(int v) { return std::to_string(v); }

// ---------------------------------------------------------------------------
// A strict JSON subset. Everything the schema needs and nothing it does not:
// objects, arrays, strings, INTEGERS, and the two booleans. No floats, no
// exponents, no null, no \u escape, no trailing comma, no duplicate key, no
// trailing content. Each of those is a refusal with a reason, never a tolerance.
// ---------------------------------------------------------------------------
struct Json {
    enum class Kind { Object, Array, String, Int, Bool };
    Kind kind = Kind::Object;
    std::string str;
    long long   num = 0;
    bool        b   = false;
    std::vector<std::string> keys;   // Object: key order as authored
    std::vector<Json>        vals;   // Object values, or Array items
};

struct JsonParser {
    const std::string& src;
    std::size_t i = 0;
    std::string err;

    explicit JsonParser(const std::string& s) : src(s) {}

    bool fail(const std::string& what) {
        if (err.empty()) err = what + " at byte " + std::to_string(i);
        return false;
    }
    void skipWs() {
        while (i < src.size()) {
            const char c = src[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i;
            else break;
        }
    }
    bool parse(Json& out, int depth) {
        if (depth > kMaxDepth) return fail("nesting deeper than " + num(kMaxDepth));
        skipWs();
        if (i >= src.size()) return fail("unexpected end of input");
        const char c = src[i];
        if (c == '{') return parseObject(out, depth);
        if (c == '[') return parseArray(out, depth);
        if (c == '"') { out.kind = Json::Kind::String; return parseString(out.str); }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
        if (src.compare(i, 4, "true") == 0)  { i += 4; out.kind = Json::Kind::Bool; out.b = true;  return true; }
        if (src.compare(i, 5, "false") == 0) { i += 5; out.kind = Json::Kind::Bool; out.b = false; return true; }
        if (src.compare(i, 4, "null") == 0)
            return fail("null is not a value this schema accepts");
        return fail("unexpected character");
    }
    bool parseObject(Json& out, int depth) {
        out.kind = Json::Kind::Object;
        ++i;                                   // '{'
        skipWs();
        if (i < src.size() && src[i] == '}') { ++i; return true; }
        for (;;) {
            skipWs();
            if (i >= src.size() || src[i] != '"') return fail("expected a key string");
            std::string key;
            if (!parseString(key)) return false;
            for (const std::string& k : out.keys)
                if (k == key) return fail("duplicate key '" + key + "'");
            skipWs();
            if (i >= src.size() || src[i] != ':') return fail("expected ':'");
            ++i;
            Json v;
            if (!parse(v, depth + 1)) return false;
            out.keys.push_back(key);
            out.vals.push_back(v);
            skipWs();
            if (i < src.size() && src[i] == ',') { ++i; skipWs();
                if (i < src.size() && src[i] == '}') return fail("trailing comma");
                continue; }
            if (i < src.size() && src[i] == '}') { ++i; return true; }
            return fail("expected ',' or '}'");
        }
    }
    bool parseArray(Json& out, int depth) {
        out.kind = Json::Kind::Array;
        ++i;                                   // '['
        skipWs();
        if (i < src.size() && src[i] == ']') { ++i; return true; }
        for (;;) {
            Json v;
            if (!parse(v, depth + 1)) return false;
            out.vals.push_back(v);
            skipWs();
            if (i < src.size() && src[i] == ',') { ++i; skipWs();
                if (i < src.size() && src[i] == ']') return fail("trailing comma");
                continue; }
            if (i < src.size() && src[i] == ']') { ++i; return true; }
            return fail("expected ',' or ']'");
        }
    }
    bool parseString(std::string& out) {
        ++i;                                   // '"'
        out.clear();
        while (i < src.size()) {
            const unsigned char c = static_cast<unsigned char>(src[i]);
            if (c == '"') { ++i; return true; }
            if (c < 0x20) return fail("raw control character in a string");
            if (c == '\\') {
                ++i;
                if (i >= src.size()) return fail("unterminated escape");
                const char e = src[i];
                switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u':  return fail("\\u escapes are not accepted (Ids are ASCII)");
                    default:   return fail("unknown escape");
                }
                ++i;
                continue;
            }
            out += static_cast<char>(c);
            ++i;
        }
        return fail("unterminated string");
    }
    bool parseNumber(Json& out) {
        const std::size_t start = i;
        if (src[i] == '-') ++i;
        if (i >= src.size() || src[i] < '0' || src[i] > '9') return fail("malformed number");
        if (src[i] == '0' && i + 1 < src.size() && src[i + 1] >= '0' && src[i + 1] <= '9')
            return fail("leading zero");
        while (i < src.size() && src[i] >= '0' && src[i] <= '9') ++i;
        if (i < src.size() && (src[i] == '.' || src[i] == 'e' || src[i] == 'E'))
            return fail("this schema holds no non-integer number");
        const std::string text = src.substr(start, i - start);
        if (text.size() > 18) return fail("integer out of range");
        out.kind = Json::Kind::Int;
        out.num  = std::strtoll(text.c_str(), nullptr, 10);
        return true;
    }
};

const Json* member(const Json& obj, const std::string& key) {
    for (std::size_t k = 0; k < obj.keys.size(); ++k)
        if (obj.keys[k] == key) return &obj.vals[k];
    return nullptr;
}

// Every key an object is allowed to carry. An unknown key is a REFUSAL, not an
// ignored extra: within one formatVersion an unrecognized key is a typo, and
// tolerating it is how a scenario silently loses a field it thought it declared.
bool onlyKeys(const Json& obj, const std::vector<std::string>& allowed,
              const std::string& where, std::string& err) {
    for (const std::string& k : obj.keys) {
        bool found = false;
        for (const std::string& a : allowed) if (a == k) { found = true; break; }
        if (!found) { err = where + ": unknown field '" + k + "'"; return false; }
    }
    for (const std::string& a : allowed)
        if (member(obj, a) == nullptr) { err = where + ": missing field '" + a + "'"; return false; }
    return true;
}

bool wantInt(const Json& obj, const std::string& key, const std::string& where,
             int& out, std::string& err) {
    const Json* v = member(obj, key);
    if (v == nullptr || v->kind != Json::Kind::Int) {
        err = where + ": '" + key + "' must be an integer"; return false; }
    if (v->num < -2147483647LL || v->num > 2147483647LL) {
        err = where + ": '" + key + "' is out of range"; return false; }
    out = static_cast<int>(v->num);
    return true;
}

bool wantString(const Json& obj, const std::string& key, const std::string& where,
                std::string& out, std::string& err) {
    const Json* v = member(obj, key);
    if (v == nullptr || v->kind != Json::Kind::String) {
        err = where + ": '" + key + "' must be a string"; return false; }
    out = v->str;
    return true;
}

bool wantBool(const Json& obj, const std::string& key, const std::string& where,
              bool& out, std::string& err) {
    const Json* v = member(obj, key);
    if (v == nullptr || v->kind != Json::Kind::Bool) {
        err = where + ": '" + key + "' must be true or false"; return false; }
    out = v->b;
    return true;
}

// A hex reference in the AUTHORED frame: [col, row], odd-r. Converted here and
// nowhere else, so no loaded state ever holds (col, row) (T-SCN-05).
bool wantHex(const Json& obj, const std::string& key, const std::string& where,
             Hex& out, std::string& err) {
    const Json* v = member(obj, key);
    if (v == nullptr || v->kind != Json::Kind::Array || v->vals.size() != 2 ||
        v->vals[0].kind != Json::Kind::Int || v->vals[1].kind != Json::Kind::Int) {
        err = where + ": '" + key + "' must be [col, row]"; return false; }
    const long long c = v->vals[0].num, r = v->vals[1].num;
    if (c < 0 || c > 4095 || r < 0 || r > 4095) {
        err = where + ": '" + key + "' is outside the authorable grid"; return false; }
    out = offsetToAxial(static_cast<int>(c), static_cast<int>(r));
    return true;
}

// ---------------------------------------------------------------------------
// the canonical preimage
// ---------------------------------------------------------------------------
std::string hexKey(const Hex& h) { return num(h.q) + "," + num(h.r); }

bool laneLessByHex(const ScenarioOwner& a, const ScenarioOwner& b) { return hexLess(a.hex, b.hex); }
bool placeLessByHex(const ScenarioPlacement& a, const ScenarioPlacement& b) { return hexLess(a.hex, b.hex); }

// FNV-1a, 64-bit. Fixed-width unsigned arithmetic is wrap-around by definition, so
// the digest is identical on every compiler and every platform -- which is the whole
// of "scenarioHash is platform-stable by canonical ordering".
std::string fnv1a64(const std::string& s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (char c : s) {
        h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        h *= 1099511628211ULL;
    }
    static const char* kHex = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) { out[static_cast<std::size_t>(i)] = kHex[h & 0xF]; h >>= 4; }
    return out;
}

} // namespace

const char* symmetryName(Symmetry s) { return (s == Symmetry::Rot180) ? "rot180" : "none"; }

std::string hexLabel(const Hex& h) {
    int c = 0, r = 0;
    axialToOffset(h, c, r);
    return "(" + num(c) + "," + num(r) + ")";
}

int captureRowIndex(const std::vector<UnitDef>& units) {
    int found = -1;
    for (std::size_t i = 0; i < units.size(); ++i) {
        if (!units[i].canCapture) continue;
        if (found >= 0) return -1;                 // more than one: T-DATA-03's job
        found = static_cast<int>(i);
    }
    return found;
}

std::string scenarioHash(const Scenario& s) {
    // Fields in §4.7 Stub 7's order, hexes in canonical hex order, `scenarioHash`
    // itself excluded. NEW FIELDS APPEND AT THE TAIL of this builder.
    std::ostringstream p;
    p << "v=" << s.formatVersion << ';';
    p << "id=" << s.scenarioId << ';';
    p << "map=" << s.bounds.cols << 'x' << s.bounds.rows << ':';
    for (std::size_t i = 0; i < s.terrainId.size(); ++i) {
        if (i) p << ',';
        p << s.terrainId[i];
    }
    p << ';';

    std::vector<ScenarioOwner> own = s.ownership;
    std::stable_sort(own.begin(), own.end(), laneLessByHex);
    p << "own=";
    for (const ScenarioOwner& o : own) p << hexKey(o.hex) << '=' << o.owner << '|';
    p << ';';

    std::vector<ScenarioPlacement> pl = s.placements;
    std::stable_sort(pl.begin(), pl.end(), placeLessByHex);
    p << "plc=";
    for (const ScenarioPlacement& u : pl)
        p << hexKey(u.hex) << '=' << u.side << ',' << u.unitId << ',' << (u.isFlag ? 1 : 0) << '|';
    p << ';';

    p << "fame=" << s.startingFame[0] << ',' << s.startingFame[1] << ';';
    p << "cap=" << s.turnCap << ';';

    // Entries serialize in the module's SIDE ENUMERATION order, not authoring order,
    // so the hash is content-only.
    p << "guided=";
    for (int side = 0; side < SIDE_COUNT; ++side)
        for (const ScenarioGuided& g : s.guided)
            if (g.side == side)
                p << side << ':' << hexKey(g.infantry) << ">" << hexKey(g.objective) << '|';
    p << ';';

    p << "sym=" << symmetryName(s.symmetry) << ';';
    return fnv1a64(p.str());
}

bool scenarioBoard(const Scenario& s, const std::vector<TerrainDef>& terrain, Board& out) {
    out.bounds = s.bounds;
    const std::size_t n = static_cast<std::size_t>(s.bounds.cols) *
                          static_cast<std::size_t>(s.bounds.rows);
    if (s.terrainId.size() != n) return false;
    out.terrain.assign(n, -1);
    // Occupancy EMPTY: Q21 ruled every priced route is terrain alone. It is also what
    // makes T-SCN-11's set minimum independent of how many units a seat deploys.
    out.occupant.assign(n, OCCUPANT_NONE);
    for (std::size_t i = 0; i < n; ++i) {
        const TerrainDef* d = findTerrain(terrain, s.terrainId[i]);
        if (d == nullptr) return false;
        out.terrain[i] = static_cast<int>(d - terrain.data());
    }
    return true;
}

bool laneCost(const Scenario& s, const std::vector<TerrainDef>& terrain,
              const Hex& from, const Hex& to, bool allowBridge, int& outCost) {
    Board board;
    if (!scenarioBoard(s, terrain, board)) return false;

    // The Bridge-free clause is applied to the TERRAIN TABLE, not to the search: a
    // Bridge whose MoveCost is the §4.8 impassable sentinel is a hex Move.h already
    // refuses to enter, so T-SCN-06 and T-SCN-11 run the SAME pathfinder over two
    // different graphs rather than two pathfinders.
    std::vector<TerrainDef> table = terrain;
    if (!allowBridge)
        for (TerrainDef& d : table)
            if (d.id == "Bridge") { d.moveCost = 0; d.passLand = false; }

    std::vector<Hex> path;
    int cost = 0;
    if (!findPath(board, table, from, to, kUnbounded, path, cost)) return false;
    outCost = cost;
    return true;
}

// ---------------------------------------------------------------------------
// parse
// ---------------------------------------------------------------------------
ScenarioLoadResult parseScenario(const std::string& text, const std::string& origin,
                                 Scenario& out) {
    ScenarioLoadResult r;
    r.failedId = "GATE-SCN-PARSE";

    JsonParser jp(text);
    Json root;
    if (!jp.parse(root, 0)) { r.reason = origin + ": " + jp.err; return r; }
    jp.skipWs();
    if (jp.i != text.size()) { r.reason = origin + ": trailing content after the object"; return r; }
    if (root.kind != Json::Kind::Object) { r.reason = origin + ": the file must be one object"; return r; }

    std::string err;
    // `scenarioHash` is the one optional field: it is DERIVED from the others, and a
    // schema that makes an author hand-compute a digest before their own file will
    // load is a contract no author can keep. Declared, it must match (loadScenario).
    std::vector<std::string> top = {"formatVersion", "scenarioId", "map", "ownership",
                                    "placements", "startingFame", "turnCap",
                                    "guidedOpening", "symmetry"};
    for (const std::string& k : root.keys) {
        bool known = (k == "scenarioHash");
        for (const std::string& a : top) if (a == k) known = true;
        if (!known) { r.reason = origin + ": unknown field '" + k + "'"; return r; }
    }
    for (const std::string& a : top)
        if (member(root, a) == nullptr) {
            r.reason = origin + ": missing field '" + a + "'"; return r; }

    Scenario s;

    if (!wantInt(root, "formatVersion", origin, s.formatVersion, err)) { r.reason = err; return r; }
    if (s.formatVersion != SCENARIO_FORMAT_VERSION) {
        r.reason = origin + ": formatVersion " + num(s.formatVersion) +
                   " is unknown to this build (it reads " + num(SCENARIO_FORMAT_VERSION) + ")";
        return r;
    }
    if (!wantString(root, "scenarioId", origin, s.scenarioId, err)) { r.reason = err; return r; }
    if (s.scenarioId.empty()) { r.reason = origin + ": 'scenarioId' must not be empty"; return r; }

    if (const Json* h = member(root, "scenarioHash")) {
        if (h->kind != Json::Kind::String) {
            r.reason = origin + ": 'scenarioHash' must be a string"; return r; }
        s.hasDeclaredHash = true;
        s.declaredHash    = h->str;
    }

    // --- map ---------------------------------------------------------------
    {
        const Json* m = member(root, "map");
        if (m->kind != Json::Kind::Object) { r.reason = origin + ": 'map' must be an object"; return r; }
        if (!onlyKeys(*m, {"width", "height", "terrain"}, origin + ".map", err)) { r.reason = err; return r; }
        if (!wantInt(*m, "width", origin + ".map", s.bounds.cols, err)) { r.reason = err; return r; }
        if (!wantInt(*m, "height", origin + ".map", s.bounds.rows, err)) { r.reason = err; return r; }
        if (s.bounds.cols < 1 || s.bounds.rows < 1 || s.bounds.cols > 512 || s.bounds.rows > 512) {
            r.reason = origin + ".map: width/height must be 1..512"; return r; }
        const Json* t = member(*m, "terrain");
        if (t->kind != Json::Kind::Array ||
            static_cast<int>(t->vals.size()) != s.bounds.rows) {
            r.reason = origin + ".map: 'terrain' must hold exactly " + num(s.bounds.rows) +
                       " rows"; return r; }
        for (int row = 0; row < s.bounds.rows; ++row) {
            const Json& line = t->vals[static_cast<std::size_t>(row)];
            if (line.kind != Json::Kind::Array ||
                static_cast<int>(line.vals.size()) != s.bounds.cols) {
                r.reason = origin + ".map: row " + num(row) + " must hold exactly " +
                           num(s.bounds.cols) + " terrain Ids"; return r; }
            for (const Json& cell : line.vals) {
                if (cell.kind != Json::Kind::String) {
                    r.reason = origin + ".map: a terrain entry is not a string"; return r; }
                s.terrainId.push_back(cell.str);
            }
        }
    }

    // --- ownership ---------------------------------------------------------
    {
        const Json* o = member(root, "ownership");
        if (o->kind != Json::Kind::Array) { r.reason = origin + ": 'ownership' must be an array"; return r; }
        for (const Json& e : o->vals) {
            if (e.kind != Json::Kind::Object) { r.reason = origin + ".ownership: entry is not an object"; return r; }
            if (!onlyKeys(e, {"hex", "owner"}, origin + ".ownership", err)) { r.reason = err; return r; }
            ScenarioOwner so;
            if (!wantHex(e, "hex", origin + ".ownership", so.hex, err)) { r.reason = err; return r; }
            if (!wantInt(e, "owner", origin + ".ownership", so.owner, err)) { r.reason = err; return r; }
            if (so.owner != OWNER_NEUTRAL && (so.owner < 0 || so.owner >= SIDE_COUNT)) {
                r.reason = origin + ".ownership: 'owner' must be " + num(OWNER_NEUTRAL) +
                           " (neutral), 0 or 1"; return r; }
            s.ownership.push_back(so);
        }
    }

    // --- placements --------------------------------------------------------
    {
        const Json* p = member(root, "placements");
        if (p->kind != Json::Kind::Array) { r.reason = origin + ": 'placements' must be an array"; return r; }
        for (const Json& e : p->vals) {
            if (e.kind != Json::Kind::Object) { r.reason = origin + ".placements: entry is not an object"; return r; }
            if (!onlyKeys(e, {"side", "unitId", "hex", "isFlag"}, origin + ".placements", err)) {
                r.reason = err; return r; }
            ScenarioPlacement sp;
            if (!wantInt(e, "side", origin + ".placements", sp.side, err)) { r.reason = err; return r; }
            if (sp.side < 0 || sp.side >= SIDE_COUNT) {
                r.reason = origin + ".placements: 'side' must be 0 or 1"; return r; }
            if (!wantString(e, "unitId", origin + ".placements", sp.unitId, err)) { r.reason = err; return r; }
            if (!wantHex(e, "hex", origin + ".placements", sp.hex, err)) { r.reason = err; return r; }
            if (!wantBool(e, "isFlag", origin + ".placements", sp.isFlag, err)) { r.reason = err; return r; }
            s.placements.push_back(sp);
        }
    }

    // --- startingFame ------------------------------------------------------
    {
        const Json* f = member(root, "startingFame");
        if (f->kind != Json::Kind::Object) { r.reason = origin + ": 'startingFame' must be an object"; return r; }
        if (!onlyKeys(*f, {"side0", "side1"}, origin + ".startingFame", err)) { r.reason = err; return r; }
        if (!wantInt(*f, "side0", origin + ".startingFame", s.startingFame[0], err)) { r.reason = err; return r; }
        if (!wantInt(*f, "side1", origin + ".startingFame", s.startingFame[1], err)) { r.reason = err; return r; }
        for (int i = 0; i < SIDE_COUNT; ++i)
            if (s.startingFame[i] < 0) {
                r.reason = origin + ".startingFame: a side's starting Fame is negative"; return r; }
    }

    if (!wantInt(root, "turnCap", origin, s.turnCap, err)) { r.reason = err; return r; }
    if (s.turnCap < 1) { r.reason = origin + ": 'turnCap' must be >= 1 (§2.8, Q7)"; return r; }

    // --- guidedOpening -----------------------------------------------------
    {
        const Json* g = member(root, "guidedOpening");
        if (g->kind != Json::Kind::Array) { r.reason = origin + ": 'guidedOpening' must be an array"; return r; }
        for (const Json& e : g->vals) {
            if (e.kind != Json::Kind::Object) { r.reason = origin + ".guidedOpening: entry is not an object"; return r; }
            if (!onlyKeys(e, {"side", "infantry", "objective"}, origin + ".guidedOpening", err)) {
                r.reason = err; return r; }
            ScenarioGuided sg;
            if (!wantInt(e, "side", origin + ".guidedOpening", sg.side, err)) { r.reason = err; return r; }
            if (sg.side < 0 || sg.side >= SIDE_COUNT) {
                r.reason = origin + ".guidedOpening: 'side' must be 0 or 1"; return r; }
            if (!wantHex(e, "infantry", origin + ".guidedOpening", sg.infantry, err)) { r.reason = err; return r; }
            if (!wantHex(e, "objective", origin + ".guidedOpening", sg.objective, err)) { r.reason = err; return r; }
            s.guided.push_back(sg);
        }
    }

    // --- symmetry ----------------------------------------------------------
    // REQUIRED, and unrecognized is a hard load failure. A scenario that forgets to
    // declare must not silently claim the weakest claim (§4.7 Stub 7).
    {
        std::string sym;
        if (!wantString(root, "symmetry", origin, sym, err)) { r.reason = err; return r; }
        if      (sym == "none")   s.symmetry = Symmetry::None;
        else if (sym == "rot180") s.symmetry = Symmetry::Rot180;
        else { r.reason = origin + ": 'symmetry' is '" + sym +
                          "'; the declarable values are rot180 and none (Q24, Q26)"; return r; }
    }

    out = s;
    r.ok = true;
    r.failedId.clear();
    return r;
}

// ---------------------------------------------------------------------------
// validate
// ---------------------------------------------------------------------------
namespace {

ScenarioLoadResult refuse(ScenarioLoadResult r, const std::string& id, const std::string& why) {
    r.ok = false;
    r.failedId = id;
    r.reason = why;
    return r;
}

// A Factory, identified from the LOADED TABLE rather than by row name: §4.8 marks
// Factory capturable AND a spawn point, and §2.7 makes spawning a factory-only
// property, so Town (capturable, not a spawn point) is the only other capturable row.
bool isFactory(const TerrainDef& d) { return d.capturable && d.isSpawnPoint; }

const TerrainDef* terrainAtHex(const Scenario& s, const std::vector<TerrainDef>& terrain,
                               const Hex& h) {
    if (!inBounds(h, s.bounds)) return nullptr;
    int c = 0, r = 0;
    axialToOffset(h, c, r);
    const std::size_t i = static_cast<std::size_t>(r) * s.bounds.cols + c;
    if (i >= s.terrainId.size()) return nullptr;
    return findTerrain(terrain, s.terrainId[i]);
}

int ownerOf(const Scenario& s, const Hex& h) {
    for (const ScenarioOwner& o : s.ownership)
        if (hexEqual(o.hex, h)) return o.owner;
    return OWNER_NEUTRAL;   // a capturable hex the file does not name is UNOWNED (§2.7)
}

// The odd-r offset neighbour rule, written from the offset convention alone. It is
// deliberately NOT Hex.h's axial arithmetic: T-SCN-05 asks whether the loaded map's
// adjacency matches the AUTHORED grid's, and a check that calls the same function
// twice answers nothing.
void offsetNeighbors(int col, int row, int out[6][2]) {
    const int odd = row & 1;
    const int e[6][2]  = {{col + 1, row}, {col, row - 1}, {col - 1, row - 1},
                          {col - 1, row}, {col - 1, row + 1}, {col, row + 1}};
    const int o[6][2]  = {{col + 1, row}, {col + 1, row - 1}, {col, row - 1},
                          {col - 1, row}, {col, row + 1}, {col + 1, row + 1}};
    for (int i = 0; i < 6; ++i) {
        out[i][0] = odd ? o[i][0] : e[i][0];
        out[i][1] = odd ? o[i][1] : e[i][1];
    }
}

} // namespace

ScenarioLoadResult validateScenario(const Scenario& s,
                                    const std::vector<UnitDef>& units,
                                    const std::vector<TerrainDef>& terrain) {
    ScenarioLoadResult r;
    r.ok = true;

    // ---- T-SCN-02: structural validity, first, because everything references it.
    const std::size_t cells = static_cast<std::size_t>(s.bounds.cols) *
                              static_cast<std::size_t>(s.bounds.rows);
    if (s.bounds.cols < 1 || s.bounds.rows < 1 || s.terrainId.size() != cells)
        return refuse(r, "T-SCN-02", "the map declares " + num(s.bounds.cols) + " x " +
                      num(s.bounds.rows) + " but carries " +
                      num(static_cast<int>(s.terrainId.size())) + " terrain Ids");
    for (std::size_t i = 0; i < s.terrainId.size(); ++i)
        if (findTerrain(terrain, s.terrainId[i]) == nullptr)
            return refuse(r, "T-SCN-02", "terrain Id '" + s.terrainId[i] +
                          "' resolves to no row in the loaded table (§4.8)");

    for (std::size_t i = 0; i < s.ownership.size(); ++i) {
        const ScenarioOwner& o = s.ownership[i];
        if (!inBounds(o.hex, s.bounds))
            return refuse(r, "T-SCN-02", "ownership names " + hexLabel(o.hex) +
                          ", which is out of bounds (T-HEX-05)");
        const TerrainDef* d = terrainAtHex(s, terrain, o.hex);
        if (d == nullptr || !d->capturable)
            return refuse(r, "T-SCN-02", "ownership names " + hexLabel(o.hex) +
                          ", which is not a capturable hex");
        for (std::size_t k = 0; k < i; ++k)
            if (hexEqual(s.ownership[k].hex, o.hex))
                return refuse(r, "T-SCN-02", "ownership names " + hexLabel(o.hex) + " twice");
    }

    for (std::size_t i = 0; i < s.placements.size(); ++i) {
        const ScenarioPlacement& p = s.placements[i];
        if (!inBounds(p.hex, s.bounds))
            return refuse(r, "T-SCN-02", "a placement stands at " + hexLabel(p.hex) +
                          ", which is out of bounds (T-HEX-05)");
        if (findUnit(units, p.unitId) == nullptr)
            return refuse(r, "T-SCN-02", "unit Id '" + p.unitId +
                          "' resolves to no row in the loaded table (§4.8)");
        for (std::size_t k = 0; k < i; ++k)
            if (hexEqual(s.placements[k].hex, p.hex))
                return refuse(r, "T-SCN-02", "two placements share " + hexLabel(p.hex) + " (§2.5)");
    }

    // ---- T-SCN-05: odd-r <-> axial round-trips, and adjacency survives the frame.
    for (int row = 0; row < s.bounds.rows; ++row) {
        for (int col = 0; col < s.bounds.cols; ++col) {
            const Hex h = offsetToAxial(col, row);
            int backCol = 0, backRow = 0;
            axialToOffset(h, backCol, backRow);
            if (backCol != col || backRow != row)
                return refuse(r, "T-SCN-05", "(" + num(col) + "," + num(row) +
                              ") does not round-trip through axial");
            if (!inBounds(h, s.bounds))
                return refuse(r, "T-SCN-05", "(" + num(col) + "," + num(row) +
                              ") converts to a hex the declared bounds reject");

            Hex adj[HEX_DIRECTIONS];
            const int n = neighbors(h, s.bounds, adj);
            int expect[6][2];
            offsetNeighbors(col, row, expect);
            int wanted = 0;
            for (int i = 0; i < 6; ++i) {
                const int c = expect[i][0], rw = expect[i][1];
                if (c < 0 || rw < 0 || c >= s.bounds.cols || rw >= s.bounds.rows) continue;
                ++wanted;
                bool seen = false;
                for (int k = 0; k < n; ++k) {
                    int ac = 0, ar = 0;
                    axialToOffset(adj[k], ac, ar);
                    if (ac == c && ar == rw) { seen = true; break; }
                }
                if (!seen)
                    return refuse(r, "T-SCN-05", "the loaded map's adjacency at (" + num(col) + "," +
                                  num(row) + ") drops the authored neighbour (" + num(c) + "," +
                                  num(rw) + ")");
            }
            if (wanted != n)
                return refuse(r, "T-SCN-05", "the loaded map's adjacency at (" + num(col) + "," +
                              num(row) + ") holds " + num(n) + " neighbours; the authored grid has " +
                              num(wanted));
        }
    }

    // ---- T-SCN-01: exactly one isFlag per side, and it is a Tank.
    Hex flagHex[SIDE_COUNT];
    int flagCount[SIDE_COUNT] = {0, 0};
    for (const ScenarioPlacement& p : s.placements) {
        if (!p.isFlag) continue;
        const UnitDef* d = findUnit(units, p.unitId);
        if (d == nullptr || d->type != UnitType::Tank)
            return refuse(r, "T-SCN-01", "the flag at " + hexLabel(p.hex) + " is a " + p.unitId +
                          "; §2.4 makes the flag a designated Tank");
        flagHex[p.side] = p.hex;
        ++flagCount[p.side];
    }
    for (int side = 0; side < SIDE_COUNT; ++side)
        if (flagCount[side] != 1)
            return refuse(r, "T-SCN-01", "side " + num(side) + " declares " + num(flagCount[side]) +
                          " flag placements; exactly one is required");

    // ---- T-SCN-03: economy validity (§2.7's "~4 factories total").
    int homeFactories[SIDE_COUNT] = {0, 0};
    int neutralFactories = 0;
    for (int row = 0; row < s.bounds.rows; ++row) {
        for (int col = 0; col < s.bounds.cols; ++col) {
            const Hex h = offsetToAxial(col, row);
            const TerrainDef* d = terrainAtHex(s, terrain, h);
            if (d == nullptr || !isFactory(*d)) continue;
            const int owner = ownerOf(s, h);
            if (owner == OWNER_NEUTRAL) ++neutralFactories;
            else ++homeFactories[owner];
        }
    }
    for (int side = 0; side < SIDE_COUNT; ++side)
        if (homeFactories[side] != 1)
            return refuse(r, "T-SCN-03", "side " + num(side) + " owns " + num(homeFactories[side]) +
                          " factories at start; exactly one home factory is required (§2.7)");
    if (neutralFactories < 2)
        return refuse(r, "T-SCN-03", "the map holds " + num(neutralFactories) +
                      " neutral factories; §2.7's layout needs at least two in contested ground");

    // ---- T-SCN-09: the declared symmetry is VERIFIED, not trusted.
    // Runs here because it quantifies over map, ownership and placements and over
    // nothing that costs a path. `none` asserts nothing and is always well-formed.
    if (s.symmetry == Symmetry::Rot180) {
        // The EVEN ROW COUNT is a PRECONDITION, not a comparison: on odd H the axial
        // constant W - H/2 is a half-integer, so NO hex has a hex image and the file
        // is refused before any comparison runs (Q24, ruled).
        if ((s.bounds.rows % 2) != 0)
            return refuse(r, "T-SCN-09", "symmetry rot180 is declared on " + num(s.bounds.cols) +
                          " x " + num(s.bounds.rows) + ": the row count is odd, so the axial "
                          "constant W - H/2 = " + num(s.bounds.cols) + " - " +
                          num(s.bounds.rows) + "/2 is a half-integer and no hex has a hex image");
        const int kq = s.bounds.cols - s.bounds.rows / 2;
        const int kr = s.bounds.rows - 1;
        // terrain
        for (int row = 0; row < s.bounds.rows; ++row) {
            for (int col = 0; col < s.bounds.cols; ++col) {
                const Hex h = offsetToAxial(col, row);
                Hex im; im.q = kq - h.q; im.r = kr - h.r;
                if (!inBounds(im, s.bounds))
                    return refuse(r, "T-SCN-09", "rot180 sends " + hexLabel(h) +
                                  " off the board");
                const TerrainDef* a = terrainAtHex(s, terrain, h);
                const TerrainDef* b = terrainAtHex(s, terrain, im);
                if (a == nullptr || b == nullptr || a->id != b->id)
                    return refuse(r, "T-SCN-09", "rot180 is declared but " + hexLabel(h) +
                                  " and its image " + hexLabel(im) + " carry different terrain");
            }
        }
        // ownership: maps onto itself with the two sides EXCHANGED
        for (int row = 0; row < s.bounds.rows; ++row) {
            for (int col = 0; col < s.bounds.cols; ++col) {
                const Hex h = offsetToAxial(col, row);
                const TerrainDef* d = terrainAtHex(s, terrain, h);
                if (d == nullptr || !d->capturable) continue;
                Hex im; im.q = kq - h.q; im.r = kr - h.r;
                const int a = ownerOf(s, h), b = ownerOf(s, im);
                const int want = (a == OWNER_NEUTRAL) ? OWNER_NEUTRAL : (SIDE_COUNT - 1 - a);
                if (b != want)
                    return refuse(r, "T-SCN-09", "rot180 is declared but " + hexLabel(h) +
                                  " (owner " + num(a) + ") images to " + hexLabel(im) +
                                  " (owner " + num(b) + "), not to owner " + num(want));
            }
        }
        // placements: maps onto itself with sides exchanged, unitId and isFlag kept
        for (const ScenarioPlacement& p : s.placements) {
            Hex im; im.q = kq - p.hex.q; im.r = kr - p.hex.r;
            const ScenarioPlacement* mate = nullptr;
            for (const ScenarioPlacement& o : s.placements)
                if (hexEqual(o.hex, im)) { mate = &o; break; }
            if (mate == nullptr || mate->side != (SIDE_COUNT - 1 - p.side) ||
                mate->unitId != p.unitId || mate->isFlag != p.isFlag)
                return refuse(r, "T-SCN-09", "rot180 is declared but the placement at " +
                              hexLabel(p.hex) + " has no image at " + hexLabel(im) +
                              " with the sides exchanged and unitId/isFlag preserved");
        }
    }

    // ---- the capturing row, DERIVED from the loaded table (T-DATA-03).
    const int capIdx = captureRowIndex(units);
    if (capIdx < 0)
        return refuse(r, "T-SCN-06", "the loaded unit table does not hold exactly one CanCapture "
                      "row; §2.7's Infantry-only rule and this lane would name different units");
    const UnitDef& cap = units[static_cast<std::size_t>(capIdx)];
    r.captureMove = cap.move;
    r.ceiling     = 2 * cap.move;      // DERIVED, never a literal (§2.4 Move 3 -> 6 MP)

    // ---- T-SCN-07: opening-capture naming (structural; no pathing).
    if (static_cast<int>(s.guided.size()) != SIDE_COUNT)
        return refuse(r, "T-SCN-07", "the file declares " + num(static_cast<int>(s.guided.size())) +
                      " guidedOpening entries; exactly one per side is required");
    {
        int perSide[SIDE_COUNT] = {0, 0};
        for (const ScenarioGuided& g : s.guided) ++perSide[g.side];
        for (int side = 0; side < SIDE_COUNT; ++side)
            if (perSide[side] != 1)
                return refuse(r, "T-SCN-07", "side " + num(side) + " has " + num(perSide[side]) +
                              " guidedOpening entries; exactly one is required");
    }
    for (const ScenarioGuided& g : s.guided) {
        const ScenarioPlacement* p = nullptr;
        for (const ScenarioPlacement& q : s.placements)
            if (hexEqual(q.hex, g.infantry)) { p = &q; break; }
        if (p == nullptr || p->side != g.side)
            return refuse(r, "T-SCN-07", "side " + num(g.side) + "'s guided infantry hex " +
                          hexLabel(g.infantry) + " holds no starting placement of that side");
        if (p->unitId != cap.id)
            return refuse(r, "T-SCN-07", "side " + num(g.side) + "'s guided unit at " +
                          hexLabel(g.infantry) + " is a " + p->unitId + "; the CanCapture row is " +
                          cap.id);
        if (p->isFlag)
            return refuse(r, "T-SCN-07", "side " + num(g.side) + "'s guided unit at " +
                          hexLabel(g.infantry) + " is the flag");
        const TerrainDef* d = terrainAtHex(s, terrain, g.objective);
        if (d == nullptr || !isFactory(*d))
            return refuse(r, "T-SCN-07", "side " + num(g.side) + "'s objective " +
                          hexLabel(g.objective) + " is not a Factory hex");
        if (ownerOf(s, g.objective) != OWNER_NEUTRAL)
            return refuse(r, "T-SCN-07", "side " + num(g.side) + "'s objective " +
                          hexLabel(g.objective) + " is not neutral at start (§2.7)");
    }
    for (std::size_t i = 0; i < s.guided.size(); ++i)
        for (std::size_t k = 0; k < i; ++k)
            if (hexEqual(s.guided[i].objective, s.guided[k].objective))
                return refuse(r, "T-SCN-07", "both seats name " + hexLabel(s.guided[i].objective) +
                              "; §2.13.1's \"the seat's own neutral\" needs distinct objectives");

    // ---- T-SCN-04: playability -- the two flags are mutually reachable by land.
    {
        int ignored = 0;
        const bool there = laneCost(s, terrain, flagHex[0], flagHex[1], true, ignored);
        const bool back  = laneCost(s, terrain, flagHex[1], flagHex[0], true, ignored);
        if (!there || !back)
            return refuse(r, "T-SCN-04", "the flags at " + hexLabel(flagHex[0]) + " and " +
                          hexLabel(flagHex[1]) + " are not mutually reachable by land; a "
                          "scenario cannot be born stalemated");
    }

    // ---- T-SCN-08: MEASURE every lane, in side order, before anything compares.
    for (int side = 0; side < SIDE_COUNT; ++side) {
        for (const ScenarioGuided& g : s.guided) {
            if (g.side != side) continue;
            ScenarioLane lane;
            lane.side      = g.side;
            lane.infantry  = g.infantry;
            lane.objective = g.objective;
            int cost = 0;
            // Bridge-free: what confines the first lesson to the seat's own bank.
            lane.laneFound = laneCost(s, terrain, g.infantry, g.objective, false, cost);
            lane.laneCost  = lane.laneFound ? cost : 0;
            r.lanes.push_back(lane);
        }
    }

    // ---- T-SCN-06: the ceiling, compared AFTER the measurement.
    for (const ScenarioLane& lane : r.lanes) {
        if (!lane.laneFound)
            return refuse(r, "T-SCN-06", "side " + num(lane.side) + "'s lane " +
                          hexLabel(lane.infantry) + " -> " + hexLabel(lane.objective) +
                          " has no Bridge-free land route");
        if (lane.laneCost > r.ceiling)
            return refuse(r, "T-SCN-06", "side " + num(lane.side) + "'s lane " +
                          hexLabel(lane.infantry) + " -> " + hexLabel(lane.objective) +
                          " costs " + num(lane.laneCost) + " against the " + num(r.ceiling) +
                          " MP ceiling (2 x " + cap.id + " Move " + num(cap.move) + ")");
    }

    // ---- T-SCN-11: NON-CONTENTION. The opposing route is minimised over EVERY
    // CanCapture-row unit that seat deploys (Q28), never over that seat's
    // guidedOpening.infantry alone. Bridges ARE permitted here (asymmetry (ii)).
    std::vector<ScenarioPlacement> byHex = s.placements;
    std::stable_sort(byHex.begin(), byHex.end(), placeLessByHex);
    for (ScenarioLane& lane : r.lanes) {
        const int foe = SIDE_COUNT - 1 - lane.side;
        for (const ScenarioPlacement& p : byHex) {
            if (p.side != foe || p.unitId != cap.id) continue;
            int cost = 0;
            if (!laneCost(s, terrain, p.hex, lane.objective, true, cost)) continue;
            if (!lane.opposingFound || cost < lane.opposingCost) {
                lane.opposingFound = true;
                lane.opposingCost  = cost;
                lane.opposingFrom  = p.hex;
            }
        }
    }
    for (const ScenarioLane& lane : r.lanes) {
        if (!lane.opposingFound) continue;              // no route is not a contest
        if (lane.opposingCost > lane.laneCost) continue;
        return refuse(r, "T-SCN-11", "side " + num(lane.side) + "'s guided lane " +
                      hexLabel(lane.infantry) + " -> " + hexLabel(lane.objective) +
                      " is contested: " + num(lane.laneCost) + " against " +
                      num(lane.opposingCost) + ". The opposing seat's cheapest " + cap.id +
                      " route to that objective must cost strictly more.");
    }

    return r;
}

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------
ScenarioLoadResult loadScenario(const std::string& path,
                                const std::vector<UnitDef>& units,
                                const std::vector<TerrainDef>& terrain,
                                Scenario& out) {
    ScenarioLoadResult r;
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        r.failedId = "GATE-SCN-PARSE";
        r.reason   = "cannot open '" + path + "'";
        return r;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string text = buf.str();

    Scenario parsed;
    ScenarioLoadResult p = parseScenario(text, path, parsed);
    if (!p.ok) return p;

    // The declared hash is a property of the FILE, not of the Scenario, so it is
    // checked here and not in validateScenario -- which is what lets a fixture mutate
    // one field of a loaded scenario and still be judged on the invariants.
    if (parsed.hasDeclaredHash) {
        const std::string actual = scenarioHash(parsed);
        if (actual != parsed.declaredHash) {
            r.failedId = "GATE-SCN-HASH";
            r.reason   = path + ": declared scenarioHash " + parsed.declaredHash +
                         " does not match the canonical serialization, which hashes to " + actual;
            return r;
        }
    }

    ScenarioLoadResult v = validateScenario(parsed, units, terrain);
    if (v.ok) out = parsed;
    return v;
}

} // namespace strat
