// Stratocracy — headless turn loop & win/tiebreak (GDD §4.7 Stub 5, §4.11 row 5).
// Zero engine dependencies. Pure state machine; no RNG, no clock, no I/O.
//
// This module OWNS THE TURN. Rows 3 and 4 declined it -- row 4 takes the turn
// number as an argument and never advances it -- so every turn-ownership question
// those rows deferred is concentrated here: whose turn it is, which units may still
// act, when a turn starts (the moment repair fires), and when the match is over.
//
// It still owns NO BOARD. Units, hexes and Fame stay with their modules; what this
// one needs about them arrives as a caller-supplied BoardSnapshot, the same way
// Combat.h::repairAmount takes onOwnedObjective and enemyAdjacent. Its inputs are
// the quantities §2.11.4's scoreboard already shows: the tiebreak adds no new state,
// only an ordering over existing state (§2.8). See spec/turn_spec.md.
#pragma once

#include <string>
#include <vector>

#include "Combat.h"    // Unit, repairAmount -- verified at 5ffa8d6 (T-REPAIR-01..07)
#include "Economy.h"   // SIDE_COUNT -- §4.11 declares row 5's dependency on row 4
#include "Hex.h"       // Hex -- the per-factory build record's key (T-TURN-10)

namespace strat {

constexpr int SIDE_NONE = -1;

// §2.8's victory quality: a TIER, never a number. Tiers rank CATEGORICALLY and Fame
// is only the sort key inside criterion 1 (T-TURN-07), which is what stops a long
// capped grind's tally from reading as better than a flag kill (§1.5 #5).
enum class ResultTier { InProgress, Draw, Marginal, Decisive };

enum class ResultCause {
    None,             // still in progress
    FlagDestroyed,    // §2.8 primary win
    Domination,       // §2.8 secondary win -- every FACTORY at the start of your turn
    AttritionLead,    // led the lexicographic comparison at the cap
    PassivityGuard,   // both sides' combat Fame zero at the cap
    AllKeysTied,      // all three keys equal at the cap
};

struct MatchResult {
    ResultTier  tier  = ResultTier::InProgress;
    ResultCause cause = ResultCause::None;
    int winner = SIDE_NONE;    // SIDE_NONE on a draw and while in progress
    int decidedByKey = 0;      // 1/2/3 = the §2.8 key that differed; 0 otherwise
};

// Categorical rank (T-TURN-07): Decisive > Marginal > Draw > InProgress. Takes no
// Fame and cannot -- there is no parameter through which a tally could reach it.
int tierRank(ResultTier t);
const char* tierName(ResultTier t);
const char* causeName(ResultCause c);

// What the loop is TOLD about the board at an evaluation point. Every field is a
// quantity the game already tracks for the economy (§2.7) and already displays on
// the standings scoreboard (§2.11.4).
struct SideSnapshot {
    // Which unit is the flag is Stub 7's `isFlag` placement field (row 7, built)
    // and Q10 is open on exactness, so this module takes the fact and designates
    // nothing.
    bool flagAlive      = true;
    int  fameCombat     = 0;   // key 1 -- Economy.h's counter; kills only, income never
    int  objectivesHeld = 0;   // key 2 -- factories AND captured towns, ownership only
    int  survivingHp    = 0;   // key 3 -- total remaining HP
    int  factoriesHeld  = 0;   // domination: FACTORIES ONLY, towns excluded (§2.8)
};

struct BoardSnapshot {
    SideSnapshot side[SIDE_COUNT];
    int factoryTotal = 0;      // every factory on the map
};

// The turn's phases. They exist so that "at the start of the unit's turn" (§2.7,
// T-TURN-08) is a MOMENT the gate can point at rather than a description.
enum class Phase {
    TurnPending,   // a side is up; beginTurn has not run
    StartOfTurn,   // beginTurn ran; start-of-turn repair may be applied
    Actions,       // repair applied; units may act
    MatchOver,     // a result is recorded; nothing further is legal
};

struct TurnState {
    int   turnNumber = 1;
    int   turnCap    = 0;      // per-scenario data (Q7, ruled); no literal 20 lives here
    int   activeSide = 0;
    int   firstSide  = 0;
    int   sidesEnded = 0;      // sides that have ended their turn this round
    Phase phase   = Phase::TurnPending;
    bool  running = false;
    // TWO INDEPENDENT PER-UNIT FLAGS, not one (T-TURN-01). A unit may move at most
    // once AND act at most once in its own turn, in EITHER order, and neither is
    // required. They are separate sets precisely so that neither can consume the
    // other: one shared set is the pass-1 reading, and it makes §2.1's move-then-act
    // sequence unimplementable.
    std::vector<int> moved;    // unit ids that have MOVED this turn
    std::vector<int> acted;    // unit ids that have ACTED this turn (§4.9 `hasActed`)
    // T-TURN-10's per-factory record. It lives here and not in Economy.h because the
    // check needs the turn, and row 4 takes the turn number as an argument rather
    // than owning it (§3) -- Stub 4 gates the half it can enforce without a turn.
    std::vector<Hex> builtThisTurn;
    MatchResult result;
};

// Starts a match. `turnCap` is the scenario's value and is never defaulted here --
// a cap below 1 is refused rather than replaced (Q7: the cap is data).
bool initMatch(TurnState& s, int firstSide, int turnCap, std::string& err);

// The immediate win conditions, callable after ANY command. A downed flag ends the
// match at once, which is why the cap tiebreak can never be reached in a match that
// contains one (T-TURN-02, §2.8 T-CAP-01). If BOTH flags read down, the snapshot
// describes a state a legal match cannot reach -- the first death already ended it --
// so this refuses to grade rather than inventing a rule for it.
MatchResult checkImmediate(TurnState& s, const BoardSnapshot& b);

// Start of `activeSide`'s turn: the immediate check, then §2.8's domination backstop
// (T-TURN-03), then BOTH per-unit flag sets and the per-factory build record clear,
// and the phase becomes StartOfTurn. That single moment is what T-TURN-01(e) and
// T-TURN-10 both name -- the flags and the build allowance renew together, at the
// start of the OWNER's turn, so a factory that changes hands mid-round arrives with
// a clear record for its new owner.
MatchResult beginTurn(TurnState& s, const BoardSnapshot& b);

// One unit offered for start-of-turn repair. `unit` and the two board facts are the
// caller's; this module forwards them and adds nothing.
struct RepairSubject {
    int  unitId = 0;
    int  side   = 0;
    Unit unit;
    bool onOwnedObjective = false;
    bool enemyAdjacent    = false;
};

struct RepairApplied {
    int unitId = 0;
    int amount = 0;            // exactly Combat.h::repairAmount, never a local formula
};

// Start-of-turn repair (§2.7). Fires ONLY in the StartOfTurn phase and ONLY for the
// ACTIVE side's units; every amount is what repairAmount returns for that unit and
// those facts. Advances the phase to Actions. Results ascend by unit id so the order
// is a property of the ids, not of the caller's vector (T-TURN-08, T-TURN-09).
std::vector<RepairApplied> applyStartOfTurnRepair(TurnState& s,
                                                  const std::vector<RepairSubject>& subjects);

// T-TURN-01. False for a unit that is not the active side's, has already acted this
// turn, or is acting outside the Actions phase.
//
// ACTING NEVER CONSUMES THE MOVE and moving never consumes the act: canAct reads the
// `acted` set alone and canMove the `moved` set alone, which is the whole mechanism
// behind "in either order". A unit that has attacked from where it stands is still a
// legal mover this turn; a unit that has moved is still a legal attacker.
bool canAct(const TurnState& s, int unitId, int unitSide);
bool hasActed(const TurnState& s, int unitId);
bool canMove(const TurnState& s, int unitId, int unitSide);
bool hasMoved(const TurnState& s, int unitId);

// Record that a unit acted / moved. Each refuses -- and changes nothing -- whenever
// its own predicate is false, filling `err` with the reason. Neither touches the
// other's set.
bool markActed(TurnState& s, int unitId, int unitSide, std::string& err);
bool markMoved(TurnState& s, int unitId, int unitSide, std::string& err);

// T-TURN-10 (§2.7; Q8(b), ruled). One build per factory per turn, player and AI
// alike: a Build naming a factory that has already taken its build this turn is
// REFUSED, and the refusal changes nothing -- Fame is committed at queue time
// (Q8(c)) and a refused Build never queues, so the caller must consult this BEFORE
// it charges anything. BOTH dispositions of the first build count against the
// allowance: one that spawned immediately, and one that waits and holds the factory's
// slot (T-FAME-04). The slot and this allowance are two rules, not one.
bool hasBuiltThisTurn(const TurnState& s, const Hex& factory);
bool canBuildAt(const TurnState& s, const Hex& factory, int side);
bool markBuilt(TurnState& s, const Hex& factory, int side, std::string& err);

// Ends the active side's turn. Strict alternation: the next side is the next in
// order. When every side has ended its turn the round is complete and the turn
// number advances -- unless that round was the cap, where the match resolves
// (T-TURN-04, and the second stated reading in spec/turn_spec.md).
MatchResult endTurn(TurnState& s, const BoardSnapshot& b);

// §2.8's attrition tiebreak, PURE: the mutual-passivity guard, then the three keys
// in order, then a draw. Takes no TurnState, so it is the one place the procedure
// lives and the same call can answer "who leads right now" (§2.11.4's chevron).
MatchResult resolveAtCap(const BoardSnapshot& b);

// Order-independent, platform-independent digest of the TURN state. §4.10's save
// hash is taken from this state alongside the other modules' (row 10 part (b), built:
// `Replay.h::canonicalStateBytes` reads `turnNumber`, `activeSide`, the two per-unit
// flag sets and `builtThisTurn` from here). This digest is NOT that hash — it is the
// turn module's own, over the turn state alone.
std::string stateDigest(const TurnState& s);

} // namespace strat
