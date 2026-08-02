// Test Engineer's gate for §4.11 row 5 — Turn loop & win/tiebreak (T-TURN-01..09).
//
// Row 5 is where the turn finally has an owner: rows 3 and 4 declined it, so every
// deferred turn-ownership question is asserted here. T-TURN-04/05/06/07 encode §2.8's
// resolution procedure exactly -- one guard, one three-key comparison, one grade --
// and two rows rest on ruled questions: Q7 (the cap is per-scenario data, so the gate
// asserts a CONFIGURED cap and never a literal 20) and Q29 (the row flips only on the
// full set at one commit).
//
// T-TURN-08 asserts one thing only: that the loop calls the ALREADY-VERIFIED
// repairAmount at the right moment with the right board facts. The heal values are
// green at 5ffa8d6 under T-REPAIR-01..07 and are not re-asserted here -- every
// expectation is a direct call into Combat.h, never a number written in this file.
#include "Turn.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace strat;

static int g_total = 0;
static int g_fail  = 0;

static void check(const char* name, bool cond) {
    ++g_total;
    if (cond) { std::printf("PASS  %s\n", name); }
    else      { ++g_fail; std::printf("FAIL  %s\n", name); }
}

static int defIndexOf(const std::vector<UnitDef>& units, const std::string& id) {
    for (std::size_t i = 0; i < units.size(); ++i) if (units[i].id == id) return static_cast<int>(i);
    return -1;
}

// A snapshot in which nothing ends the match: both flags up, factories split.
static BoardSnapshot quietBoard() {
    BoardSnapshot b;
    b.factoryTotal = 4;
    for (int i = 0; i < SIDE_COUNT; ++i) {
        b.side[i].flagAlive      = true;
        b.side[i].fameCombat     = 100;
        b.side[i].objectivesHeld = 4;
        b.side[i].survivingHp    = 200;
        b.side[i].factoriesHeld  = 2;
    }
    return b;
}

// Drive one full turn: begin, apply the start-of-turn moment, and land in Actions.
static void openTurn(TurnState& s, const BoardSnapshot& b) {
    beginTurn(s, b);
    const std::vector<RepairSubject> none;
    applyStartOfTurnRepair(s, none);
}

