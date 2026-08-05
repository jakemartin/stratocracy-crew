// Stratocracy — the Balance Analyst's self-play log producer, PART (c) of §4.11 row 10
// (GDD §4.10, §4.4 week 4). Zero engine dependencies. No RNG, no clock, no I/O.
//
// WHAT THIS EXISTS FOR. T-SAVE-07 asserts "a Balance Analyst self-play log validates and
// replays as a save file -- one format, no dialect drift". Before this module the repo
// had no producer of such a log at any scope: `cpp_reference/selfplay.cpp` is a
// combat-only 1v1 duel harness over Combat.h that prints a table of duel outcomes and
// opens no file, so the ID had a subject in the GDD and none in the code.
//
// WHY IT IS A THIRD MODULE FOR ONE LEDGER ROW. §4.11 gives row 10 three parts with three
// dependency sets, and each part's set is encoded as its own row's link set in
// crew/tools.py so that contradicting one fails at the gate rather than in prose. Part
// (a) has no dependencies, part (b) needs rows 1-3 plus row 7's structural half, and
// part (c) is where rows 4, 5 and 6 arrive: the AI is row 6, and a match that runs to a
// result is rows 4 and 5. Folding this into `Replay.h` would put row 6 inside part (b)'s
// link set and silently falsify its cell, the same argument that made part (b) a
// separate module from part (a).
//
// THIS MODULE DECIDES NOTHING. It chooses no move -- row 6's `nextCommand` does -- and
// applies no rule: every command goes through `Replay.h::applyCommand`, which forwards
// to the module that owns the rule. What this module owns is the TRANSLATION between
// row 6's command vocabulary and §4.9's, and the discipline that only an ACCEPTED
// command enters the log.
//
// A SELF-PLAY LOG CARRIES NO `Capture` COMMAND, and that is the AI's design rather than
// a gap here. Ai.h says so in as many words: capture is a turn-boundary event the caller
// runs alongside income, and the AI's part of §2.9's capture behaviour is the MOVE onto
// the objective (T-AI-03). `AiCommandKind` therefore has four members and §4.9 has five.
// Part (b)'s hand-authored log is where the complete §4.9 set was exercised
// (`test_replay.cpp`); T-SAVE-07 asserts format compatibility, not command coverage, so
// the four kinds a self-play match can emit are its whole written fixture set and Q29 is
// satisfied over that set. The gate states this rather than leaving it to be inferred.
//
// WHY IT IS NAMED `Balance` AND NOT `Selfplay`. `cpp_reference/selfplay.cpp` is a
// tracked file that `crew/tools.py::ensure_workspace` copies into `build/` on every gate
// call, and the filesystem this project builds on is CASE-INSENSITIVE -- so a
// `Selfplay.cpp` authored beside it is the same file, and the duel harness's `main`
// silently replaced this module. §4.9 names `selfplay.cpp` in the enumeration of what is
// deliberately not vendored, so renaming THAT file would falsify a merged sentence; this
// one is renamed instead. `Balance` is also the name the document already uses for the
// role that owns the artifact: §3's Balance Analyst, and §4.4's week-4 "self-play balance
// sims".
//
// THE TWO HELPERS BELOW ALSO EXIST AS FILE-STATICS IN `test_replay.cpp`, and are left
// there deliberately. Deleting them in favour of these would add `Balance.cpp` to the
// `replay` row's link set and put a part-(c) dependency inside part (b)'s claim. The
// duplication is between a module and a test's private copy, it is of a TRANSLATION and
// not of a rule any module owns, and converging them is filed as a change request in
// spec/balance_spec.md rather than done here.
//
// See spec/balance_spec.md.
#pragma once

#include <string>
#include <vector>

#include "Ai.h"
#include "Replay.h"
#include "Save.h"

namespace strat {

// The AI's view of the authoritative state (§4.9). A projection, never a second source
// of truth: every field is copied out of `g` or out of the borrowed tables, and nothing
// reads back.
AiState aiViewOf(const GameState& g, const std::vector<UnitDef>& ud,
                 const std::vector<TerrainDef>& td, const std::vector<int>& buildlist);

// One `AiCommand` in the §4.9 vocabulary the log records. Returns false when the command
// has no §4.9 spelling, which is a refusal to write a log entry rather than a guess.
//
// Attack is tagged by TARGET HEX -- §4.9 spells it `Attack{unit, targetHex}` -- so the
// target's id is resolved against `g` here and never stored as an id.
bool aiCommandToSave(const GameState& g, const AiCommand& a, int turn, int side,
                     SaveCommand& out);

// Why a self-play run stopped. `Ended` is the only outcome T-SAVE-07 runs on; the other
// three are named so a failure cannot be read as a short match.
enum class SelfPlayStop {
    Ended,          // the rules ended the match; `result` carries the tier
    Guard,          // the command budget ran out with the match still running
    Untranslatable, // the AI emitted a command with no §4.9 spelling
    Refused,        // the rules refused a command the AI emitted
};

struct SelfPlayResult {
    SelfPlayStop             stop = SelfPlayStop::Guard;
    std::string              reason;      // empty unless stop is Refused/Untranslatable
    std::vector<SaveCommand> commandLog;  // ACCEPTED commands only, in order
    GameState                final;       // the state the log's last command produced
    MatchResult              result;      // `final.turn.result`, copied for the caller
};

// Plays an AI-vs-AI match to its end over `g`, which the caller has already started
// (`initMatch`) and opened (`openTurn`) -- the same precondition `replayLog` has, so the
// producing run and the replaying run begin at the same moment.
//
// `g` is taken BY VALUE. The caller keeps its initial state untouched, which is what lets
// the gate replay the log from that same state rather than from a state this function
// has already advanced.
//
// `maxCommands` bounds the loop so a misbehaving AI cannot hang the gate. Exhausting it
// is `SelfPlayStop::Guard` and is a failure, never a short log.
SelfPlayResult playSelfPlay(GameState g, const RulesTables& t,
                            const std::vector<UnitDef>& ud,
                            const std::vector<TerrainDef>& td,
                            const std::vector<int>& buildlist,
                            int maxCommands);

// The Balance Analyst's artifact: the run's log in a §4.10 save. Every header field is
// SUPPLIED by the caller, exactly as part (a) requires -- this module recomputes no
// header value and hashes only the final state, which is the one thing it holds.
//
// `result` is written as §2.8's tier name when the match ended and left NULL otherwise,
// which is what §4.10's "string/null" type column allows.
Save selfPlaySave(const SelfPlayResult& r, const SaveHeaderExpectation& expect,
                  const std::string& scenarioId);

} // namespace strat
