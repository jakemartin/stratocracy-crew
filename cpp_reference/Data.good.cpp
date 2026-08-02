// Stratocracy — data table loaders (GDD §4.8, §4.7 Stub 2).
// Pure parse. A missing column or unparseable value is a HARD FAILURE: the loader
// returns false with a reason and leaves `out` untouched, so a caller can never end
// up holding a half-parsed or silently defaulted table.
#include "Data.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace strat {

namespace {

using Row = std::vector<std::string>;

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

Row splitRow(const std::string& line) {
    Row cells;
    std::string cell;
    std::istringstream in(line);
    while (std::getline(in, cell, ',')) cells.push_back(trim(cell));
    return cells;
}

// Reads the file into a header row + data rows. Blank lines are skipped; nothing
// else is tolerated.
bool readCsv(const std::string& path, Row& header, std::vector<Row>& rows, std::string& err) {
    std::ifstream in(path.c_str());
    if (!in) { err = "cannot open '" + path + "'"; return false; }
    std::string line;
    bool haveHeader = false;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty()) continue;
        if (!haveHeader) { header = splitRow(t); haveHeader = true; }
        else             { rows.push_back(splitRow(t)); }
    }
    if (!haveHeader) { err = "'" + path + "' is empty — no header row"; return false; }
    return true;
}

// Column index by name, or -1. Unknown EXTRA columns in the file are ignored, so a
// column added ahead of its ruling (e.g. MoveClass on Q2) breaks nothing.
int columnOf(const Row& header, const std::string& name) {
    for (std::size_t i = 0; i < header.size(); ++i)
        if (header[i] == name) return static_cast<int>(i);
    return -1;
}

bool requireColumns(const Row& header, const std::vector<std::string>& needed,
                    const std::string& path, std::string& err) {
    for (const std::string& n : needed) {
        if (columnOf(header, n) < 0) {
            err = "'" + path + "' is missing required column '" + n + "'";
            return false;
        }
    }
    return true;
}

bool cellAt(const Row& row, int col, const std::string& path, std::size_t lineNo,
            const std::string& colName, std::string& out, std::string& err) {
    if (col < 0 || static_cast<std::size_t>(col) >= row.size()) {
        err = "'" + path + "' row " + std::to_string(lineNo) + " has no value for '" + colName + "'";
        return false;
    }
    out = row[col];
    return true;
}

// Strict integer: optional '-', then at least one digit, then nothing else.
bool parseInt(const std::string& s, int& out) {
    if (s.empty()) return false;
    std::size_t i = (s[0] == '-') ? 1 : 0;
    if (i >= s.size()) return false;
    for (std::size_t k = i; k < s.size(); ++k)
        if (s[k] < '0' || s[k] > '9') return false;
    out = std::atoi(s.c_str());
    return true;
}

bool parseBool(const std::string& s, bool& out) {
    std::string l;
    for (char c : s) l += static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    if (l == "true")  { out = true;  return true; }
    if (l == "false") { out = false; return true; }
    return false;
}

// Strict double: strtod must consume the WHOLE token.
bool parseDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == nullptr || *end != '\0') return false;
    out = v;
    return true;
}

// --- typed field readers: each fails loudly, none defaults -------------------- //
bool readInt(const Row& row, const Row& header, const std::string& col,
             const std::string& path, std::size_t lineNo, int& out, std::string& err) {
    std::string cell;
    if (!cellAt(row, columnOf(header, col), path, lineNo, col, cell, err)) return false;
    if (!parseInt(cell, out)) {
        err = "'" + path + "' row " + std::to_string(lineNo) + " column '" + col +
              "': '" + cell + "' is not an integer";
        return false;
    }
    return true;
}

bool readBool(const Row& row, const Row& header, const std::string& col,
              const std::string& path, std::size_t lineNo, bool& out, std::string& err) {
    std::string cell;
    if (!cellAt(row, columnOf(header, col), path, lineNo, col, cell, err)) return false;
    if (!parseBool(cell, out)) {
        err = "'" + path + "' row " + std::to_string(lineNo) + " column '" + col +
              "': '" + cell + "' is not true/false";
        return false;
    }
    return true;
}