static Unit unitFrom(const UnitDef& d, int hp) {
    Unit u;
    u.atk = d.atk; u.def = d.def; u.hp = hp; u.hpMax = d.hpMax;
    u.rangeMin = d.rangeMin; u.rangeMax = d.rangeMax; u.type = d.type;
    return u;
}

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? std::string(argv[1]) : std::string("../data");

    std::vector<UnitDef> units;
    std::string err;
    if (!loadUnits(dir + "/units.csv", units, err)) {
        std::printf("FAIL  T-TURN-00 load-row-2-tables (%s)\n\n0/1 passed\n", err.c_str());
        return 1;
    }
    const int TANK = defIndexOf(units, "Tank");
    const int INF  = defIndexOf(units, "Infantry");
    if (TANK < 0 || INF < 0) {
        std::printf("FAIL  T-TURN-00 table-lookup\n\n0/1 passed\n");
        return 1;
    }

    // --- T-TURN-01 -------------------------------------------------------------
    // Strict alternation; at most once per own turn; any order the owner chooses.
    bool ok01 = true;
    {
        const BoardSnapshot b = quietBoard();
        TurnState s;
        if (!initMatch(s, 0, 20, err)) ok01 = false;

        // Acting before the turn has begun, and before the start-of-turn moment,
        // is refused -- and refusing changes nothing.
        std::string e;
        const std::string d0 = stateDigest(s);
        if (markActed(s, 1, 0, e)) ok01 = false;              // TurnPending
        if (stateDigest(s) != d0) ok01 = false;
        beginTurn(s, b);
        if (markActed(s, 1, 0, e)) ok01 = false;              // StartOfTurn
        const std::vector<RepairSubject> none;
        applyStartOfTurnRepair(s, none);

        if (!canAct(s, 1, 0)) ok01 = false;
        if (!markActed(s, 1, 0, e)) ok01 = false;
        if (!hasActed(s, 1)) ok01 = false;
        // At most once per own turn.
        if (canAct(s, 1, 0)) ok01 = false;
        const std::string d1 = stateDigest(s);
        if (markActed(s, 1, 0, e)) ok01 = false;
        if (stateDigest(s) != d1) ok01 = false;
        // The inactive side may not act at all.
        if (canAct(s, 9, 1)) ok01 = false;
        if (markActed(s, 9, 1, e)) ok01 = false;
        if (stateDigest(s) != d1) ok01 = false;

        // Strict alternation: side 0 ends, side 1 is up, and side 0 is now the one
        // that cannot act.
        endTurn(s, b);
        if (s.activeSide != 1) ok01 = false;
        openTurn(s, b);
        if (canAct(s, 1, 0)) ok01 = false;
        if (!canAct(s, 9, 1)) ok01 = false;

        // A new turn clears the acted set: unit 1 may act again next time round.
        markActed(s, 9, 1, e);
        endTurn(s, b);
        if (s.turnNumber != 2 || s.activeSide != 0) ok01 = false;
        openTurn(s, b);
        if (!canAct(s, 1, 0)) ok01 = false;
        if (hasActed(s, 1)) ok01 = false;

        // Any order: the same three units acted in two different orders leave the
        // same state.
        TurnState p, q;
        initMatch(p, 0, 20, err); openTurn(p, b);
        initMatch(q, 0, 20, err); openTurn(q, b);
        const int orderA[3] = {1, 2, 3};
        const int orderB[3] = {3, 1, 2};
        for (int i = 0; i < 3; ++i) {
            if (!markActed(p, orderA[i], 0, e)) ok01 = false;
            if (!markActed(q, orderB[i], 0, e)) ok01 = false;
        }
        if (stateDigest(p) != stateDigest(q)) ok01 = false;
        for (int i = 1; i <= 3; ++i) if (!hasActed(p, i) || !hasActed(q, i)) ok01 = false;
    }
    check("T-TURN-01 alternation-and-once-per-own-turn", ok01);

    // --- T-TURN-02 -------------------------------------------------------------
    // A downed flag ends the match immediately: Decisive for the killer, and the
    // tiebreak is never reached (§2.8 T-CAP-01).
    bool ok02 = true;
    {
        BoardSnapshot b = quietBoard();
        TurnState s;
        initMatch(s, 0, 20, err);
        openTurn(s, b);

        b.side[0].flagAlive = false;                  // side 0's flag falls
        const MatchResult r = checkImmediate(s, b);
        if (r.tier != ResultTier::Decisive) ok02 = false;
        if (r.cause != ResultCause::FlagDestroyed) ok02 = false;
        if (r.winner != 1) ok02 = false;              // the killer wins, the owner loses
        if (r.decidedByKey != 0) ok02 = false;
        if (s.phase != Phase::MatchOver || s.running) ok02 = false;

        // Over means over: nothing further moves the state.
        const std::string after = stateDigest(s);
        std::string e;
        if (markActed(s, 1, 0, e)) ok02 = false;
        beginTurn(s, b);
        endTurn(s, b);
        if (stateDigest(s) != after) ok02 = false;
        if (s.result.cause != ResultCause::FlagDestroyed) ok02 = false;

        // The cap tiebreak is never evaluated in a match that contains a flag kill,
        // even when the flag falls in the capped round itself.
        TurnState c;
        BoardSnapshot cb = quietBoard();
        cb.side[0].fameCombat = 0;                    // a tiebreak here would be a draw
        cb.side[1].fameCombat = 0;
        initMatch(c, 0, 1, err);                      // cap = 1: this IS the last round
        openTurn(c, cb);
        endTurn(c, cb);
        openTurn(c, cb);
        cb.side[1].flagAlive = false;
        const MatchResult cr = endTurn(c, cb);
        if (cr.cause != ResultCause::FlagDestroyed) ok02 = false;
        if (cr.tier != ResultTier::Decisive || cr.winner != 0) ok02 = false;

        // Both flags down is a state a legal match cannot reach; it is not graded.
        TurnState both;
        BoardSnapshot bb = quietBoard();
        initMatch(both, 0, 20, err);
        bb.side[0].flagAlive = false;
        bb.side[1].flagAlive = false;
        const MatchResult br = checkImmediate(both, bb);
        if (br.tier != ResultTier::InProgress) ok02 = false;
        if (br.winner != SIDE_NONE) ok02 = false;
    }
    check("T-TURN-02 flag-death-ends-immediately", ok02);

    // --- T-TURN-03 -------------------------------------------------------------
    // Domination: every FACTORY at the START of your turn. Towns excluded.
    bool ok03 = true;
    {
        BoardSnapshot b = quietBoard();
        b.factoryTotal = 4;
        b.side[0].factoriesHeld = 4;
        b.side[1].factoriesHeld = 0;

        TurnState s;
        initMatch(s, 0, 20, err);
        const MatchResult r = beginTurn(s, b);
        if (r.tier != ResultTier::Decisive) ok03 = false;
        if (r.cause != ResultCause::Domination) ok03 = false;
        if (r.winner != 0) ok03 = false;
        if (s.phase != Phase::MatchOver || s.running) ok03 = false;

        // Towns are excluded: three of four factories plus every town is not
        // domination, however large `objectivesHeld` gets.
        BoardSnapshot t = quietBoard();
        t.factoryTotal = 4;
        t.side[0].factoriesHeld  = 3;
        t.side[0].objectivesHeld = 7;                 // 3 factories + 4 towns
        t.side[1].factoriesHeld  = 1;
        t.side[1].objectivesHeld = 1;
        TurnState s2;
        initMatch(s2, 0, 20, err);
        if (beginTurn(s2, t).tier != ResultTier::InProgress) ok03 = false;
        if (s2.phase != Phase::StartOfTurn) ok03 = false;

        // "At the start of YOUR turn": side 0 holding everything does not end the
        // match during side 1's turn -- it ends it when side 0's next turn begins.
        TurnState s3;
        initMatch(s3, 1, 20, err);                    // side 1 moves first
        if (beginTurn(s3, b).tier != ResultTier::InProgress) ok03 = false;
        const std::vector<RepairSubject> none;
        applyStartOfTurnRepair(s3, none);
        endTurn(s3, b);
        if (s3.activeSide != 0) ok03 = false;
        const MatchResult r3 = beginTurn(s3, b);
        if (r3.cause != ResultCause::Domination || r3.winner != 0) ok03 = false;

        // A map with no factories crowns nobody.
        BoardSnapshot none0 = quietBoard();
        none0.factoryTotal = 0;
        none0.side[0].factoriesHeld = 0;
        none0.side[1].factoriesHeld = 0;
        TurnState s4;
        initMatch(s4, 0, 20, err);
        if (beginTurn(s4, none0).tier != ResultTier::InProgress) ok03 = false;
    }
    check("T-TURN-03 domination-factories-only-at-turn-start", ok03);

    // --- T-TURN-04 -------------------------------------------------------------
    // The exact §2.8 order: combat Fame -> objectives held -> surviving HP -> draw,
    // resolved at the CONFIGURED cap (Q7) and never at a literal 20.
    bool ok04 = true;
    {
        // (a) key 1 differs
        BoardSnapshot a = quietBoard();
        a.side[0].fameCombat = 150; a.side[1].fameCombat = 100;
        a.side[0].objectivesHeld = 1; a.side[1].objectivesHeld = 8;   // key 2 would flip it
        a.side[0].survivingHp = 10;   a.side[1].survivingHp = 400;    // so would key 3
        const MatchResult ra = resolveAtCap(a);
        if (ra.winner != 0 || ra.decidedByKey != 1) ok04 = false;
        if (ra.tier != ResultTier::Marginal || ra.cause != ResultCause::AttritionLead) ok04 = false;

        // (b) key 1 ties nonzero, key 2 differs
        BoardSnapshot b2 = quietBoard();
        b2.side[0].fameCombat = 100; b2.side[1].fameCombat = 100;
        b2.side[0].objectivesHeld = 3; b2.side[1].objectivesHeld = 2;
        b2.side[0].survivingHp = 10;   b2.side[1].survivingHp = 400;  // key 3 would flip it
        const MatchResult rb = resolveAtCap(b2);
        if (rb.winner != 0 || rb.decidedByKey != 2) ok04 = false;

        // (c) keys 1 and 2 tie, key 3 decides
        BoardSnapshot c = quietBoard();
        c.side[0].fameCombat = 100; c.side[1].fameCombat = 100;
        c.side[0].objectivesHeld = 3; c.side[1].objectivesHeld = 3;
        c.side[0].survivingHp = 47;   c.side[1].survivingHp = 55;
        const MatchResult rc = resolveAtCap(c);
        if (rc.winner != 1 || rc.decidedByKey != 3) ok04 = false;
        if (rc.tier != ResultTier::Marginal) ok04 = false;

        // (d) all three equal -> draw
        BoardSnapshot d = quietBoard();
        const MatchResult rd = resolveAtCap(d);
        if (rd.tier != ResultTier::Draw || rd.cause != ResultCause::AllKeysTied) ok04 = false;
        if (rd.winner != SIDE_NONE || rd.decidedByKey != 0) ok04 = false;

        // The cap is data. A cap of 3 resolves at the end of round 3 and not before,
        // and the same board under a cap of 20 is still in progress at round 3.
        BoardSnapshot live = quietBoard();
        live.side[0].fameCombat = 150; live.side[1].fameCombat = 100;
        TurnState s3, s20;
        if (!initMatch(s3, 0, 3, err)) ok04 = false;
        if (!initMatch(s20, 0, 20, err)) ok04 = false;
        for (int round = 1; round <= 3; ++round) {
            for (int half = 0; half < SIDE_COUNT; ++half) {
                openTurn(s3, live);
                const MatchResult step = endTurn(s3, live);
                const bool last = (round == 3 && half == SIDE_COUNT - 1);
                if (!last && step.tier != ResultTier::InProgress) ok04 = false;
                openTurn(s20, live);
                if (endTurn(s20, live).tier != ResultTier::InProgress) ok04 = false;
            }
        }
        if (s3.result.tier != ResultTier::Marginal) ok04 = false;
        if (s3.result.winner != 0 || s3.result.decidedByKey != 1) ok04 = false;
        if (s3.turnNumber != 3) ok04 = false;
        if (s20.result.tier != ResultTier::InProgress) ok04 = false;
        if (s20.turnNumber != 4) ok04 = false;

        // A cap is refused rather than defaulted, so no fallback value can exist.
        TurnState bad;
        if (initMatch(bad, 0, 0, err)) ok04 = false;
        if (initMatch(bad, 0, -1, err)) ok04 = false;
        if (initMatch(bad, SIDE_COUNT, 20, err)) ok04 = false;
    }
    check("T-TURN-04 cap-tiebreak-order-and-configured-cap", ok04);

    // --- T-TURN-05 -------------------------------------------------------------
    // Mutual-passivity guard: both at zero combat Fame -> immediate draw, with NO
    // fall-through to objectives held.
    bool ok05 = true;
    {
        BoardSnapshot b = quietBoard();
        b.side[0].fameCombat = 0;      b.side[1].fameCombat = 0;
        b.side[0].objectivesHeld = 4;  b.side[1].objectivesHeld = 1;   // the turtle's lead
        b.side[0].survivingHp = 300;   b.side[1].survivingHp = 50;
        const MatchResult r = resolveAtCap(b);
        if (r.tier != ResultTier::Draw) ok05 = false;
        if (r.cause != ResultCause::PassivityGuard) ok05 = false;
        if (r.winner != SIDE_NONE) ok05 = false;
        if (r.decidedByKey != 0) ok05 = false;                          // no key was read

        // The same through a real capped match, not only through the pure function.
        TurnState s;
        initMatch(s, 0, 2, err);
        for (int round = 1; round <= 2; ++round)
            for (int half = 0; half < SIDE_COUNT; ++half) { openTurn(s, b); endTurn(s, b); }
        if (s.result.cause != ResultCause::PassivityGuard) ok05 = false;
        if (s.result.tier != ResultTier::Draw) ok05 = false;

        // One kill is enough to leave the guard: the side that fought wins.
        BoardSnapshot one = b;
        one.side[1].fameCombat = 50;
        const MatchResult r2 = resolveAtCap(one);
        if (r2.winner != 1 || r2.decidedByKey != 1) ok05 = false;
        if (r2.tier != ResultTier::Marginal) ok05 = false;
    }
    check("T-TURN-05 mutual-passivity-guard-no-fall-through", ok05);

    // --- T-TURN-06 -------------------------------------------------------------
    // Criterion 2 is reached only when both sides fought and their combat Fame is
    // equal. Asserted structurally over a sweep, not on one fixture.
    bool ok06 = true;
    {
        // Positive: equal nonzero Fame, differing objectives -> key 2 decides.
        BoardSnapshot eq = quietBoard();
        eq.side[0].fameCombat = 75; eq.side[1].fameCombat = 75;
        eq.side[0].objectivesHeld = 5; eq.side[1].objectivesHeld = 2;
        if (resolveAtCap(eq).decidedByKey != 2) ok06 = false;

        // Negative: unequal Fame -> key 2 is never consulted, even when it would
        // reverse the answer.
        BoardSnapshot uneq = quietBoard();
        uneq.side[0].fameCombat = 50; uneq.side[1].fameCombat = 75;
        uneq.side[0].objectivesHeld = 8; uneq.side[1].objectivesHeld = 0;
        const MatchResult ru = resolveAtCap(uneq);
        if (ru.decidedByKey != 1 || ru.winner != 1) ok06 = false;

        // Structural sweep: whenever a result is decided below key 1, both sides'
        // combat Fame is equal AND nonzero.
        int sawKey2 = 0, sawKey3 = 0;
        const int fames[4] = {0, 25, 75, 150};
        const int objs[3]  = {0, 2, 5};
        const int hps[3]   = {0, 40, 90};
        for (int f0 = 0; f0 < 4; ++f0)
        for (int f1 = 0; f1 < 4; ++f1)
        for (int o0 = 0; o0 < 3; ++o0)
        for (int o1 = 0; o1 < 3; ++o1)
        for (int h0 = 0; h0 < 3; ++h0)
        for (int h1 = 0; h1 < 3; ++h1) {
            BoardSnapshot b = quietBoard();
            b.side[0].fameCombat = fames[f0];     b.side[1].fameCombat = fames[f1];
            b.side[0].objectivesHeld = objs[o0];  b.side[1].objectivesHeld = objs[o1];
            b.side[0].survivingHp = hps[h0];      b.side[1].survivingHp = hps[h1];
            const MatchResult r = resolveAtCap(b);
            if (r.decidedByKey >= 2) {
                if (fames[f0] != fames[f1] || fames[f0] == 0) ok06 = false;
                if (r.decidedByKey == 2) ++sawKey2; else ++sawKey3;
            }
        }
        if (sawKey2 == 0 || sawKey3 == 0) ok06 = false;     // a vacuous sweep is not a pass
    }
    check("T-TURN-06 criterion-2-only-when-both-fought-and-tied", ok06);

    // --- T-TURN-07 -------------------------------------------------------------
    // Tiers are categorical: Decisive > Marginal > Draw for ANY pair of Fame totals.
    bool ok07 = true;
    {
        if (!(tierRank(ResultTier::Decisive) > tierRank(ResultTier::Marginal))) ok07 = false;
        if (!(tierRank(ResultTier::Marginal) > tierRank(ResultTier::Draw))) ok07 = false;
        if (!(tierRank(ResultTier::Draw) > tierRank(ResultTier::InProgress))) ok07 = false;

        // The cap can never mint a Decisive, however wide the lead: Fame is the sort
        // key inside criterion 1 and nothing else.
        int sawMarginal = 0, sawDraw = 0;
        for (int f0 = 0; f0 <= 5000; f0 += 250)
        for (int f1 = 0; f1 <= 5000; f1 += 250) {
            BoardSnapshot b = quietBoard();
            b.side[0].fameCombat = f0; b.side[1].fameCombat = f1;
            b.side[0].objectivesHeld = 8; b.side[1].objectivesHeld = 0;
            b.side[0].survivingHp = 900;  b.side[1].survivingHp = 1;
            const MatchResult r = resolveAtCap(b);
            if (r.tier == ResultTier::Decisive) ok07 = false;
            if (r.tier == ResultTier::Marginal) ++sawMarginal;
            if (r.tier == ResultTier::Draw) ++sawDraw;
        }
        if (sawMarginal == 0 || sawDraw == 0) ok07 = false;

        // A Decisive win on a tiny tally outranks a Marginal win on a huge one.
        BoardSnapshot small = quietBoard();
        small.factoryTotal = 2;
        small.side[0].factoriesHeld = 2; small.side[1].factoriesHeld = 0;
        small.side[0].fameCombat = 0;    small.side[1].fameCombat = 0;
        TurnState s;
        initMatch(s, 0, 20, err);
        const MatchResult decisive = beginTurn(s, small);

        BoardSnapshot huge = quietBoard();
        huge.side[0].fameCombat = 5000; huge.side[1].fameCombat = 10;
        const MatchResult marginal = resolveAtCap(huge);

        if (decisive.tier != ResultTier::Decisive) ok07 = false;
        if (marginal.tier != ResultTier::Marginal) ok07 = false;
        if (!(tierRank(decisive.tier) > tierRank(marginal.tier))) ok07 = false;
    }
    check("T-TURN-07 tiers-are-categorical", ok07);

    // --- T-TURN-08 -------------------------------------------------------------
    // Repair fires at the start of the unit's turn, for the active side, with the
    // caller's board facts, and equals Combat.h::repairAmount -- nothing more.
    bool ok08 = true;
    {
        const BoardSnapshot b = quietBoard();
        const UnitDef& tank = units[TANK];
        const UnitDef& inf  = units[INF];
        const Unit hurtTank = unitFrom(tank, 1);
        const Unit hurtInf  = unitFrom(inf, 1);

        // The fixture has to be able to tell the two board facts apart, or the
        // pass-through assertion below proves nothing.
        if (repairAmount(hurtTank, true, false) == repairAmount(hurtTank, true, true)) ok08 = false;
        if (repairAmount(hurtTank, true, false) == repairAmount(hurtTank, false, false)) ok08 = false;

        std::vector<RepairSubject> subjects;
        RepairSubject s2; s2.unitId = 2; s2.side = 0; s2.unit = hurtTank;
        s2.onOwnedObjective = true;  s2.enemyAdjacent = false;
        RepairSubject s1; s1.unitId = 1; s1.side = 0; s1.unit = hurtTank;
        s1.onOwnedObjective = true;  s1.enemyAdjacent = true;      // in contact
        RepairSubject s3; s3.unitId = 3; s3.side = 0; s3.unit = hurtInf;
        s3.onOwnedObjective = false; s3.enemyAdjacent = false;     // not on an objective
        RepairSubject s9; s9.unitId = 9; s9.side = 1; s9.unit = hurtTank;
        s9.onOwnedObjective = true;  s9.enemyAdjacent = false;     // the OTHER side
        subjects.push_back(s2); subjects.push_back(s1);
        subjects.push_back(s3); subjects.push_back(s9);

        TurnState s;
        initMatch(s, 0, 20, err);
        beginTurn(s, b);
        if (s.phase != Phase::StartOfTurn) ok08 = false;
        const std::vector<RepairApplied> applied = applyStartOfTurnRepair(s, subjects);

        // The active side's three units, and only those, in ascending id order.
        if (applied.size() != 3) ok08 = false;
        else {
            if (applied[0].unitId != 1 || applied[1].unitId != 2 || applied[2].unitId != 3) ok08 = false;
            if (applied[0].amount != repairAmount(hurtTank, true, true)) ok08 = false;
            if (applied[1].amount != repairAmount(hurtTank, true, false)) ok08 = false;
            if (applied[2].amount != repairAmount(hurtInf, false, false)) ok08 = false;
        }
        for (const RepairApplied& a : applied) if (a.unitId == 9) ok08 = false;

        // The moment: it has advanced to Actions, and calling again heals nothing.
        if (s.phase != Phase::Actions) ok08 = false;
        if (!applyStartOfTurnRepair(s, subjects).empty()) ok08 = false;

        // And nothing before the turn begins.
        TurnState pending;
        initMatch(pending, 0, 20, err);
        if (pending.phase != Phase::TurnPending) ok08 = false;
        if (!applyStartOfTurnRepair(pending, subjects).empty()) ok08 = false;

        // Subject order is the caller's; the result order is not.
        std::vector<RepairSubject> shuffled;
        shuffled.push_back(s9); shuffled.push_back(s3);
        shuffled.push_back(s1); shuffled.push_back(s2);
        TurnState t;
        initMatch(t, 0, 20, err);
        beginTurn(t, b);
        const std::vector<RepairApplied> again = applyStartOfTurnRepair(t, shuffled);
        if (again.size() != applied.size()) ok08 = false;
        else for (std::size_t i = 0; i < again.size(); ++i)
            if (again[i].unitId != applied[i].unitId || again[i].amount != applied[i].amount) ok08 = false;
    }
    check("T-TURN-08 repair-called-at-turn-start-with-callers-facts", ok08);

    // --- T-TURN-09 -------------------------------------------------------------
    // The same command sequence from the same scenario: identical tier and
    // identical state at every step.
    bool ok09 = true;
    {
        BoardSnapshot b = quietBoard();
        b.side[0].fameCombat = 100; b.side[1].fameCombat = 100;
        b.side[0].objectivesHeld = 5; b.side[1].objectivesHeld = 3;

        std::vector<RepairSubject> subjects;
        RepairSubject a; a.unitId = 1; a.side = 0; a.unit = unitFrom(units[TANK], 2);
        a.onOwnedObjective = true; a.enemyAdjacent = false;
        RepairSubject c; c.unitId = 2; c.side = 1; c.unit = unitFrom(units[INF], 2);
        c.onOwnedObjective = true; c.enemyAdjacent = false;
        subjects.push_back(a); subjects.push_back(c);

        auto play = [&](std::vector<std::string>& trace) {
            TurnState s;
            std::string e;
            initMatch(s, 0, 4, e);
            trace.push_back(stateDigest(s));
            for (int round = 1; round <= 4; ++round) {
                for (int half = 0; half < SIDE_COUNT; ++half) {
                    beginTurn(s, b);
                    trace.push_back(stateDigest(s));
                    const std::vector<RepairApplied> rep = applyStartOfTurnRepair(s, subjects);
                    for (const RepairApplied& r : rep)
                        trace.push_back("r" + std::to_string(r.unitId) + ":" +
                                        std::to_string(r.amount));
                    markActed(s, 1 + half, s.activeSide, e);
                    trace.push_back(stateDigest(s));
                    endTurn(s, b);
                    trace.push_back(stateDigest(s));
                }
            }
            trace.push_back(std::string(tierName(s.result.tier)) + "/" +
                            causeName(s.result.cause) + "/" +
                            std::to_string(s.result.winner) + "/" +
                            std::to_string(s.result.decidedByKey));
        };

        std::vector<std::string> t1, t2;
        play(t1);
        play(t2);
        if (t1.empty() || t1.size() != t2.size()) ok09 = false;
        else for (std::size_t i = 0; i < t1.size(); ++i) if (t1[i] != t2[i]) ok09 = false;
        if (!ok09 || t1.empty() || t1.back().find("Marginal") == std::string::npos) ok09 = false;
    }
    check("T-TURN-09 determinism", ok09);

    std::printf("\n%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
