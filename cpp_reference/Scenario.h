// Stratocracy — headless scenario file & validator (GDD §4.7 Stub 7, §4.11 row 7).
// Zero engine dependencies. Pure parse + validation: no RNG, no clock, and no I/O
// beyond reading the one file. Depends on rows 1 (Hex), 2 (Data) and 3 (Move).
//
// NO THIRD-PARTY JSON. The parser below is written here and REFUSES malformed input
// rather than tolerating it, the same posture Data.h takes on the §4.8 CSVs.
//
// LOADED STATE HOLDS AXIAL ONLY (T-SCN-05). The authored file stores odd-r
// (col, row); every hex on `Scenario` is a `Hex`, converted at parse time and never
// converted back except for printing.
//
// See spec/scenario_spec.md. This suite is DELIBERATELY INCOMPLETE: the Director's
// scope ruling authors no scenario file for Longwater March (§2.13.5) or The Causeway
// (§2.13.6), so T-SCN-08 (a)/(b), T-SCN-09's asserting branch and T-SCN-11 (c) do not
// run. test_scenario.cpp names each one and its reason.
//
// ELEVEN STATED READINGS, the way spec/ai_spec.md records its five. Each is a
// DOCUMENTED CHOICE, not a rule: §4.7 Stub 7 requires a determinate answer and leaves
// the term undefined, and none of them adds a rule the GDD does not have.
//
//  1. `formatVersion` is 1. The stub versions the file and names no number; this
//     build defines the format, so this is its first version.
//  2. `scenarioId` is "ferrum_crossing" -- a "stable identifier", which the stub asks
//     for and does not supply. Taken from §2.13.2's map name, not invented for it.
//  3. `scenarioHash` is EXCLUDED FROM ITS OWN PREIMAGE. A value cannot hash itself,
//     and the stub's field order is the order of the preimage, not of the file.
//  4. `scenarioHash` is the one OPTIONAL field. It is derived, and a schema that
//     makes an author hand-compute a digest before their own file will load is a
//     contract no author can keep. Declared, it must match or the file is refused --
//     which is what makes the shipped map self-verifying against a transcription slip.
//  5. The digest is FNV-1a 64-bit over the preimage, 16 lowercase hex digits. The
//     stub says "hash" and names no algorithm; fixed-width unsigned arithmetic is
//     wrap-around by definition, so it is identical on every compiler.
//  6. A FACTORY is identified from the LOADED TABLE as `capturable && isSpawnPoint`,
//     never by the row name "Factory". §4.8 marks it both and Town neither, and §2.7
//     makes spawning a factory-only property, so a §2.3 rename cannot break the gate.
//  7. A capturable hex that `ownership` does not name is NEUTRAL (§2.7's "neutral
//     factories/towns unowned"). An `ownership` entry naming a hex nothing can capture
//     is a T-SCN-02 refusal -- the field is "the initial owner of each capturable hex".
//  8. T-SCN-11 with NO OPPOSING ROUTE is a PASS, reported as "no route". The invariant
//     requires the opposing route to cost strictly more; a route that does not exist
//     does. (The stub's fixture (c) prints "no route" as a reportable state.)
//  9. T-SCN-06's lane is priced on TERRAIN ALONE, the same as T-SCN-11's. Q21 rules
//     the opposing route terrain-only, §2.13.1 note 2 measures both halves of the
//     eight-route table that way, and pricing the two sides differently is the one
//     thing T-SCN-11 says would make its inequality mean nothing.
// 10. The parser is STRICT by default: unknown key, duplicate key, trailing comma,
//     non-integer number, `null`, `\u` escape, raw control character, trailing content
//     and an out-of-range `side` or `owner` are all refusals. Within one
//     formatVersion an unrecognized key is a typo, and tolerating it is how a file
//     silently loses a field it thought it declared.
// 11. Hex references are authored as `[col, row]`, and `startingFame` is keyed
//     `side0`/`side1`. The stub fixes the TYPES ("HEX references", "object; per
//     side") and not the spelling.
//
// TWO THINGS THE SCHEMA DOES NOT CARRY, noted rather than invented: §2.7's capture N
// (Q4 fixes it at 1 and Stub 7 lists no field for it) and §2.9's difficulty handicap,
// which the stub states is a match-setup parameter applied on top.
#pragma once

#include <string>
#include <vector>

#include "Data.h"
#include "Economy.h"   // OWNER_NEUTRAL, SIDE_COUNT — ownership is the same vocabulary
#include "Hex.h"
#include "Move.h"

namespace strat {

// The only format version this build reads. An unknown version is a hard load
// failure (§4.7 Stub 7: "unknown -> refuse load"), never a best-effort parse.
constexpr int SCENARIO_FORMAT_VERSION = 1;

// §4.7 Stub 7's enum, narrow by SCOPE (Q24/Q26 ruled): `rot180` or `none`, and
// nothing else. Absent or unrecognized in the file is a hard load failure -- a
// scenario that forgets to declare must not silently claim the weakest claim.
enum class Symmetry { None, Rot180 };

const char* symmetryName(Symmetry s);

struct ScenarioOwner {
    Hex hex;
    int owner = OWNER_NEUTRAL;   // 0, 1, or OWNER_NEUTRAL
};

// {side, unitId, hex, isFlag} (§4.7 Stub 7). `isFlag` is valid only on a Tank --
// §2.4 makes the flag "a designated Tank", so it is a placement field and not a
// fifth unit row (T-SCN-01).
struct ScenarioPlacement {
    int         side = 0;
    std::string unitId;
    Hex         hex;
    bool        isFlag = false;
};

// One seat's opening-capture lane (§2.13.1). Both members are HEX references: the
// deployment hex of that seat's marked Infantry (§2.11.6 turn 1a) and the neutral
// Factory hex it walks to. A hex identifies the placement uniquely because T-SCN-02
// already forbids two placements sharing one.
struct ScenarioGuided {
    int side = 0;
    Hex infantry;
    Hex objective;
};

// The loaded scenario. Field order here is the CANONICAL SERIALIZATION order that
// `scenarioHash` hashes; NEW FIELDS APPEND AT THE TAIL so adding one can never
// reorder an existing one. Serialization order is not validation order.
struct Scenario {
    int         formatVersion = 0;
    std::string scenarioId;
    MapBounds   bounds;                        // width/height (Q1: per-scenario data)
    std::vector<std::string>     terrainId;    // row-major == canonical hex order
    std::vector<ScenarioOwner>   ownership;
    std::vector<ScenarioPlacement> placements;
    int         startingFame[SIDE_COUNT] = {0, 0};
    int         turnCap = 0;
    std::vector<ScenarioGuided>  guided;
    Symmetry    symmetry = Symmetry::None;

