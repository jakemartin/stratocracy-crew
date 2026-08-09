// Test Engineer's gate for §4.11 row 10, PART (c) — the Balance Analyst's self-play
// log producer.
//
// WHAT THIS SUITE CLOSES. One acceptance ID: T-SAVE-07, "harness compatibility: a
// Balance Analyst self-play log validates and replays as a save file — one format, no
// dialect drift". It is checked in three clauses, printed on three lines: it VALIDATES
// (the produced text loads under a matching header expectation), it REPLAYS (replaying
// the parsed log from the same initial state reaches the same canonical state hash the
// producing run reached), and there is NO DIALECT DRIFT (serialise -> parse ->
// serialise is byte-identical, and every kind in the log is one the writer and the
// parser both spell).
//
// T-SAVE-07 IS NOT MARKED †, and it is the only one of row 10's seven that is headless
// and was still open after part (b). T-SAVE-06 remains the row's only †; it needs the
// in-editor Automation harness and does not run here. Both facts are printed by name at
// the end rather than folded into the tally.
//
// THE COMMAND SET THIS LOG CARRIES IS FOUR KINDS, NOT FIVE, and that is the AI's design.
// Ai.h states that capture is deliberately outside the AI's vocabulary — it is a
// turn-boundary event the caller runs beside income, and the AI's part of §2.9's capture
// behaviour is the MOVE onto the objective (T-AI-03). So a self-play match emits `Move`,
// `Attack`, `Build` and `EndTurn` and never `Capture`. The complete §4.9 set was
// exercised by part (b)'s hand-authored log in test_replay.cpp; T-SAVE-07 asserts FORMAT
// COMPATIBILITY, not command coverage, so those four are its whole written fixture set
// and Q29 is satisfied over that set. GATE-BALANCE-COMMAND-SET asserts the four are
// present rather than leaving "the AI happened to emit some commands" unstated.
//
// THE CHECKS RECOMPUTE; THEY DO NOT DELEGATE. The translation checks below read the
// expected target hex out of the fixture's own unit list by scanning it here, never by
// asking the module what it thinks the target is. The replay check re-applies the log
// through `replayLog` from a COPY of the initial state — an independent path from the
// producing run, which interleaved AI decisions with single `applyCommand` calls — and
// compares the hash both ways round.
//
// THE FIXTURE TABLES ARE BUILT HERE, not loaded from data/, on test_replay.cpp's stated
// reason: loading the shipped CSVs would make this verdict depend on row 2's files.
#include "Hex.h"
#include "Replay.h"
#include "Save.h"
#include "Balance.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace strat;

static int g_total = 0;
static int g_fail  = 0;

static void check(const char* name, bool cond) {
    ++g_total;
    if (cond) { std::printf("PASS  %s\n", name); }
    else      { std::printf("FAIL  %s\n", name); ++g_fail; }
}

// ---------------------------------------------------------------------------
// Fixture. A 5x3 board, one factory per side so §2.8's domination backstop cannot end
// the match before its first command, and one neutral town. Two unit rows, both
// buildable out of the starting Fame so the AI's Build branch is actually reached.
// ---------------------------------------------------------------------------
static std::vector<TerrainDef> fixtureTerrain() {
    std::vector<TerrainDef> t(3);
    t[0].id = "Plains";  t[0].moveCost = 1; t[0].defensePct = 0; t[0].passLand = true;
    t[1].id = "Factory"; t[1].moveCost = 1; t[1].defensePct = 0; t[1].passLand = true;
    t[1].capturable = true; t[1].incomeFame = 100; t[1].isSpawnPoint = true;
    t[2].id = "Town";    t[2].moveCost = 1; t[2].defensePct = 10; t[2].passLand = true;
    t[2].capturable = true; t[2].incomeFame = 25;
    return t;
}

static std::vector<UnitDef> fixtureUnits() {
    std::vector<UnitDef> u(2);
    u[0].id = "Infantry"; u[0].hpMax = 10; u[0].move = 3; u[0].atk = 4; u[0].def = 2;
    u[0].rangeMin = 1; u[0].rangeMax = 1; u[0].costFame = 100;
    u[0].type = UnitType::Infantry; u[0].canCapture = true;
    u[1].id = "Tank";     u[1].hpMax = 20; u[1].move = 3; u[1].atk = 8; u[1].def = 5;
    u[1].rangeMin = 1; u[1].rangeMax = 1; u[1].costFame = 200;
    u[1].type = UnitType::Tank;     u[1].canCapture = false;
    return u;
}

