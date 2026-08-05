// Stratocracy — save & replay format, part (a). Reference implementation.
// See Save.h for the part boundary and spec/save_spec.md for the eight stated readings.
//
// Depends on Hex.h for the odd-r <-> axial conversion and on NOTHING ELSE. That empty
// dependency set is the claim §4.11 makes about part (a), and it is checked by the
// link set in crew/tools.py, not by this comment.

#include "Save.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace strat {

namespace {

// Guards a hand-written recursive-descent parser against an adversarial file. A save
// nests three deep (root -> commandLog -> entry -> hex array); 32 is slack, not a
// limit anyone authors into. The same constant Scenario.good.cpp uses, for the same
// reason -- arrived at independently here rather than shared, since sharing it would
// be the dependency this part does not have.
constexpr int kMaxDepth = 32;

std::string num(int v) { return std::to_string(v); }

// ---------------------------------------------------------------------------
// A strict JSON subset. Objects, arrays, strings, INTEGERS, the two booleans, and
// `null` -- which is accepted as a VALUE KIND here and refused everywhere except
// `result` by the schema layer below (reading 3). §4.10's own type column reads
// "string/null" for that field, so the schema requires it; nothing else in the
// layout admits it. No floats, no exponents, no \u escape, no trailing comma, no
// duplicate key, no trailing content. Each is a refusal with a reason.
// ---------------------------------------------------------------------------
struct Json {
    enum class Kind { Object, Array, String, Int, Bool, Null };
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
        if (src.compare(i, 4, "null") == 0)  { i += 4; out.kind = Json::Kind::Null; return true; }
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

// Every key an object is allowed to carry, and every key it MUST. An unknown key is a
// refusal, not an ignored extra: within one formatVersion an unrecognized key is a
// typo, and tolerating it is how a save silently loses a field it thought it declared.
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

// A hex reference in the AUTHORED frame: [col, row], odd-r. Converted here and nowhere
// else, so no parsed state ever holds (col, row).
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

SaveLoadResult refuse(const std::string& id, const std::string& why) {
    SaveLoadResult r;
    r.ok = false;
    r.failedId = id;
    r.reason = why;
    return r;
}

SaveLoadResult accept() {
    SaveLoadResult r;
    r.ok = true;
    return r;
}

// The per-kind field list. `turn`, `side` and `kind` are universal; the rest is exactly
// what §4.9 names for that command and nothing else, so a Move carrying `targetHex` is
// refused by onlyKeys rather than quietly ignored.
bool commandKeysFor(SaveCommandKind k, std::vector<std::string>& out) {
    out.clear();
    out.push_back("turn");
    out.push_back("side");
    out.push_back("kind");
    switch (k) {
        case SaveCommandKind::Move:    out.push_back("unit");       out.push_back("destHex");    return true;
        case SaveCommandKind::Attack:  out.push_back("unit");       out.push_back("targetHex");  return true;
        case SaveCommandKind::Build:   out.push_back("unitId");     out.push_back("factoryHex"); return true;
        case SaveCommandKind::Capture: out.push_back("unit");                                    return true;
        case SaveCommandKind::EndTurn:                                                           return true;
    }
    return false;
}

bool kindFromName(const std::string& name, SaveCommandKind& out) {
    if (name == "Move")    { out = SaveCommandKind::Move;    return true; }
    if (name == "Attack")  { out = SaveCommandKind::Attack;  return true; }
    if (name == "Build")   { out = SaveCommandKind::Build;   return true; }
    if (name == "Capture") { out = SaveCommandKind::Capture; return true; }
    if (name == "EndTurn") { out = SaveCommandKind::EndTurn; return true; }
    return false;
}

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    out += "\"";
    return out;
}

std::string hexLiteral(const Hex& h) {
    int col = 0, row = 0;
    axialToOffset(h, col, row);
    return "[" + num(col) + ", " + num(row) + "]";
}

} // namespace

