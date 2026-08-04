// Stratocracy — debug-command driver (§4.4 week 1, "Playable via debug commands").
// Zero engine dependencies. Contains NO RULES: every rule decision delegates to
// Hex.h, Data.h, Move.h, Combat.h, Economy.h, Turn.h, Ai.h or Scenario.h. See
// spec/driver_spec.md.
#pragma once

#include <string>
#include <vector>

#include "Ai.h"
#include "Combat.h"
#include "Data.h"
#include "Economy.h"
#include "Hex.h"
#include "Move.h"
#include "Scenario.h"
#include "Turn.h"
#include "Ui.h"

namespace strat {

// A unit on the debug board. `defIndex` indexes the loaded UnitDef table, so the
// driver never holds a copy of a stat -- it looks every one of them up.
struct DriverUnit {
    int id       = 0;
    int side     = 0;      // 0 or 1. No turn owns a side; see spec/driver_spec.md.
    int defIndex = 0;
    Hex hex;
    int hp       = 0;
    // Where this unit was DEPLOYED, which stops being `hex` the moment it moves.
    // Stub 8's `isGuidedMarked` is a property of the placement rather than of the
    // current hex, so the snapshot cannot derive it without this. A unit that was
    // never deployed from a scenario -- one the human placed with `unit`, or one a
    // factory spawned -- records the hex it appeared on, which no guidedOpening
    // entry names, so it is simply unmarked.
    Hex placement;
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

    // Which unit is the flag. On a BUILT-IN fixture this is a debug designation the
    // human writes with `flag <side> <id>`; the driver never picks one, and -1 means
    // this side has no flag, so flag death is unreachable rather than assumed. On a
    // scenario loaded from a file it is set from Stub 7's `isFlag` placement field,
    // which T-SCN-01 has already checked is exactly one Tank per side. Q10 stays open
    // on exactness either way.
    int flagUnit[SIDE_COUNT] = {-1, -1};

    // The per-factory build record MOVED TO ROW 5 (TurnState::builtThisTurn) when
    // T-TURN-10 was minted. It was driver bookkeeping here, read by the AI and
    // gating no player command, which is exactly why a second player build at one
    // factory was accepted. It is now a rule Turn.h owns and enforces for player and
    // AI alike; the driver reads it off `match` and keeps no copy.

    // The AI's buildlist (§2.9), as defIndexes. Empty until `ai buildlist` sets it
    // or a fixture supplies one; the driver invents no ratio.
    std::vector<int> buildlist;

    // Row 7. The scenario `scenario load` installed, and the verdict that installed
    // it. Held so `scenario report` can reprint the MEASURED lane integers without
    // re-pricing anything, and so `match <firstSide>` can read the cap off the file
    // rather than off a literal (Q7). False until a file is loaded; a built-in
    // fixture clears it, because a fixture is not a scenario.
    bool               scenarioLoaded = false;
    Scenario           scenario;
    ScenarioLoadResult scenarioReport;
};

// Loads data/units.csv, data/terrain.csv and data/effectiveness.csv. False on any
// defect, with the loader's reason in `err` (§4.8: never a silent default).
bool sessionInit(Session& s, const std::string& dataDir, std::string& err);

// Built-in boards -- hand-built terrain with no ownership, no starting force and no
// guided opening. A SCENARIO FILE is the other way onto a board (`scenario load`),
// and it is Scenario.h that parses and validates it; the driver installs what that
// module returns and refuses whatever it refuses.
std::vector<std::string> fixtureNames();
bool loadFixture(Session& s, const std::string& name, std::string& err);

// Installs an ALREADY-VALIDATED scenario as the session board: terrain, placements,
// the `isFlag` designation, objective ownership and starting Fame, each landing in
// the module that owns it. Decides nothing -- false and `err` if the loaded tables
// cannot resolve an Id, and the session is left untouched.
bool installScenario(Session& s, const Scenario& sc, std::string& err);

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

// Everything §4.7 Stub 8's contract reads, COMPOSED from the modules that own it --
// the same gathering `snapshotOf` does for §2.8's facts. The returned UiWorld borrows
// the session's tables and module states, so it must not outlive `s`. The driver
// decides nothing here either: `buildUiSnapshot` projects it and adds nothing, which
// is what GATE-DRV-12 asserts.
UiWorld uiWorldOf(const Session& s);

const DriverUnit* findUnitById(const Session& s, int id);

} // namespace strat