bool readText(const Row& row, const Row& header, const std::string& col,
              const std::string& path, std::size_t lineNo, std::string& out, std::string& err) {
    std::string cell;
    if (!cellAt(row, columnOf(header, col), path, lineNo, col, cell, err)) return false;
    if (cell.empty()) {
        err = "'" + path + "' row " + std::to_string(lineNo) + " column '" + col + "' is empty";
        return false;
    }
    out = cell;
    return true;
}

} // namespace

// The pinned type order (addendum Part A). Index == enumerator value.
static const char* const kTypeNames[UNIT_TYPE_COUNT] = { "Infantry", "Tank", "Artillery", "Recon" };

const char* unitTypeName(UnitType t) {
    const int i = static_cast<int>(t);
    return (i >= 0 && i < UNIT_TYPE_COUNT) ? kTypeNames[i] : "?";
}

bool parseUnitType(const std::string& name, UnitType& out) {
    for (int i = 0; i < UNIT_TYPE_COUNT; ++i) {
        if (name == kTypeNames[i]) { out = static_cast<UnitType>(i); return true; }
    }
    return false;
}

bool loadUnits(const std::string& path, std::vector<UnitDef>& out, std::string& err) {
    Row header;
    std::vector<Row> rows;
    if (!readCsv(path, header, rows, err)) return false;
    // MoveClass is NOT here: it is reserved on Q2 (data_spec.md), so it is neither
    // required nor read, and adding it to the CSV early is a no-op.
    const std::vector<std::string> needed = {
        "Id", "HP", "Move", "Atk", "Def", "RangeMin", "RangeMax", "CostFame", "Type", "CanCapture"
    };
    if (!requireColumns(header, needed, path, err)) return false;

    std::vector<UnitDef> parsed;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::size_t lineNo = i + 2;   // 1-based, header is line 1
        UnitDef u;
        std::string typeName;
        if (!readText(rows[i], header, "Id",        path, lineNo, u.id,       err)) return false;
        if (!readInt (rows[i], header, "HP",        path, lineNo, u.hpMax,    err)) return false;
        if (!readInt (rows[i], header, "Move",      path, lineNo, u.move,     err)) return false;
        if (!readInt (rows[i], header, "Atk",       path, lineNo, u.atk,      err)) return false;
        if (!readInt (rows[i], header, "Def",       path, lineNo, u.def,      err)) return false;
        if (!readInt (rows[i], header, "RangeMin",  path, lineNo, u.rangeMin, err)) return false;
        if (!readInt (rows[i], header, "RangeMax",  path, lineNo, u.rangeMax, err)) return false;
        if (!readInt (rows[i], header, "CostFame",  path, lineNo, u.costFame, err)) return false;
        if (!readText(rows[i], header, "Type",      path, lineNo, typeName,   err)) return false;
        if (!readBool(rows[i], header, "CanCapture",path, lineNo, u.canCapture, err)) return false;
        if (!parseUnitType(typeName, u.type)) {
            err = "'" + path + "' row " + std::to_string(lineNo) + " column 'Type': '" +
                  typeName + "' is not one of the four pinned unit types";
            return false;
        }
        parsed.push_back(u);
    }
    out.swap(parsed);
    return true;
}

