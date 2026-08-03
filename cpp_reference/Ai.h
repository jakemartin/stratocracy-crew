// Stratocracy — headless baseline opponent AI (GDD §4.7 Stub 6, §4.11 row 6).
// Zero engine dependencies. Pure function of state; no RNG, no clock, no I/O.
//
// THE AI HOLDS NO RULES AND APPLIES NOTHING. It emits ONE ordinary command at a
// time and the caller applies it through the same path a player's command takes
// (§2.9) -- which is what makes T-AI-01's "validated like any player command"
// structural rather than asserted. Every decision inside delegates: reachability
// and routes to Move.h, damage and counters to Combat.h, stats to Data.h,
// affordability and kill value to Economy.h, act flags and alternation to Turn.h.
//
// It also sees only real state (§4.7 Stub 6: "the AI cheats at nothing"). There is
// no hidden field on AiState that a player could not read off the board.
// See spec/ai_spec.md.
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

// A unit as the AI sees it. `isFlag` is Stub 7's placement field (row 7, built;
// Q10 open) and arrives as a fact -- the AI designates nothing.
struct AiUnit {
    int  id       = 0;
    int  side     = 0;
    int  defIndex = 0;
    Hex  hex;
    int  hp       = 0;
    bool isFlag   = false;
};

// Everything the AI is allowed to know. All of it is visible board state.
struct AiState {
    MapBounds bounds;
    std::vector<int>        terrain;      // offset-indexed into terrainDefs
    std::vector<AiUnit>     units;
    std::vector<UnitDef>    unitDefs;
    std::vector<TerrainDef> terrainDefs;
    EconomyState economy;
    TurnState    turn;

    // The default buildlist (§2.9), as defIndexes into unitDefs. §2.9 describes it
    // as "mostly Infantry, an occasional Tank" and gives no ratio or rule, so the
    // list is DATA the caller supplies; inventing a ratio here would be a rule the
    // GDD does not have. Q9's priority orders whatever it contains.
    std::vector<int> buildlist;

    // Factories this side has already built at THIS turn. §2.7's "one build per
    // factory per turn" is turn-scoped, and the turn belongs to row 5, so the AI
    // takes it as a caller-supplied fact rather than keeping a counter -- the same
    // discipline row 4 uses for occupancy. See spec/ai_spec.md's change request.
    std::vector<Hex> builtThisTurn;
};

// Capture is deliberately NOT here. §2.7 completes a capture "after N turns of
// holding", which is a turn-boundary event the caller runs alongside income --
// the same division row 5 draws when it leaves income to row 4. The AI's part of
// §2.9's capture behaviour is the MOVE onto the objective (T-AI-03); standing
// still on one it already holds is how it keeps holding.
enum class AiCommandKind { Build, Move, Attack, EndTurn };

// One ordinary command, in the same vocabulary a player uses.
struct AiCommand {
    AiCommandKind kind = AiCommandKind::EndTurn;
    int unitId   = -1;    // Move, Attack
    int targetId = -1;    // Attack
    Hex hex;              // Move destination, or the Build factory hex
    int defIndex = -1;    // Build
};

const char* commandKindName(AiCommandKind k);

// THE entry point. Returns the next command for `side`, or EndTurn when the turn
// holds nothing more. The caller applies it and calls again; the AI never mutates
// state, so a command it emits and a command a player types are indistinguishable
// to the rules modules.
AiCommand nextCommand(const AiState& s, int side);

// --- the pieces, exposed so the gate can compare against them directly --------

// Damage `attacker` would deal to `defender` right now, via Combat.h. Zero if the
// distance is outside the attacker's band.
int expectedDamage(const AiState& s, const AiUnit& attacker, const AiUnit& defender);

// §2.9's one guard, and BOTH halves of it bind: an attack is strictly losing only
// when the counter kills the attacker AND the exchange trades down. §2.9 gives no
// metric for "trades down", so one is stated in spec/ai_spec.md as a documented
// choice: value dealt is the victim's Economy.h kill award prorated by the damage
// share of its max HP, value lost is the attacker's own kill award. An Infantry
// that dies putting a large dent in a Tank trades UP and the attack is NOT skipped
// (T-AI-05). Every price comes from Economy.h::killAward, never from this file.
bool isStrictlyLosing(const AiState& s, const AiUnit& attacker, const AiUnit& defender);

// Value dealt and value lost, exposed so the gate compares against the same two
// numbers the guard uses rather than re-deriving them.
int exchangeValueDealt(const AiState& s, const AiUnit& attacker, const AiUnit& defender);
int exchangeValueLost(const AiState& s, const AiUnit& attacker);

// The buildlist unit to produce at a held factory, or -1 if none is affordable.
// Ties break by the fixed priority Infantry > Recon > Artillery > Tank, which is
// ascending §2.4 COST and NOT the order §2.4's table prints (Q9, ruled).
int chooseBuild(const AiState& s, int side);

// Q9's tie-break, exposed because it is the rule most likely to be got wrong:
// strictly-ascending costFame, ties by the pinned UnitType order.
bool buildPriorityLess(const UnitDef& a, const UnitDef& b);

const AiUnit* findAiUnit(const AiState& s, int id);

} // namespace strat
