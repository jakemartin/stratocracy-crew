// Stratocracy — headless save & replay format, PART (a) (GDD §4.10, §4.11 row 10).
// Zero engine dependencies. Pure parse + serialize + header comparison: no RNG, no
// clock, and no I/O beyond reading the one file.
//
// PART (a) ONLY, and the boundary is the whole point of the row's split. §4.11 gives
// row 10 three parts with three dependency sets; this one has NO DEPENDENCIES AT ALL
// and T-SAVE-04 closes on it alone, "since it never applies a command". So:
//
//   - NO COMMAND IS APPLIED. The log is parsed STRUCTURALLY. Nothing here asks whether
//     a Move is reachable, whether a side owns a unit, or whether the turns ascend.
//     That is part (b), the headless replayer, and it is where T-SAVE-01/02/03/05 run.
//   - THE CANONICAL STATE HASH IS NOT DEFINED HERE. §4.10 defines it over `GameState`
//     and its first consumer is part (b). `stateHash` below is an OPAQUE STRING:
//     required, string-typed, carried, re-emitted, never examined. It is NOT the
//     `stateHash` in Driver.h, which is the debug driver's own digest (GATE-DRV-06)
//     and a different thing -- this module does not link the driver.
//   - NOTHING IS RECOMPUTED. `dataHash` and `scenarioHash` are compared as strings the
//     CALLER SUPPLIES. Recomputing either would import row 2 or row 7 as a dependency,
//     which is precisely what §4.11 says part (a) does not have. A module takes what
//     it does not own as an argument -- the rule that let row 4 land before row 5.
//
// NO THIRD-PARTY JSON. The parser is written in Save.good.cpp and REFUSES malformed
// input rather than tolerating it -- the posture Scenario.h and Data.h both take.
// It is this module's OWN parser and not row 7's: Scenario.good.cpp keeps its JSON
// layer in an anonymous namespace (internal linkage, deliberately unexported), so
// there is nothing to reuse without reopening row 7 and taking on the dependency.
//
// PARSED STATE HOLDS AXIAL ONLY. Hexes are authored odd-r `[col, row]`, converted at
// parse time and never converted back except for printing -- the T-SCN-05 posture.
//
// See spec/save_spec.md. THE SUITE IS DELIBERATELY PARTIAL: six of row 10's seven
// acceptance IDs do not run here and test_save.cpp names each with its reason.
// T-SAVE-01/02/03/05 need the replayer, which has SINCE LANDED as part (b) and closes
// all four in `test_replay.cpp`; T-SAVE-06 is in-editor (†) and now waits on the editor
// pass alone, its other blocker having been the hash part (b) defines; T-SAVE-07 needs
// row 6's self-play logs. Row 10 is a PROPOSED ledger row and has none to flip.
//
// EIGHT STATED READINGS live in spec/save_spec.md rather than here, because this time
// the spec is the shorter document. The two that change what this header declares:
// the refusal set is exactly §4.10's four fields and `scenarioId` is NOT one of them;
// and `null` is a legal value for `result` ALONE, because §4.10's own type column
// reads "string/null".
#pragma once

#include <string>
#include <vector>

#include "Hex.h"

namespace strat {

// The five commands of §4.9, and no others. `Move{unit, destHex}`,
// `Attack{unit, targetHex}`, `Build{factoryHex, unitId}`, `Capture{unit}`, `EndTurn{}`.
enum class SaveCommandKind { Move, Attack, Build, Capture, EndTurn };

// One entry of the command log. §4.10 tags every entry `{turn, side}`; the rest of the
// fields are exactly the ones §4.9 names for that kind, and carrying another kind's
// field is a REFUSAL rather than a tolerated extra.
struct SaveCommand {
    int             turn = 0;
    int             side = 0;
    SaveCommandKind kind = SaveCommandKind::EndTurn;

    int unitId = 0;    // Move, Attack, Capture: the acting unit. Build: the unit BUILT.
    Hex hex;           // Move: destHex. Attack: targetHex. Build: factoryHex.
    bool hasHex = false;   // false for Capture and EndTurn, which name no hex
    bool hasUnit = false;  // false for EndTurn alone
};

// The header §4.10's file-layout table defines, plus the log. Field order here is the
// table's order, and serializeSave emits in this order so a round trip is byte-stable.
struct Save {
    int         formatVersion = 0;
    std::string rulesCommit;
    std::string dataHash;
    std::string scenarioId;
    std::string scenarioHash;
    int         seed = 0;             // reserved; MUST be 0 -- no RNG ships
    std::vector<SaveCommand> commandLog;
    std::string stateHash;            // OPAQUE at part (a); see the header comment
    std::string result;               // empty when `result` was null
    bool        hasResult = false;    // false == the file carried `null`
};

// What the caller believes is currently in effect. Every field is SUPPLIED, never
// recomputed here -- that is what keeps part (a)'s dependency set empty. `expectedVersion`
// is the version this build accepts; kFormatVersion below is what it writes.
struct SaveHeaderExpectation {
    int         expectedVersion = 0;
    std::string rulesCommit;
    std::string dataHash;
    std::string scenarioHash;
};

// The version THIS build writes and accepts. Reading 1: §4.10 versions the file and
// names no number, and this build defines the format.
constexpr int kFormatVersion = 1;

struct SaveLoadResult {
    bool        ok = false;
    std::string failedId;   // "T-SAVE-04", "GATE-SAVE-PARSE"; empty on success
    std::string reason;     // empty on success
};

// Parse alone: JSON, the field list, the schema. Asserts NO header agreement -- a file
// from a different rules commit parses fine and is refused later, by checkHeader.
// `origin` labels the source in reasons. Exposed so a caller can exercise malformed
// input without touching a disk.
//
// ON FAILURE `out` IS NOT MODIFIED (reading 6). Parsing fills a local and assigns only
// on success, which is the whole content of T-SAVE-04's "state untouched" clause at
// this part and the defect Save.buggy.cpp exists to have.
SaveLoadResult parseSave(const std::string& text, const std::string& origin, Save& out);

// The header comparison, on an ALREADY-PARSED save. Kept separate from the parse so a
// caller can mutate ONE header field of a loaded save and re-check it -- which is
// exactly what T-SAVE-04's four mismatch fixtures do, none of which is a file on disk.
//
// Refuses on any of the four fields §4.10's Version policy enumerates, and NOT on
// `scenarioId` (reading 2). Names the disagreeing field in the reason -- §4.10 requires
// "refuse load WITH A REASON", so a bare false would not satisfy the invariant.
SaveLoadResult checkHeader(const Save& s, const SaveHeaderExpectation& expect);

// Parse then check, the ordinary load path. The two verdicts stay distinct: a malformed
// file fails GATE-SAVE-PARSE and a well-formed disagreeing one fails T-SAVE-04, so a
// fixture cannot pass for the wrong reason (reading 7). `out` is untouched on either.
SaveLoadResult loadSave(const std::string& text, const std::string& origin,
                        const SaveHeaderExpectation& expect, Save& out);

// Emit the §4.10 layout, fields in the table's order. Total, deterministic, and the
// inverse of parseSave for every Save this module can produce.
std::string serializeSave(const Save& s);

// "kind" spellings, shared by the writer and the parser so the two cannot drift.
const char* saveCommandName(SaveCommandKind k);

} // namespace strat