bool loadTerrain(const std::string& path, std::vector<TerrainDef>& out, std::string& err) {
    Row header;
    std::vector<Row> rows;
    if (!readCsv(path, header, rows, err)) return false;
    const std::vector<std::string> needed = {
        "Id", "MoveCost", "DefensePct", "PassLand", "PassAir", "PassSea",
        "Capturable", "IncomeFame", "IsSpawnPoint", "IsRepairPoint"
    };
    if (!requireColumns(header, needed, path, err)) return false;

    std::vector<TerrainDef> parsed;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::size_t lineNo = i + 2;
        TerrainDef t;
        if (!readText(rows[i], header, "Id",           path, lineNo, t.id,           err)) return false;
        if (!readInt (rows[i], header, "MoveCost",     path, lineNo, t.moveCost,     err)) return false;
        if (!readInt (rows[i], header, "DefensePct",   path, lineNo, t.defensePct,   err)) return false;
        if (!readBool(rows[i], header, "PassLand",     path, lineNo, t.passLand,     err)) return false;
        if (!readBool(rows[i], header, "PassAir",      path, lineNo, t.passAir,      err)) return false;
        if (!readBool(rows[i], header, "PassSea",      path, lineNo, t.passSea,      err)) return false;
        if (!readBool(rows[i], header, "Capturable",   path, lineNo, t.capturable,   err)) return false;
        if (!readInt (rows[i], header, "IncomeFame",   path, lineNo, t.incomeFame,   err)) return false;
        if (!readBool(rows[i], header, "IsSpawnPoint", path, lineNo, t.isSpawnPoint, err)) return false;
        if (!readBool(rows[i], header, "IsRepairPoint",path, lineNo, t.isRepairPoint,err)) return false;
        parsed.push_back(t);
    }
    out.swap(parsed);
    return true;
}

bool loadEffectiveness(const std::string& path, double out[UNIT_TYPE_COUNT][UNIT_TYPE_COUNT],
                       std::string& err) {
    Row header;
    std::vector<Row> rows;
    if (!readCsv(path, header, rows, err)) return false;

    // Header: a label cell, then the four defender types IN THE PINNED ORDER. Asserting
    // the order here is what stops a reordered file becoming a transposed matrix.
    if (header.size() != UNIT_TYPE_COUNT + 1) {
        err = "'" + path + "' header has " + std::to_string(header.size()) +
              " cells; expected " + std::to_string(UNIT_TYPE_COUNT + 1);
        return false;
    }
    for (int c = 0; c < UNIT_TYPE_COUNT; ++c) {
        if (header[c + 1] != kTypeNames[c]) {
            err = "'" + path + "' header column " + std::to_string(c + 1) + " is '" +
                  header[c + 1] + "'; the pinned order requires '" + kTypeNames[c] + "'";
            return false;
        }
    }
    if (rows.size() != UNIT_TYPE_COUNT) {
        err = "'" + path + "' has " + std::to_string(rows.size()) +
              " data rows; expected " + std::to_string(UNIT_TYPE_COUNT);
        return false;
    }

    double parsed[UNIT_TYPE_COUNT][UNIT_TYPE_COUNT];
    for (int r = 0; r < UNIT_TYPE_COUNT; ++r) {
        const std::size_t lineNo = static_cast<std::size_t>(r) + 2;
        if (rows[r].size() != UNIT_TYPE_COUNT + 1) {
            err = "'" + path + "' row " + std::to_string(lineNo) + " has " +
                  std::to_string(rows[r].size()) + " cells; expected " +
                  std::to_string(UNIT_TYPE_COUNT + 1);
            return false;
        }
        if (rows[r][0] != kTypeNames[r]) {
            err = "'" + path + "' row " + std::to_string(lineNo) + " is '" + rows[r][0] +
                  "'; the pinned order requires '" + kTypeNames[r] + "'";
            return false;
        }
        for (int c = 0; c < UNIT_TYPE_COUNT; ++c) {
            if (!parseDouble(rows[r][c + 1], parsed[r][c])) {
                err = "'" + path + "' row " + std::to_string(lineNo) + " cell " +
                      std::to_string(c + 1) + ": '" + rows[r][c + 1] + "' is not a number";
                return false;
            }
        }
    }
    for (int r = 0; r < UNIT_TYPE_COUNT; ++r)
        for (int c = 0; c < UNIT_TYPE_COUNT; ++c)
            out[r][c] = parsed[r][c];
    return true;
}

const UnitDef* findUnit(const std::vector<UnitDef>& units, const std::string& id) {
    for (const UnitDef& u : units) if (u.id == id) return &u;
    return nullptr;
}

const TerrainDef* findTerrain(const std::vector<TerrainDef>& terrain, const std::string& id) {
    for (const TerrainDef& t : terrain) if (t.id == id) return &t;
    return nullptr;
}

} // namespace strat
