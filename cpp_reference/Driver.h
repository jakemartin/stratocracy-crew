// Stratocracy — debug-command driver (§4.4 week 1, "Playable via debug commands").
// Zero engine dependencies. Contains NO RULES: every rule decision delegates to
// Hex.h, Data.h, Move.h or Combat.h. See spec/driver_spec.md.
#pragma once

#include <string>
#include <vector>

#include "Combat.h"
#include "Data.h"
#include "Economy.h"
#include "Hex.h"
#include "Move.h"

namespace strat {

// A unit on the debug board. `defIndex` indexes the loaded UnitDef table, so the
// driver never holds a copy of a stat -- it looks every one of them up.
struct DriverUnit {
    int id       = 0;
    int side     = 0;      // 0 or 1. No turn owns a side; see spec/driver_spec.md.
    int defIndex = 0;
    Hex hex;
    int hp       = 0;
};

struct Session {
    MapBounds bounds;
    std::vector<int> terrain;          // offset-indexed, into terrainDefs
    std::vector<DriverUnit> units;
    std::vector<UnitDef>    unitDefs;
    std::vector<TerrainDef> terrainDefs;
    int nextUnitId = 1;
    bool loaded    = false;            // false until a fixture is loaded

    // Row 4. The driver still owns no rules: every economy transition below is a
    // call into Economy.h. `turnNumber` exists ONLY to feed T-FAME-02's no-accrual-
    // on-turn-1 argument -- it is not a turn loop, which is row 5's and unbuilt, so
    // nothing advances it but the explicit `turn` command.
    EconomyState economy;
    int turnNumber = 1;
};

// Loads data/units.csv, data/terrain.csv and data/effectiveness.csv. False on any
// defect, with the loader's reason in `err` (§4.8: never a silent default).
bool sessionInit(Session& s, const std::string& dataDir, std::string& err);

// Built-in boards. No file format is defined or read -- that is Stub 7's, unbuilt.
std::vector<std::string> fixtureNames();
bool loadFixture(Session& s, const std::string& name, std::string& err);

// Executes one command line, appending its output to `out`. Returns false only for
// `quit`. A refused command appends a reason and changes nothing (GATE-DRV-06).
bool execute(Session& s, const std::string& line, std::vector<std::string>& out);

// Order-independent digest of the session state, used by GATE-DRV-06 to assert that
// a refused command changed nothing. Units are visited in canonical hex order.
std::string stateHash(const Session& s);

const DriverUnit* findUnitById(const Session& s, int id);

} // namespace strat