static GameUnit mkUnit(int id, int side, int defIndex, int col, int row, int hp) {
    GameUnit u;
    u.id = id; u.side = side; u.defIndex = defIndex;
    u.hex = offsetToAxial(col, row);
    u.placement = u.hex;
    u.hp = hp;
    return u;
}

// The turn cap is deliberately small: the match must END inside the command budget, and
// a cap reached is a result §2.8 states (resolveAtCap) rather than a stalled run.
static const int kTurnCap = 6;

static GameState fixtureState(const std::vector<UnitDef>& ud,
                              const std::vector<TerrainDef>& td) {
    GameState g;
    g.bounds.cols = 5; g.bounds.rows = 3;
    g.terrain.assign(15, 0);
    g.terrain[2 * 5 + 0] = 1;                 // Factory at (0,2), side 0's
    g.terrain[2 * 5 + 4] = 1;                 // Factory at (4,2), side 1's
    g.terrain[1 * 5 + 2] = 2;                 // Town    at (2,1), neutral

    g.units.push_back(mkUnit(1, 0, 0, 1, 1, 10));   // side 0 Infantry
    g.units.push_back(mkUnit(2, 0, 1, 0, 0, 20));   // side 0 Tank (side 0's flag)
    g.units.push_back(mkUnit(3, 1, 0, 3, 1, 10));   // side 1 Infantry
    g.nextUnitId = 4;
    g.flagUnit[0] = 2;
    g.flagUnit[1] = -1;                             // side 1 designates none

    initSide(g.economy, 0, 400);
    initSide(g.economy, 1, 400);
    Objective f0; f0.hex = offsetToAxial(0, 2); f0.owner = 0; f0.terrainIndex = 1;
    Objective f1; f1.hex = offsetToAxial(4, 2); f1.owner = 1; f1.terrainIndex = 1;
    Objective tn; tn.hex = offsetToAxial(2, 1); tn.owner = OWNER_NEUTRAL;
    tn.terrainIndex = 2;
    g.economy.objectives.push_back(f0);
    g.economy.objectives.push_back(f1);
    g.economy.objectives.push_back(tn);
    g.economy.captureTurns = 1;

    std::string err;
    initMatch(g.turn, 0, kTurnCap, err);
    RulesTables t;
    t.units = &ud;
    t.terrain = &td;
    openTurn(g, t);                 // the same moment `replayLog`'s caller must reach
    return g;
}

// The expectation a Balance Analyst run is written against. Every field is supplied.
static SaveHeaderExpectation fixtureExpectation() {
    SaveHeaderExpectation e;
    e.expectedVersion = kFormatVersion;
    e.rulesCommit     = "part-c";
    e.dataHash        = "1122334455667788";
    e.scenarioHash    = "99aabbccddeeff00";
    return e;
}

// The fixture's own answer to "where is unit `id`", scanned here so no check asks the
// module under test what it thinks a unit's hex is.
static bool hexOfUnitInFixture(const GameState& g, int id, Hex& out) {
    for (std::size_t i = 0; i < g.units.size(); ++i) {
        if (g.units[i].id == id) { out = g.units[i].hex; return true; }
    }
    return false;
}

static bool sameHex(const Hex& a, const Hex& b) { return a.q == b.q && a.r == b.r; }