const char* saveCommandName(SaveCommandKind k) {
    switch (k) {
        case SaveCommandKind::Move:    return "Move";
        case SaveCommandKind::Attack:  return "Attack";
        case SaveCommandKind::Build:   return "Build";
        case SaveCommandKind::Capture: return "Capture";
        case SaveCommandKind::EndTurn: return "EndTurn";
    }
    return "EndTurn";
}

// ---------------------------------------------------------------------------
// parse
// ---------------------------------------------------------------------------
SaveLoadResult parseSave(const std::string& text, const std::string& origin, Save& out) {
    // Everything below fills THIS local. `out` is assigned once, at the very end, on
    // success alone -- reading 6, and the whole of T-SAVE-04's "state untouched" clause
    // at part (a). Save.buggy.cpp is this function writing through `out` as it goes.
    Save s;

    JsonParser p(text);
    Json root;
    if (!p.parse(root, 0))
        return refuse("GATE-SAVE-PARSE", origin + ": " + p.err);
    p.skipWs();
    if (p.i != text.size())
        return refuse("GATE-SAVE-PARSE", origin + ": trailing content after the object");
    if (root.kind != Json::Kind::Object)
        return refuse("GATE-SAVE-PARSE", origin + ": the file's root must be an object");

    // Exactly §4.10's file-layout table -- every field required, no field extra.
    const std::vector<std::string> allowed = {
        "formatVersion", "rulesCommit", "dataHash", "scenarioId", "scenarioHash",
        "seed", "commandLog", "stateHash", "result"
    };
    std::string err;
    if (!onlyKeys(root, allowed, origin, err))
        return refuse("GATE-SAVE-PARSE", err);

    if (!wantInt(root, "formatVersion", origin, s.formatVersion, err))
        return refuse("GATE-SAVE-PARSE", err);
    if (!wantString(root, "rulesCommit",  origin, s.rulesCommit,  err))
        return refuse("GATE-SAVE-PARSE", err);
    if (!wantString(root, "dataHash",     origin, s.dataHash,     err))
        return refuse("GATE-SAVE-PARSE", err);
    if (!wantString(root, "scenarioId",   origin, s.scenarioId,   err))
        return refuse("GATE-SAVE-PARSE", err);
    if (!wantString(root, "scenarioHash", origin, s.scenarioHash, err))
        return refuse("GATE-SAVE-PARSE", err);
    if (!wantInt(root, "seed", origin, s.seed, err))
        return refuse("GATE-SAVE-PARSE", err);
    // Reading 5: §4.10 says the field is reserved and WRITTEN AS 0. A non-zero seed is
    // a schema violation, not a header disagreement, so it refuses here and not in
    // checkHeader -- a file promising RNG that no build reads is not a file to load.
    if (s.seed != 0)
        return refuse("GATE-SAVE-PARSE", origin + ": 'seed' is reserved and must be 0 "
                                                  "(no RNG ships; §2.6, pending Q12)");
    if (!wantString(root, "stateHash", origin, s.stateHash, err))
        return refuse("GATE-SAVE-PARSE", err);

    // Reading 3: `result` is the ONE field whose value may be null, because §4.10's
    // type column reads "string/null". Anywhere else a null has already been refused
    // by the type checks above.
    {
        const Json* v = member(root, "result");
        if (v == nullptr) return refuse("GATE-SAVE-PARSE", origin + ": missing field 'result'");
        if (v->kind == Json::Kind::Null) { s.hasResult = false; s.result.clear(); }
        else if (v->kind == Json::Kind::String) { s.hasResult = true; s.result = v->str; }
        else return refuse("GATE-SAVE-PARSE", origin + ": 'result' must be a string or null");
    }

    // The command log, STRUCTURALLY. No command is applied, no legality is asked, and
    // no turn ordering is checked -- all three are part (b)'s.
    {
        const Json* log = member(root, "commandLog");
        if (log == nullptr || log->kind != Json::Kind::Array)
            return refuse("GATE-SAVE-PARSE", origin + ": 'commandLog' must be an array");
        for (std::size_t n = 0; n < log->vals.size(); ++n) {
            const Json& e = log->vals[n];
            const std::string where = origin + ": commandLog[" + num(static_cast<int>(n)) + "]";
            if (e.kind != Json::Kind::Object)
                return refuse("GATE-SAVE-PARSE", where + " must be an object");

            std::string kindName;
            if (!wantString(e, "kind", where, kindName, err))
                return refuse("GATE-SAVE-PARSE", err);
            SaveCommand c;
            if (!kindFromName(kindName, c.kind))
                return refuse("GATE-SAVE-PARSE", where + ": unknown command kind '" + kindName +
                                                 "' (§4.9 names Move, Attack, Build, Capture, EndTurn)");

            std::vector<std::string> keys;
            commandKeysFor(c.kind, keys);
            if (!onlyKeys(e, keys, where + " (" + kindName + ")", err))
                return refuse("GATE-SAVE-PARSE", err);

            if (!wantInt(e, "turn", where, c.turn, err)) return refuse("GATE-SAVE-PARSE", err);
            if (!wantInt(e, "side", where, c.side, err)) return refuse("GATE-SAVE-PARSE", err);
            if (c.turn < 1)
                return refuse("GATE-SAVE-PARSE", where + ": 'turn' must be 1 or greater");
            if (c.side < 0 || c.side > 1)
                return refuse("GATE-SAVE-PARSE", where + ": 'side' must be 0 or 1");

            switch (c.kind) {
                case SaveCommandKind::Move:
                    if (!wantInt(e, "unit", where, c.unitId, err)) return refuse("GATE-SAVE-PARSE", err);
                    if (!wantHex(e, "destHex", where, c.hex, err)) return refuse("GATE-SAVE-PARSE", err);
                    c.hasUnit = true; c.hasHex = true;
                    break;
                case SaveCommandKind::Attack:
                    if (!wantInt(e, "unit", where, c.unitId, err)) return refuse("GATE-SAVE-PARSE", err);
                    if (!wantHex(e, "targetHex", where, c.hex, err)) return refuse("GATE-SAVE-PARSE", err);
                    c.hasUnit = true; c.hasHex = true;
                    break;
                case SaveCommandKind::Build:
                    if (!wantInt(e, "unitId", where, c.unitId, err)) return refuse("GATE-SAVE-PARSE", err);
                    if (!wantHex(e, "factoryHex", where, c.hex, err)) return refuse("GATE-SAVE-PARSE", err);
                    c.hasUnit = true; c.hasHex = true;
                    break;
                case SaveCommandKind::Capture:
                    if (!wantInt(e, "unit", where, c.unitId, err)) return refuse("GATE-SAVE-PARSE", err);
                    c.hasUnit = true;
                    break;
                case SaveCommandKind::EndTurn:
                    break;
            }
            s.commandLog.push_back(c);
        }
    }

    out = s;              // the ONLY write to the caller's object, and only on success
    return accept();
}

