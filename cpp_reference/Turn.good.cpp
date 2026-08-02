// Stratocracy — turn loop & win/tiebreak implementation (GDD §4.7 Stub 5).
// Pure state machine; no RNG, no clock, no I/O. §2.8's resolution procedure lives
// here once -- one guard, one three-key comparison, one grade -- and nowhere else.
#include "Turn.h"

#include <algorithm>
#include <cstddef>

namespace strat {

// §2.8 is written for two sides: the guard says "BOTH sides", the comparison is
// pairwise, and the domination win is "you hold every factory". Pinned rather than
// assumed, so a later side count fails to build instead of resolving wrongly.
static_assert(SIDE_COUNT == 2, "Turn.cpp implements the two-side procedure of GDD 2.8");

namespace {

bool validSide(int side) { return side >= 0 && side < SIDE_COUNT; }

std::string num(int v) { return std::to_string(v); }

} // namespace

int tierRank(ResultTier t) {
    // Categorical, and deliberately not a function of any tally: a Decisive win
    // always outranks a Marginal one regardless of how much Fame either side piled
    // up (§2.8, closing the §1.5 #5 inversion).
    switch (t) {
        case ResultTier::Decisive:   return 3;
        case ResultTier::Marginal:   return 2;
        case ResultTier::Draw:       return 1;
        case ResultTier::InProgress: return 0;
    }
    return 0;
}

const char* tierName(ResultTier t) {
    switch (t) {
        case ResultTier::Decisive:   return "Decisive";
        case ResultTier::Marginal:   return "Marginal";
        case ResultTier::Draw:       return "Draw";
        case ResultTier::InProgress: return "InProgress";
    }
    return "InProgress";
}

const char* causeName(ResultCause c) {
    switch (c) {
        case ResultCause::FlagDestroyed:  return "FlagDestroyed";
        case ResultCause::Domination:     return "Domination";
        case ResultCause::AttritionLead:  return "AttritionLead";
        case ResultCause::PassivityGuard: return "PassivityGuard";
        case ResultCause::AllKeysTied:    return "AllKeysTied";
        case ResultCause::None:           return "None";
    }
    return "None";
}

bool initMatch(TurnState& s, int firstSide, int turnCap, std::string& err) {
    if (!validSide(firstSide)) { err = "firstSide must be 0 or " + num(SIDE_COUNT - 1); return false; }
    // Q7, ruled: the cap is per-scenario data held in Stub 7's `turnCap`. A scenario
    // that supplies no usable cap is refused; it is never replaced with a default,
    // which is how "no literal 20 lives here" is enforced rather than promised.
    if (turnCap < 1) { err = "turnCap must be >= 1 (per-scenario data, Q7)"; return false; }
    TurnState fresh;
    fresh.turnNumber = 1;
    fresh.turnCap    = turnCap;
    fresh.firstSide  = firstSide;
    fresh.activeSide = firstSide;
    fresh.sidesEnded = 0;
    fresh.phase      = Phase::TurnPending;
    fresh.running    = true;
    s = fresh;
    return true;
}

MatchResult checkImmediate(TurnState& s, const BoardSnapshot& b) {
    if (s.result.tier != ResultTier::InProgress) return s.result;   // already over
    if (!s.running) return s.result;

    int down = 0, survivors = 0, survivor = SIDE_NONE;
    for (int i = 0; i < SIDE_COUNT; ++i) {
        if (b.side[i].flagAlive) { ++survivors; survivor = i; }
        else                     { ++down; }
    }
    // Exactly one side left standing: Decisive for it, loss for the owner of the
    // downed flag, and the match ends AT ONCE -- which is why no match that reaches
    // the cap can contain a flag kill (§2.8, T-CAP-01/T-CAP-04).
    if (down >= 1 && survivors == 1) {
        s.result.tier         = ResultTier::Decisive;
        s.result.cause        = ResultCause::FlagDestroyed;
        s.result.winner       = survivor;
        s.result.decidedByKey = 0;
        s.phase   = Phase::MatchOver;
        s.running = false;
    }
    // survivors == 0 is a state a legal match cannot reach: the first flag death
    // already ended it. Nothing is graded here rather than inventing a rule for it.
    return s.result;
}

MatchResult beginTurn(TurnState& s, const BoardSnapshot& b) {
    if (!s.running || s.phase != Phase::TurnPending) return s.result;

    const MatchResult immediate = checkImmediate(s, b);
    if (immediate.tier != ResultTier::InProgress) return immediate;

    // §2.8's secondary win, evaluated at the START of the active side's turn and for
    // that side alone. FACTORIES ONLY -- towns are excluded so domination stays
    // hard-won -- and factoryTotal == 0 crowns nobody.
    if (b.factoryTotal > 0 && b.side[s.activeSide].factoriesHeld == b.factoryTotal) {
        s.result.tier         = ResultTier::Decisive;
        s.result.cause        = ResultCause::Domination;
        s.result.winner       = s.activeSide;
        s.result.decidedByKey = 0;
        s.phase   = Phase::MatchOver;
        s.running = false;
        return s.result;
    }

    s.acted.clear();                 // a new turn: every unit is unacted again
    s.phase = Phase::StartOfTurn;
    return s.result;
}

std::vector<RepairApplied> applyStartOfTurnRepair(TurnState& s,
                                                  const std::vector<RepairSubject>& subjects) {
    std::vector<RepairApplied> out;
    // The MOMENT half of T-TURN-08: only at the start of a turn. Called at any other
    // phase this heals nothing and advances nothing.
    if (!s.running || s.phase != Phase::StartOfTurn) return out;

    std::vector<const RepairSubject*> mine;
    for (const RepairSubject& r : subjects)
        if (r.side == s.activeSide) mine.push_back(&r);       // the active side alone
    std::stable_sort(mine.begin(), mine.end(),
                     [](const RepairSubject* a, const RepairSubject* b2) {
                         return a->unitId < b2->unitId;
                     });

    for (const RepairSubject* r : mine) {
        RepairApplied a;
        a.unitId = r->unitId;
        // Combat.h decides the amount. There is no heal arithmetic in this file --
        // the board facts are forwarded exactly as given (T-TURN-08).
        a.amount = repairAmount(r->unit, r->onOwnedObjective, r->enemyAdjacent);
        out.push_back(a);
    }
    s.phase = Phase::Actions;
    return out;
}

bool hasActed(const TurnState& s, int unitId) {
    for (int id : s.acted) if (id == unitId) return true;
    return false;
}

bool canAct(const TurnState& s, int unitId, int unitSide) {
    if (!s.running) return false;
    if (s.phase != Phase::Actions) return false;
    if (unitSide != s.activeSide) return false;      // strict alternation (§2.1)
    return !hasActed(s, unitId);                     // at most once per own turn
}

bool markActed(TurnState& s, int unitId, int unitSide, std::string& err) {
    if (!s.running)                { err = "no match is running"; return false; }
    if (s.phase == Phase::MatchOver) { err = "the match is over"; return false; }
    if (s.phase == Phase::TurnPending) { err = "the turn has not begun"; return false; }
    if (s.phase == Phase::StartOfTurn) {
        err = "start-of-turn repair has not been applied yet"; return false;
    }
    if (!validSide(unitSide))      { err = "invalid side"; return false; }
    if (unitSide != s.activeSide)  {
        err = "side " + num(unitSide) + " is not the active side"; return false;
    }
    if (hasActed(s, unitId)) {
        err = "unit " + num(unitId) + " has already acted this turn"; return false;
    }
    s.acted.push_back(unitId);
    return true;
}

MatchResult endTurn(TurnState& s, const BoardSnapshot& b) {
    if (!s.running || s.phase != Phase::Actions) return s.result;   // refuse, unchanged

    const MatchResult immediate = checkImmediate(s, b);
    if (immediate.tier != ResultTier::InProgress) return immediate;

    s.sidesEnded += 1;
    if (s.sidesEnded < SIDE_COUNT) {
        s.activeSide = (s.activeSide + 1) % SIDE_COUNT;   // strict alternation
        s.phase = Phase::TurnPending;
        return s.result;
    }

    // The round is complete. Reading 2 (spec/turn_spec.md): turn `turnCap` is a
    // playable turn, so the tiebreak resolves at the END of that round.
    if (s.turnNumber >= s.turnCap) {
        s.result  = resolveAtCap(b);
        s.phase   = Phase::MatchOver;
        s.running = false;
        return s.result;
    }

    s.turnNumber += 1;
    s.sidesEnded  = 0;
    s.activeSide  = s.firstSide;
    s.phase       = Phase::TurnPending;
    return s.result;
}

MatchResult resolveAtCap(const BoardSnapshot& b) {
    MatchResult r;

    // 1. The mutual-passivity guard. Both sides at zero combat Fame -- nobody
    //    engaged -- is an IMMEDIATE draw. It does NOT fall through to the keys
    //    below, because "objectives held" would otherwise re-crown a turtle who
    //    simply sat on more factories (§2.8 step 1, T-CAP-02).
    bool anyoneFought = false;
    for (int i = 0; i < SIDE_COUNT; ++i) if (b.side[i].fameCombat != 0) anyoneFought = true;
    if (!anyoneFought) {
        r.tier  = ResultTier::Draw;
        r.cause = ResultCause::PassivityGuard;
        return r;
    }

    // 2. Lexicographic comparison, higher wins at the first key that differs:
    //    combat Fame -> objectives held -> surviving HP (§2.8 step 2, in order).
    //    Reaching key 2 therefore implies key 1 tied at a nonzero value, so both
    //    sides fought by construction and no separate precondition is needed
    //    (T-TURN-06 -- the guard above is what makes that true).
    const int keys[SIDE_COUNT][3] = {
        {b.side[0].fameCombat, b.side[0].objectivesHeld, b.side[0].survivingHp},
        {b.side[1].fameCombat, b.side[1].objectivesHeld, b.side[1].survivingHp},
    };
    for (int k = 0; k < 3; ++k) {
        if (keys[0][k] == keys[1][k]) continue;
        r.tier         = ResultTier::Marginal;   // led the comparison at the cap
        r.cause        = ResultCause::AttritionLead;
        r.winner       = (keys[0][k] > keys[1][k]) ? 0 : 1;
        r.decidedByKey = k + 1;
        return r;
    }

    // 3. All three keys equal -> draw (§2.8 step 3).
    r.tier  = ResultTier::Draw;
    r.cause = ResultCause::AllKeysTied;
    return r;
}

std::string stateDigest(const TurnState& s) {
    std::vector<int> acted = s.acted;
    std::sort(acted.begin(), acted.end());       // order-independent by construction

    std::string acc;
    acc += "t" + num(s.turnNumber) + "/" + num(s.turnCap);
    acc += "|side" + num(s.activeSide) + ":first" + num(s.firstSide) +
           ":ended" + num(s.sidesEnded);
    acc += "|phase" + num(static_cast<int>(s.phase)) + ":run" + num(s.running ? 1 : 0);
    acc += "|acted";
    for (int id : acted) acc += num(id) + ",";
    acc += "|res" + num(static_cast<int>(s.result.tier)) + ":" +
           num(static_cast<int>(s.result.cause)) + ":" +
           num(s.result.winner) + ":" + num(s.result.decidedByKey);

    unsigned long long h = 1469598103934665603ULL;          // FNV-1a, 64-bit
    for (char c : acc) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ULL; }
    std::string hex;
    for (int i = 15; i >= 0; --i) hex += "0123456789abcdef"[(h >> (i * 4)) & 0xF];
    return hex;
}

} // namespace strat