int main() {
    const std::vector<UnitDef>    ud = fixtureUnits();
    const std::vector<TerrainDef> td = fixtureTerrain();
    RulesTables tables;
    tables.units   = &ud;
    tables.terrain = &td;

    const GameState initial = fixtureState(ud, td);
    std::vector<int> buildlist;
    buildlist.push_back(0);          // Infantry — §2.9's list is DATA the caller supplies
    const int kBudget = 400;

    // ---- the translation, checked directly ---------------------------------------
    // Four small checks over hand-built AiCommands, so a translation defect is named at
    // the clause rather than showing up only as a short match.
    {
        AiCommand a;
        a.kind = AiCommandKind::Move; a.unitId = 1; a.hex = offsetToAxial(2, 1);
        SaveCommand out;
        const bool ok = aiCommandToSave(initial, a, 3, 0, out);
        check("GATE-BALANCE-TRANSLATE-MOVE",
              ok && out.kind == SaveCommandKind::Move && out.unitId == 1 &&
              sameHex(out.hex, offsetToAxial(2, 1)) && out.hasUnit && out.hasHex &&
              out.turn == 3 && out.side == 0);
    }
    {
        // §4.9 spells Attack by TARGET HEX. The expected hex is read out of the fixture
        // here; a translation that records the ATTACKER's hex passes any check that
        // asks the module for it.
        Hex targetHex;
        const bool found = hexOfUnitInFixture(initial, 3, targetHex);
        AiCommand a;
        a.kind = AiCommandKind::Attack; a.unitId = 1; a.targetId = 3;
        SaveCommand out;
        const bool ok = aiCommandToSave(initial, a, 1, 0, out);
        check("GATE-BALANCE-TRANSLATE-ATTACK-IS-TARGET-HEX",
              found && ok && out.kind == SaveCommandKind::Attack && out.unitId == 1 &&
              sameHex(out.hex, targetHex) && out.hasUnit && out.hasHex);
    }
    {
        // Save.h: on a Build entry `unitId` is the unit BUILT, i.e. the defIndex — not
        // the acting unit, of which a Build command has none.
        AiCommand a;
        a.kind = AiCommandKind::Build; a.defIndex = 1; a.hex = offsetToAxial(0, 2);
        SaveCommand out;
        const bool ok = aiCommandToSave(initial, a, 2, 0, out);
        check("GATE-BALANCE-TRANSLATE-BUILD-NAMES-THE-UNIT-BUILT",
              ok && out.kind == SaveCommandKind::Build && out.unitId == 1 &&
              sameHex(out.hex, offsetToAxial(0, 2)) && out.hasUnit && out.hasHex);
    }
    {
        AiCommand a;
        a.kind = AiCommandKind::EndTurn;
        SaveCommand out;
        const bool ok = aiCommandToSave(initial, a, 4, 1, out);
        check("GATE-BALANCE-TRANSLATE-ENDTURN-NAMES-NEITHER",
              ok && out.kind == SaveCommandKind::EndTurn && !out.hasUnit && !out.hasHex &&
              out.turn == 4 && out.side == 1);
    }

    // ---- the run -------------------------------------------------------------------
    const SelfPlayResult run = playSelfPlay(initial, tables, ud, td, buildlist, kBudget);

    check("GATE-BALANCE-RUN-ENDS-WITH-A-TIER",
          run.stop == SelfPlayStop::Ended &&
          run.result.tier != ResultTier::InProgress &&
          run.result.cause != ResultCause::None &&
          !run.commandLog.empty());
    if (run.stop != SelfPlayStop::Ended) {
        std::printf("      (run stopped: %s)\n",
                    run.reason.empty() ? "command budget exhausted" : run.reason.c_str());
    }
    std::printf("      (log: %d commands over a %d-turn cap)\n",
                static_cast<int>(run.commandLog.size()), kTurnCap);

    // Every kind the AI can emit is present, so "no dialect drift" is asserted over a
    // log that actually carries the vocabulary rather than over a log of EndTurns.
    {
        bool sawMove = false, sawAttack = false, sawBuild = false, sawEnd = false;
        bool sawCapture = false;
        for (std::size_t i = 0; i < run.commandLog.size(); ++i) {
            switch (run.commandLog[i].kind) {
            case SaveCommandKind::Move:    sawMove = true; break;
            case SaveCommandKind::Attack:  sawAttack = true; break;
            case SaveCommandKind::Build:   sawBuild = true; break;
            case SaveCommandKind::Capture: sawCapture = true; break;
            case SaveCommandKind::EndTurn: sawEnd = true; break;
            }
        }
        check("GATE-BALANCE-COMMAND-SET-IS-THE-AIS-FOUR",
              sawMove && sawAttack && sawBuild && sawEnd && !sawCapture);
    }

    // Every logged entry is one the rules ACCEPTED. Re-applied here one at a time from
    // the initial state, which is a different path from `replayLog`'s all-or-nothing.
    {
        GameState g = initial;
        bool allAccepted = true;
        bool tagsLive    = true;
        for (std::size_t i = 0; i < run.commandLog.size(); ++i) {
            const SaveCommand& c = run.commandLog[i];
            if (g.turn.running &&
                (c.turn != g.turn.turnNumber || c.side != g.turn.activeSide))
                tagsLive = false;
            if (!applyCommand(g, c, tables).ok) { allAccepted = false; break; }
        }
        check("GATE-BALANCE-LOG-HOLDS-ONLY-ACCEPTED-COMMANDS", allAccepted);
        check("GATE-BALANCE-ENTRY-TAGS-ARE-THE-LIVE-TURN-AND-SIDE", tagsLive);
    }

    // ---- T-SAVE-07, clause by clause -----------------------------------------------
    const SaveHeaderExpectation expect = fixtureExpectation();
    const Save   file = selfPlaySave(run, expect, "selfplay_fixture");
    const std::string text = serializeSave(file);

    {
        Save back;
        const SaveLoadResult lr = loadSave(text, "balance", expect, back);
        check("T-SAVE-07 (a) the self-play log VALIDATES as a save file", lr.ok);
        if (!lr.ok) std::printf("      (%s: %s)\n", lr.failedId.c_str(), lr.reason.c_str());
    }
    {
        // REPLAYS: parse the emitted text, replay its log from the same opened state the
        // producing run began at, and land on the same canonical hash — compared against
        // the run's own final state AND against the header the file carries.
        Save back;
        const SaveLoadResult lr = loadSave(text, "balance", expect, back);
        GameState g = initial;
        const ReplayResult rr = replayLog(g, back.commandLog, tables);
        const std::string replayed = canonicalStateHash(g);
        const std::string produced = canonicalStateHash(run.final);
        check("T-SAVE-07 (b) it REPLAYS to the producing run's canonical state hash",
              lr.ok && rr.ok && rr.applied == static_cast<int>(back.commandLog.size()) &&
              replayed == produced && replayed == back.stateHash);
        if (!rr.ok)
            std::printf("      (replay stopped at %d: %s)\n", rr.failedIndex,
                        rr.reason.c_str());
    }
    {
        // NO DIALECT DRIFT: one format. The text the producer emits re-serialises
        // byte-identically after a round trip, and every kind in it is one the shared
        // spelling table names.
        Save back;
        const SaveLoadResult lr = loadSave(text, "balance", expect, back);
        const std::string again = lr.ok ? serializeSave(back) : std::string();
        bool spelled = true;
        for (std::size_t i = 0; i < file.commandLog.size(); ++i) {
            const char* n = saveCommandName(file.commandLog[i].kind);
            if (n == nullptr || *n == '\0') spelled = false;
        }
        check("T-SAVE-07 (c) NO DIALECT DRIFT: the round trip is byte-identical",
              lr.ok && again == text && spelled &&
              back.commandLog.size() == file.commandLog.size());
    }

    // ---- determinism ---------------------------------------------------------------
    {
        const SelfPlayResult r2 =
            playSelfPlay(initial, tables, ud, td, buildlist, kBudget);
        bool same = r2.commandLog.size() == run.commandLog.size();
        for (std::size_t i = 0; same && i < r2.commandLog.size(); ++i) {
            const SaveCommand& a = run.commandLog[i];
            const SaveCommand& b = r2.commandLog[i];
            if (a.kind != b.kind || a.unitId != b.unitId || a.turn != b.turn ||
                a.side != b.side || !sameHex(a.hex, b.hex)) same = false;
        }
        check("GATE-BALANCE-DETERMINISM-TWO-RUNS-ARE-IDENTICAL",
              same && canonicalStateHash(r2.final) == canonicalStateHash(run.final) &&
              serializeSave(selfPlaySave(r2, expect, "selfplay_fixture")) == text);
    }

    // ---- what did not run ------------------------------------------------------------
    std::printf("\nNOT RUN  T-SAVE-06 — stateHash stability across the headless and "
                "in-engine builds. It is row 10's only †, is asserted jointly with "
                "T-INT-02, and both are in-engine, which this suite is not. NOT RUN "
                "here. The "
                "blocker this line used to name is spent — the VENDORED replayer "
                "T-INT-02 needs was vendored at f5fdb69, retiring the deferring "
                "ruling.\n");
    std::printf("NOT RUN  T-SAVE-01..05 — closed by parts (a) and (b) at their own "
                "commits; this suite re-asserts none of them.\n");

    std::printf("\n%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
