// Stratocracy — debug-command driver (§4.4 week 1, "Playable via debug commands").
// Zero engine dependencies. Contains NO RULES: every rule decision delegates to
// Hex.h, Data.h, Move.h, Combat.h, Economy.h or Turn.h. See spec/driver_spec.md.
#pragma once

#include <string>
#include <vector>

#include "Combat.h"
#include "Data.h"
#include "Economy.h"
#include "Hex.h"
#include "Move.h"
#include "Turn.h"

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
    // call into Economy.h. `turnNumber` is the debug setter it has always been --
    // the sandbox's turn number when NO match is running. Once `match` starts one,
    // Turn.h owns the number and the setter is refused.
    EconomyState economy;
    int turnNumber = 1;

    // Row 5. Alternation, per-unit act flags, the start-of-turn repair moment and
    // §2.8's result all live in Turn.h; the driver reads them and enforces nothing
    // of its own. Before `match` runs, `match.running` is false and the session is
    // the same free sandbox it was at 647d4df -- which is why GATE-DRV-01..07 are
    // untouched by row 5.
    TurnState match;

    // A DEBUG DESIGNATION standing in for Stub 7's `isFlag` placement field (row 7,
    // unbuilt; Q10 open on exactness). The human names the flag unit; the driver
    // never picks one. -1 means this side has no flag designated, and flag death is
    // then simply unreachable rather than assumed.
    int flagUnit[SIDE_COUNT] = {-1, -1};
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

// The turn number in force: Turn.h's while a match runs, the debug setter otherwise.
int currentTurn(const Session& s);

// The §2.8 facts, COMPOSED from the modules that own them -- objective ownership and
// combat Fame from Economy.h, factory-ness from the terrain table, surviving HP from
// the units on the board, flag status from the debug designation above. Nothing here
// is decided; it is gathered.
BoardSnapshot snapshotOf(const Session& s);

const DriverUnit* findUnitById(const Session& s, int id);

} // namespace strat