// ---------------------------------------------------------------------------
// header comparison
// ---------------------------------------------------------------------------
SaveLoadResult checkHeader(const Save& s, const SaveHeaderExpectation& expect) {
    // The four fields §4.10's Version policy enumerates, in the table's order.
    // `scenarioId` is deliberately NOT among them (reading 2) -- widening the refusal
    // set would be a rule the GDD does not have, and `scenarioHash` already
    // distinguishes any two scenario files.
    //
    // Each refusal NAMES the disagreeing field and both values: §4.10 requires "refuse
    // load with a reason", so a bare false does not satisfy the invariant.
    if (s.formatVersion != expect.expectedVersion)
        return refuse("T-SAVE-04", "formatVersion mismatch: the file declares " +
                                   num(s.formatVersion) + " and this build accepts " +
                                   num(expect.expectedVersion) +
                                   " (no migration in the prototype)");
    if (s.rulesCommit != expect.rulesCommit)
        return refuse("T-SAVE-04", "rulesCommit mismatch: the file was written by '" +
                                   s.rulesCommit + "' and this build is '" +
                                   expect.rulesCommit +
                                   "' (a save is valid only against the rules that wrote it)");
    if (s.dataHash != expect.dataHash)
        return refuse("T-SAVE-04", "dataHash mismatch: the file was written against '" +
                                   s.dataHash + "' and the §4.8 data set in effect is '" +
                                   expect.dataHash + "'");
    if (s.scenarioHash != expect.scenarioHash)
        return refuse("T-SAVE-04", "scenarioHash mismatch: the file was written against '" +
                                   s.scenarioHash + "' and the loaded scenario is '" +
                                   expect.scenarioHash + "'");
    return accept();
}

