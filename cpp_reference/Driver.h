// Stratocracy — debug-command driver (§4.4 week 1, "Playable via debug commands").
// Zero engine dependencies. Contains NO RULES: every rule decision delegates to
// Hex.h, Data.h, Move.h, Combat.h, Economy.h or Turn.h. See spec/driver_spec.md.
#pragma once

#include <string>
#include <vector>

#include "Ai.h"
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

    // Row 6. Factories built at this turn, cleared at each turn start. §2.7's "one
    // build per factory per turn" is turn-scoped and Economy.h enforces only the
    // per-PENDING half, which was the whole of it while no module owned the turn --
    // see the change request in spec/ai_spec.md. Bookkeeping, not a rule: it is
    // handed to the AI as a board fact and gates no player command.
    std::vector<Hex> builtThisTurn;

    // The AI's buildlist (§2.9), as defIndexes. Empty until `ai buildlist` sets it
    // or a fixture supplies one; the driver invents no ratio.
    std::vector<int> buildlist;
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

// Everything the AI is allowed to see, composed from the same session a human
// drives. Nothing here is hidden state: the AI cheats at nothing (§4.7 Stub 6).
AiState aiStateOf(const Session& s);

// Renders one AI command as the driver command line a player would type. This is
// what makes T-AI-01 structural -- the AI's commands enter through `execute`, the
// same door a typed command uses, and are validated identically.
std::string renderAiCommand(const Session& s, const AiCommand& c);

// The §2.8 facts, COMPOSED from the modules that own them -- objective ownership and
// combat Fame from Economy.h, factory-ness from the terrain table, surviving HP from
// the units on the board, flag status from the debug designation above. Nothing here
// is decided; it is gathered.
BoardSnapshot snapshotOf(const Session& s);

const DriverUnit* findUnitById(const Session& s, int id);

} // namespace strat
