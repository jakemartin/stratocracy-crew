// Stratocracy — headless replayer + canonical state hash, PART (b) of §4.11 row 10
// (GDD §4.10). Zero engine dependencies. No RNG, no clock, no I/O.
//
// WHY THIS IS A SEPARATE MODULE FROM Save.h. §4.11 gives row 10 three parts with
// three dependency sets, and part (a) has NO DEPENDENCIES AT ALL -- a claim encoded
// as the `save` row's link set in crew/tools.py (`Save.cpp`, `Hex.cpp`,
// `test_save.cpp`). Part (b) needs rows 1-5 plus row 7's structural half. Putting
// the replayer in Save.h would widen that link set and silently falsify part (a)'s
// cell; a second module with its own row keeps BOTH dependency claims encoded, so
// contradicting either one fails loudly at the gate rather than in prose.
//
// WHAT PART (b) DEFINES THAT PART (a) DEFERRED. Save.h carries `stateHash` as an
// OPAQUE required string and says so in as many words. This module is where §4.10's
// canonical state hash is defined, and it is the FIRST and only definition of it.
// It is NOT the `stateHash` in Driver.h, which is the debug driver's own digest
// (GATE-DRV-06) over the driver's own Session and is a different thing -- this
// module does not link the driver and the driver does not link this.
//
// `strat::GameState` IS DECLARED HERE, AND THE DOCUMENT ALREADY NAMED IT. §4.9 calls
// it "the authoritative `strat::GameState`", §4.10 defines the hash over it, and
// T-UI-05's own invariant text asserts the snapshot's mirrors against it. No such
// type existed in this repo until this commit: the name was written into the GDD
// before anything realised it. Declaring it here does not amend any invariant's
// text, so no closure re-dates.
//
// IT IS A FOURTH COMPOSITION, NOT A SECOND SOURCE OF TRUTH. `AiState` (row 6),
// `UiWorld` (row 8) and the driver's `Session` are already three separate bundles
// over the same module-owned state, and the project does not treat that as
// duplication because NONE OF THEM OWNS A RULE -- which is what GATE-DRV-05 is
// actually about. GameState is the one the GDD calls authoritative; that the other
// three do not yet read from it is filed as a change request and is not fixed here.
//
// A MODULE TAKES WHAT IT DOES NOT OWN AS AN ARGUMENT -- the rule that let row 4 land
// before row 5. So GameState holds the mutable state the rules modules own (board,
// units, economy, turn) and NOT the §4.8 tables or the Stub-7 scenario: §4.9 names
// those as three separate module-side sources, and the tables arrive here as
// `RulesTables`, borrowed and const.
//
// See spec/replay_spec.md.
#pragma once

#include <string>
#include <vector>

#include "Combat.h"
#include "Data.h"
#include "Economy.h"
#include "Hex.h"
#include "Move.h"
#include "Save.h"
#include "Scenario.h"
#include "Turn.h"