// ---------------------------------------------------------------------------
// the ordinary load path
// ---------------------------------------------------------------------------
SaveLoadResult loadSave(const std::string& text, const std::string& origin,
                        const SaveHeaderExpectation& expect, Save& out) {
    // Parse into a LOCAL, check its header, and only then hand it over. A refused
    // header must leave the caller's object exactly as it was, which it cannot do if
    // the parse has already written through it.
    Save s;
    SaveLoadResult parsed = parseSave(text, origin, s);
    if (!parsed.ok) return parsed;

    SaveLoadResult header = checkHeader(s, expect);
    if (!header.ok) return header;

    out = s;
    return accept();
}

// ---------------------------------------------------------------------------
// serialize
// ---------------------------------------------------------------------------
std::string serializeSave(const Save& s) {
    // Fields in §4.10's table order, so a round trip is byte-stable and a diff between
    // two saves lines up field for field.
    std::string o;
    o += "{\n";
    o += "  \"formatVersion\": " + num(s.formatVersion) + ",\n";
    o += "  \"rulesCommit\": "   + quote(s.rulesCommit) + ",\n";
    o += "  \"dataHash\": "      + quote(s.dataHash) + ",\n";
    o += "  \"scenarioId\": "    + quote(s.scenarioId) + ",\n";
    o += "  \"scenarioHash\": "  + quote(s.scenarioHash) + ",\n";
    o += "  \"seed\": "          + num(s.seed) + ",\n";
    o += "  \"commandLog\": [";
    for (std::size_t n = 0; n < s.commandLog.size(); ++n) {
        const SaveCommand& c = s.commandLog[n];
        o += (n == 0) ? "\n" : ",\n";
        o += "    {\"turn\": " + num(c.turn) + ", \"side\": " + num(c.side) +
             ", \"kind\": " + quote(saveCommandName(c.kind));
        switch (c.kind) {
            case SaveCommandKind::Move:
                o += ", \"unit\": " + num(c.unitId) + ", \"destHex\": " + hexLiteral(c.hex);
                break;
            case SaveCommandKind::Attack:
                o += ", \"unit\": " + num(c.unitId) + ", \"targetHex\": " + hexLiteral(c.hex);
                break;
            case SaveCommandKind::Build:
                o += ", \"unitId\": " + num(c.unitId) + ", \"factoryHex\": " + hexLiteral(c.hex);
                break;
            case SaveCommandKind::Capture:
                o += ", \"unit\": " + num(c.unitId);
                break;
            case SaveCommandKind::EndTurn:
                break;
        }
        o += "}";
    }
    o += s.commandLog.empty() ? "]," : "\n  ],";
    o += "\n";
    o += "  \"stateHash\": " + quote(s.stateHash) + ",\n";
    o += "  \"result\": " + (s.hasResult ? quote(s.result) : std::string("null")) + "\n";
    o += "}\n";
    return o;
}

} // namespace strat