    // The hash the FILE declared, if it declared one. Not part of its own preimage:
    // a value cannot hash itself. See scenarioHash below.
    bool        hasDeclaredHash = false;
    std::string declaredHash;
};

// One seat's measured lane, as T-SCN-08 and T-SCN-11 report it. Every integer here
// is a Stub-3 path cost under the T-MOVE-01 accounting -- cost counts EVERY HEX
// ENTERED INCLUDING THE OBJECTIVE -- so the two sides of T-SCN-11's inequality are
// priced identically and the validator cannot price one lane two ways.
struct ScenarioLane {
    int  side = 0;
    Hex  infantry;
    Hex  objective;

    // T-SCN-06 / T-SCN-08: the owning lane. Bridge-free, terrain alone.
    bool laneFound = false;
    int  laneCost  = 0;

    // T-SCN-11: the opposing seat's cheapest route to the SAME objective, minimised
    // over EVERY CanCapture-row unit that seat deploys (Q28) -- never over that
    // seat's `guidedOpening.infantry` alone. Bridges ARE permitted here (asymmetry
    // (ii)); terrain alone with occupancy excluded (Q21).
    bool opposingFound = false;      // false == "no route", which is not a failure
    int  opposingCost  = 0;
    Hex  opposingFrom;               // the hex that ACHIEVES the set minimum
};

// The verdict. A failure refuses the WHOLE FILE with a reason (Determinism), and the
// lanes measured before the refusal are still reported -- T-SCN-08's integers are the
// source of truth for §2.13.1's lane table, so an author reads a number rather than a
// boolean even when the file is refused.
struct ScenarioLoadResult {
    bool        ok = false;
    std::string failedId;    // "T-SCN-06", "GATE-SCN-PARSE", ... empty on success
    std::string reason;      // empty on success

    int captureMove = 0;     // Move of the single CanCapture row (T-DATA-03)
    int ceiling     = 0;     // 2 x captureMove -- DERIVED from the loaded table
    std::vector<ScenarioLane> lanes;   // in side order, not authoring order
};

// Parse + validate one scenario file. The only I/O in this module.
ScenarioLoadResult loadScenario(const std::string& path,
                                const std::vector<UnitDef>& units,
                                const std::vector<TerrainDef>& terrain,
                                Scenario& out);

// Parse alone: JSON, the field list, the format version, and the declared hash if the
// file carried one. Asserts NO invariant. `origin` labels the source in reasons.
// Exposed so a caller can exercise malformed input without touching a disk.
ScenarioLoadResult parseScenario(const std::string& text, const std::string& origin,
                                 Scenario& out);

// The invariants (T-SCN-01..09, T-SCN-11), on an already-parsed scenario. Kept
// separate from the parse so a caller can mutate ONE field of a loaded scenario and
// re-check it -- which is exactly what T-SCN-09's refusal branch and T-SCN-11's
// fixture (b) do, neither of which is a file on disk.
ScenarioLoadResult validateScenario(const Scenario& s,
                                    const std::vector<UnitDef>& units,
                                    const std::vector<TerrainDef>& terrain);

// Prices ONE lane without loading a file: the cheapest terrain-only path cost from
// `from` to `to`, counting every hex entered including `to`. `allowBridge == false`
// confines the route to the seat's own bank (T-SCN-06's Bridge-free clause); true is
// T-SCN-11's opposing route (asymmetry (ii)). Returns false when no route exists.
bool laneCost(const Scenario& s, const std::vector<TerrainDef>& terrain,
              const Hex& from, const Hex& to, bool allowBridge, int& outCost);

// Hash of the canonical serialization: the Scenario fields in the §4.7 Stub 7 order,
// hexes in canonical hex order, `scenarioHash` itself EXCLUDED because a value cannot
// hash itself. Platform-stable by canonical ordering and by fixed-width integer
// arithmetic; 16 lowercase hex digits.
std::string scenarioHash(const Scenario& s);

// Builds the Move.h board the validator prices on: terrain from the scenario, and
// occupancy EMPTY -- Q21 ruled the lanes are priced on terrain alone. False if the
// scenario's terrain does not resolve against the loaded table.
bool scenarioBoard(const Scenario& s, const std::vector<TerrainDef>& terrain, Board& out);

// The single CanCapture row (T-DATA-03). Returns -1 unless there is exactly one.
int captureRowIndex(const std::vector<UnitDef>& units);

// "(col,row)" for a hex, for reasons and reports. The only place (col,row) reappears.
std::string hexLabel(const Hex& h);

} // namespace strat