namespace strat {

// A unit on the authoritative board. `defIndex` indexes the loaded UnitDef table, so
// no stat is ever copied here -- every one is looked up, the DriverUnit discipline.
struct GameUnit {
    int id       = 0;
    int side     = 0;
    int defIndex = 0;
    Hex hex;
    int hp       = 0;
    // Where this unit was DEPLOYED, which stops being `hex` the moment it moves.
    // NOT hashed: it is scenario data pinned by `scenarioHash`, not match state.
    Hex placement;
};

// The authoritative composed state (§4.9). Every member is state a rules module owns
// and this struct borrows nothing mutable from anywhere else.
struct GameState {
    MapBounds             bounds;
    std::vector<int>      terrain;    // offset-indexed, into the TerrainDef table
    std::vector<GameUnit> units;
    EconomyState          economy;
    TurnState             turn;
    int                   nextUnitId = 1;
    // Which unit is the flag, per side, from Stub 7's `isFlag` placement field; -1
    // means this side designates none, so flag death is unreachable rather than
    // assumed (Q10 stays open on exactness). It is held HERE and not as a bool on
    // GameUnit because THE DESIGNATION MUST OUTLIVE THE UNIT: a dead flag is simply
    // absent from `units`, and a per-unit bool cannot then tell "the flag died" from
    // "this side never had one" -- which are opposite verdicts at §2.8.
    int                   flagUnit[SIDE_COUNT] = {-1, -1};
};

// Per-unit flag status, DERIVED from the designation above. §4.10 hashes it as a
// per-unit value, in the same way it hashes `hasMoved`/`hasActed` -- per-unit FACTS
// that row 5 holds as id sets rather than as fields on the unit.
bool isFlagUnit(const GameState& g, const GameUnit& u);

// The §4.8 tables, borrowed. Never owned, never copied, never recomputed here.
struct RulesTables {
    const std::vector<UnitDef>*    units   = nullptr;
    const std::vector<TerrainDef>* terrain = nullptr;
};

// Occupancy is DERIVED from `units` on every call rather than stored beside them --
// one source for where a unit is. Row 3 takes a Board as an argument for the same
// reason row 4 takes the turn number as one.
Board boardOf(const GameState& g);

const GameUnit* findGameUnit(const GameState& g, int id);

// ---------------------------------------------------------------------------
// §4.10's CANONICAL STATE HASH. Defined once, here.
//
// Serialised in a fixed field order, every value an integer, every collection walked
// in canonical hex order (T-HEX-07) rather than storage order, then the bytes hashed.
// The field groups are the ones the MODULES hold, which is not the grouping §4.10
// carried before this revision:
//
//   turn counter; side to move
//   per side          : fameTotal, fameCombat
//   objective ownership: {hex, owner}                    <- EconomyState::objectives
//   per unit          : {id, side, hex, hp, isFlag, hasMoved, hasActed}
//   per tile          : capture progress {hex, unitId, turnsHeld}
//                                                        <- EconomyState::captures
//   per factory       : build allowance {hex}            <- TurnState::builtThisTurn
//   pending builds    : {factoryHex, side, defIndex}     <- EconomyState::pending
//
// TWO OF THOSE GROUPS MOVED, and the modules are why. `captureProgress` is held by
// the TILE and names the unit that accumulated it -- Economy.h says so in as many
// words, and that is the whole content of Q4/T-FAME-05's "progress can never
// transfer". `pendingBuilds` is keyed by `factoryHex`. Neither is a per-unit field,
// and hashing them as per-unit ones would have required a projection that contradicts
// the rule they encode.
//
// `buildWaiting` IS NOT HASHED, on §4.10's own omission rule: it is exactly "a
// pending build stands at this factory", recomputable from the pending-build group
// above, so it can add no distinction and only one more way for two builds of one
// state to disagree. `spawnBlocked` is omitted for the same stated reason.
//
// The four turn and build facts are written as 0 or 1 BECAUSE A SAVE IS ACCEPTED
// MID-TURN: at hash time a unit may have spent one of its two flags and a factory may
// have taken its build, so a hash without them is identical across states the rules
// distinguish, and T-INT-02 and T-SAVE-06 would agree over that difference rather
// than catch it.
// ---------------------------------------------------------------------------

// The exact byte stream the hash is taken over. Exposed so a check can assert the
// SERIALISATION rather than only the digest -- a digest comparison cannot say which
// field diverged, and a test that can only compare two digests is a test that agrees
// with whatever this file does.
std::string canonicalStateBytes(const GameState& g);

// FNV-1a 64 over `canonicalStateBytes`, lowercase hex, 16 characters. Every input
// byte is ASCII decimal or a separator, so the digest is platform-stable by
// construction -- which is the property T-INT-02/T-SAVE-06 exist to prove across
// two compiled worlds rather than to assume.
std::string canonicalStateHash(const GameState& g);

// ---------------------------------------------------------------------------
// THE REPLAYER
// ---------------------------------------------------------------------------

// The START-OF-TURN MOMENT, over GameState: `beginTurn`, then start-of-turn repair,
// then income, then the capture tick. Row 5 defines WHEN and row 4's own calls do the
// work; the ORDER is the Director's ruling of 2026-08-03 -- the tick runs AFTER
// income, so an objective whose capture completes at the start of turn T pays its new
// owner from T+1.
//
// A REPLAY THAT SKIPS THIS DIVERGES FROM THE MATCH IT REPLAYS, silently and only
// after the first turn boundary, which is precisely the class of defect T-SAVE-01 and
// T-SAVE-02 exist to catch. It is called here on `EndTurn` and must be called once
// after `initMatch` to open the first turn.
//
// THIS IS THE SECOND IMPLEMENTATION OF THAT SEQUENCE IN THIS REPO. Driver.good.cpp's
// `openActiveTurn` is the older sibling, over the driver's own `Session`. That is a
// genuine duplication of an ORDER THE DIRECTOR RULED, not of a rule any module owns,
// and it is FILED AS A CHANGE REQUEST rather than hidden: the two should converge on
// one implementation over `GameState` once the driver reads from it.
void openTurn(GameState& g, const RulesTables& t);

// THE ONLY Scenario -> GameState mapping in the project, and it is here because
// §4.9 part 2's bridge and the headless fixture must produce the SAME seed. A seed
// re-implemented on the engine side is exactly the divergence T-INT-02 exists to
// catch: both worlds would pass their own tests and disagree on the hash. Driver's
// `installScenario` is the older sibling over `Session`; that the two do not yet
// share one implementation is the same convergence change request already filed
// above for `openTurn`/`openActiveTurn`, and it is not fixed here.
//
// Builds aside and assigns to `g` only on success, so a refusal leaves the caller's
// state untouched. On success the match is initialised and the FIRST TURN IS OPEN --
// the same state a real match reaches, never a half-built one.
//
// `firstSide` IS A PARAMETER AND NOT A DEFAULT because no rule in this project
// decides it: Stub 7's scenario file carries no such field and §2 names no rule, so
// the module refuses to invent one. Filed as a change request for the Director. Both
// callers pass the same value; if they ever disagree the hashes diverge and T-INT-02
// reports it, which is the check doing its job rather than a silent drift.
bool seedFromScenario(GameState& g, const Scenario& sc, const RulesTables& t,
                      int firstSide, std::string& err);

struct ReplayResult {
    bool        ok        = false;
    std::string failedId;      // "T-SAVE-05" on an illegal command; empty on success
    std::string reason;        // empty on success
    int         failedIndex = -1;   // index into the log; -1 on success
    int         applied     = 0;    // commands applied (== log size on success)
};

// Applies ONE command through the module that owns the rule -- Move.h for a move,
// Combat.h/Economy.h for an attack, Economy.h+Turn.h for a build, Economy.h for a
// capture, Turn.h for the turn boundary. This module decides NOTHING: every refusal
// below is a refusal some other module returned, forwarded with its own reason.
ReplayResult applyCommand(GameState& g, const SaveCommand& c, const RulesTables& t);

// Applies the whole log ALL-OR-NOTHING (T-SAVE-05). Works on a copy and assigns to
// `g` only after the last command succeeds, so an illegal command at index k leaves
// the caller's pre-load state byte-identical -- the tripwire that stub names is an
// implementation that applies then validates.
ReplayResult replayLog(GameState& g, const std::vector<SaveCommand>& log,
                       const RulesTables& t);

} // namespace strat
