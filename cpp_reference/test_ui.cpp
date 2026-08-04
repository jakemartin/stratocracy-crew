// Test Engineer's gate for §4.11 row 8 — UI binding contract (§4.7 Stub 8).
//
// WHAT THIS SUITE DOES NOT COVER, stated up front because a suite that quietly omits
// an ID reads as a complete pass and this one is DELIBERATELY INCOMPLETE. Row 8's
// acceptance set is split across two harnesses: T-UI-01, T-UI-02 and
// GATE-CAP-PARTIAL are headless and run here; T-UI-03 and T-UI-04 are in-editor
// Unreal Automation over widget bindings, marked `†` in §4.11, and NO IN-EDITOR PASS
// EXISTS AT THIS COMMIT. They are printed by name as NOT RUN lines at the end of the
// run, not folded into the tally, and row 8's ledger row does not flip (Q29).
//
// Row 8 depends on rows 5 and 7 and reads rows 1-4 besides; this gate links Hex.cpp,
// Data.cpp, Move.cpp, Combat.cpp, Economy.cpp and Turn.cpp, and takes its move costs
// from data/terrain.csv and its stat blocks from data/units.csv. argv[1] overrides
// the data directory.
//
// Every expectation compared against a value is COMPUTED BY CALLING THE OWNING
// MODULE, never hardcoded: T-UI-01's damage comes from Combat.h and T-UI-02's set
// from Move.h. Comparing the contract to a literal would assert that someone typed
// the same number twice; comparing it to the module is the whole content of "the
// screen cannot disagree with the simulation".
#include "Combat.h"
#include "Data.h"
#include "Economy.h"
#include "Hex.h"
#include "Move.h"
#include "Turn.h"
#include "Ui.h"

#include <cstddef>
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
static void why(const char* what) { std::printf("      (%s)\n", what); }
static void whyInt(const char* what, int v) { std::printf("      (%s: %d)\n", what, v); }

// ---------------------------------------------------------------------------
// the fixture board. Deliberately mixed: a Water column that cannot be entered, a
// Mountains/Woods belt that costs more than 1, and two objectives. A distance filter
// agrees with Move.h on open ground, so a fixture without cost variation and a hard
// block would not discriminate -- T-UI-02 (c) below MEASURES that this one does.
//
//   . Plains(0)  w Woods(1)  M Mountains(2)  ~ Water(3)  T Town(4)  F Factory(6)
// ---------------------------------------------------------------------------
static const int kCols = 7;
static const int kRows = 5;
static const char* const kGlyphs[kRows] = {
    "..M...F",
    ".ww~...",
    "...~T..",
    ".ww~...",
    "..M....",
};

static int terrainIndexForGlyph(char g) {
    switch (g) {
        case '.': return 0;   // Plains
        case 'w': return 1;   // Woods
        case 'M': return 2;   // Mountains
        case '~': return 3;   // Water
        case 'T': return 4;   // Town
        case 'F': return 6;   // Factory
        default:  return 0;
    }
}

static Board buildBoard(const std::vector<UiUnit>& units) {
    Board b;
    b.bounds.cols = kCols;
    b.bounds.rows = kRows;
    b.terrain.assign(static_cast<std::size_t>(kCols) * kRows, 0);
    b.occupant.assign(static_cast<std::size_t>(kCols) * kRows, OCCUPANT_NONE);
    for (int row = 0; row < kRows; ++row)
        for (int col = 0; col < kCols; ++col)
            b.terrain[static_cast<std::size_t>(row) * kCols + col] =
                terrainIndexForGlyph(kGlyphs[row][col]);
    for (const UiUnit& u : units) {
        const int i = b.index(u.hex);
        if (i >= 0) b.occupant[static_cast<std::size_t>(i)] = u.id;
    }
    return b;
}

static Unit unitFromDef(const UnitDef& d) {
    Unit u;
    u.atk = d.atk; u.def = d.def; u.hp = d.hpMax; u.hpMax = d.hpMax;
    u.rangeMin = d.rangeMin; u.rangeMax = d.rangeMax; u.type = d.type;
    return u;
}

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : "data";
    std::vector<UnitDef>    units;
    std::vector<TerrainDef> terrain;
    std::string err;
    if (!loadUnits(dir + "/units.csv", units, err)) {
        std::printf("FATAL loading units.csv: %s\n", err.c_str());
        return 1;
    }
    if (!loadTerrain(dir + "/terrain.csv", terrain, err)) {
        std::printf("FATAL loading terrain.csv: %s\n", err.c_str());
        return 1;
    }

    std::printf("=== §4.11 row 8 — UI binding contract (§4.7 Stub 8) ===\n");
    std::printf("Headless half only. T-UI-03 and T-UI-04 are in-editor and do not run;\n");
    std::printf("they are named at the end. Row 8's ledger row does not flip (Q29).\n\n");

    // -----------------------------------------------------------------------
    // T-UI-01 — forecast = resolution
    // -----------------------------------------------------------------------
    std::printf("-- T-UI-01  forecast = resolution ------------------------------------\n");
    {
        // Sweep every ordered pair of passable hexes against every ordered pair of
        // §2.4 rows. Legal and illegal placements both, so range refusal is swept
        // too and not only the agreeing case.
        std::vector<Hex> placeable;
        for (int row = 0; row < kRows; ++row)
            for (int col = 0; col < kCols; ++col)
                if (terrain[static_cast<std::size_t>(terrainIndexForGlyph(kGlyphs[row][col]))]
                        .moveCost > 0)
                    placeable.push_back(offsetToAxial(col, row));

        int swept = 0, legal = 0, mismatched = 0, counters = 0;
        for (std::size_t ai = 0; ai < units.size(); ++ai) {
            for (std::size_t di = 0; di < units.size(); ++di) {
                for (const Hex& ha : placeable) {
                    for (const Hex& hd : placeable) {
                        if (hexEqual(ha, hd)) continue;
                        UiUnit a; a.id = 1; a.side = 0; a.defIndex = static_cast<int>(ai);
                        a.hex = ha; a.unit = unitFromDef(units[ai]);
                        UiUnit d; d.id = 2; d.side = 1; d.defIndex = static_cast<int>(di);
                        d.hex = hd; d.unit = unitFromDef(units[di]);

                        UiWorld w;
                        w.units = { a, d };
                        w.board = buildBoard(w.units);
                        w.unitDefs = &units; w.terrain = &terrain;
                        EconomyState e; TurnState t;
                        w.economy = &e; w.turn = &t;

                        const UiForecast f = uiForecast(w, 1, hd);
                        ++swept;

                        // The expectation, computed by calling Combat.h directly.
                        const int dist = hexDistance(ha, hd);
                        const bool inRange = (dist >= units[ai].rangeMin &&
                                              dist <= units[ai].rangeMax);
                        if (!inRange) {
                            if (f.legal) ++mismatched;
                            continue;
                        }
                        ++legal;
                        const int dTerr = w.board.terrainAt(hd);
                        const int aTerr = w.board.terrainAt(ha);
                        const int expDamage = resolveDamage(
                            a.unit, d.unit, terrain[static_cast<std::size_t>(dTerr)].defensePct);
                        const int hpAfter = d.unit.hp - expDamage;
                        const bool expDies = (hpAfter <= 0);
                        bool expCounter = false;
                        int  expCounterDmg = 0;
                        if (!expDies) {
                            Unit wounded = d.unit;
                            wounded.hp = hpAfter;
                            if (defenderCanCounter(wounded, dist)) {
                                expCounter = true;
                                expCounterDmg = resolveDamage(
                                    wounded, a.unit,
                                    terrain[static_cast<std::size_t>(aTerr)].defensePct);
                            }
                        }
                        if (expCounter) ++counters;

                        if (!f.legal || f.damage != expDamage || f.distance != dist ||
                            f.defenderDies != expDies || f.counterFires != expCounter ||
                            f.counterDamage != expCounterDmg) {
                            ++mismatched;
                        }
                    }
                }
            }
        }
        check("T-UI-01 (a) every forecast equals a direct Combat.h computation",
              mismatched == 0);
        whyInt("placements swept", swept);
        whyInt("legal forecasts among them", legal);
        whyInt("of those, forecasts in which a counter fires", counters);
        whyInt("mismatches against resolveDamage / defenderCanCounter", mismatched);
        check("T-UI-01 (a2) the sweep actually exercised counters", counters > 0);
    }
    {
        // The other end: what a resolution SPENDS is the forecast's own numbers, and
        // the HP it lands on is what Combat.h says. Measuring only the forecast end
        // would leave "identical numbers, mechanically" half-asserted.
        UiUnit a; a.id = 1; a.side = 0; a.defIndex = 1;              // Tank
        a.hex = offsetToAxial(4, 4); a.unit = unitFromDef(units[1]);
        UiUnit d; d.id = 2; d.side = 1; d.defIndex = 1;              // Tank, survives
        d.hex = offsetToAxial(5, 4); d.unit = unitFromDef(units[1]);
        UiWorld w; w.units = { a, d }; w.board = buildBoard(w.units);
        w.unitDefs = &units; w.terrain = &terrain;
        EconomyState e; TurnState t; w.economy = &e; w.turn = &t;

        const UiForecast   f = uiForecast(w, 1, d.hex);
        const UiResolution r = uiResolveForGate(w, 1, d.hex);
        const int dTerr = w.board.terrainAt(d.hex);
        const int expDamage = resolveDamage(a.unit, d.unit,
                                            terrain[static_cast<std::size_t>(dTerr)].defensePct);

        check("T-UI-01 (b) the resolution spends exactly the forecast's damage",
              r.applied && f.legal &&
              r.defenderHpAfter == d.unit.hp - f.damage &&
              f.damage == expDamage);
        whyInt("forecast damage", f.damage);
        whyInt("Combat.h damage", expDamage);
        whyInt("defender hp after", r.defenderHpAfter);
        check("T-UI-01 (c) the forecast a caller reads twice does not move",
              uiForecast(w, 1, d.hex).damage == f.damage &&
              uiForecast(w, 1, d.hex).counterDamage == f.counterDamage);
    }

    // -----------------------------------------------------------------------
    // T-UI-02 — the reachable-hex highlight
    // -----------------------------------------------------------------------
    std::printf("\n-- T-UI-02  highlight = the T-MOVE-01 set ----------------------------\n");
    {
        int compared = 0, divergent = 0;
        for (std::size_t di = 0; di < units.size(); ++di) {
            for (int row = 0; row < kRows; ++row) {
                for (int col = 0; col < kCols; ++col) {
                    const int ti = terrainIndexForGlyph(kGlyphs[row][col]);
                    if (terrain[static_cast<std::size_t>(ti)].moveCost <= 0) continue;
                    UiUnit u; u.id = 1; u.side = 0; u.defIndex = static_cast<int>(di);
                    u.hex = offsetToAxial(col, row); u.unit = unitFromDef(units[di]);
                    UiWorld w; w.units = { u }; w.board = buildBoard(w.units);
                    w.unitDefs = &units; w.terrain = &terrain;
                    EconomyState e; TurnState t; w.economy = &e; w.turn = &t;

                    const std::vector<ReachEntry> got = uiReachable(w, 1);
                    const std::vector<ReachEntry> exp =
                        reachable(w.board, terrain, u.hex, units[di].move);
                    ++compared;
                    bool same = (got.size() == exp.size());
                    for (std::size_t k = 0; same && k < exp.size(); ++k)
                        same = hexEqual(got[k].hex, exp[k].hex) && got[k].cost == exp[k].cost;
                    if (!same) ++divergent;
                }
            }
        }
        check("T-UI-02 (a) the highlight is Move.h's set, hex for hex and cost for cost",
              divergent == 0);
        whyInt("start hexes x unit rows compared", compared);
        whyInt("divergences", divergent);
    }
    {
        // Q3's conservative reading is in force: ANY other unit blocks pathing
        // entirely. The highlight must shrink with it, because it is not the
        // highlight's rule to have.
        UiUnit u; u.id = 1; u.side = 0; u.defIndex = 0;               // Infantry, move 3
        u.hex = offsetToAxial(1, 2); u.unit = unitFromDef(units[0]);
        UiWorld openW; openW.units = { u }; openW.board = buildBoard(openW.units);
        openW.unitDefs = &units; openW.terrain = &terrain;
        EconomyState e; TurnState t; openW.economy = &e; openW.turn = &t;
        const std::size_t openCount = uiReachable(openW, 1).size();

        UiUnit blocker; blocker.id = 2; blocker.side = 1; blocker.defIndex = 1;
        blocker.hex = offsetToAxial(2, 2); blocker.unit = unitFromDef(units[1]);
        UiWorld blockedW; blockedW.units = { u, blocker };
        blockedW.board = buildBoard(blockedW.units);
        blockedW.unitDefs = &units; blockedW.terrain = &terrain;
        blockedW.economy = &e; blockedW.turn = &t;
        const std::vector<ReachEntry> gotBlocked = uiReachable(blockedW, 1);
        const std::vector<ReachEntry> expBlocked =
            reachable(blockedW.board, terrain, u.hex, units[0].move);

        bool same = (gotBlocked.size() == expBlocked.size());
        for (std::size_t k = 0; same && k < expBlocked.size(); ++k)
            same = hexEqual(gotBlocked[k].hex, expBlocked[k].hex) &&
                   gotBlocked[k].cost == expBlocked[k].cost;
        check("T-UI-02 (b) an occupant shrinks the highlight, and it is still Move.h's set",
              same && gotBlocked.size() < openCount);
        whyInt("hexes highlighted with the lane open", static_cast<int>(openCount));
        whyInt("hexes highlighted with an occupant at (2,2)", static_cast<int>(gotBlocked.size()));
    }
    {
        // The fixture must be able to FAIL this invariant, or (a) and (b) assert
        // nothing. A plain distance filter -- the reading T-UI-02 exists to refuse --
        // is computed here and shown to differ on this board.
        UiUnit u; u.id = 1; u.side = 0; u.defIndex = 0;               // Infantry, move 3
        u.hex = offsetToAxial(1, 2); u.unit = unitFromDef(units[0]);
        UiWorld w; w.units = { u }; w.board = buildBoard(w.units);
        w.unitDefs = &units; w.terrain = &terrain;
        EconomyState e; TurnState t; w.economy = &e; w.turn = &t;

        int withinDistance = 0;
        for (int row = 0; row < kRows; ++row)
            for (int col = 0; col < kCols; ++col)
                if (hexDistance(u.hex, offsetToAxial(col, row)) <= units[0].move)
                    ++withinDistance;
        const int actual = static_cast<int>(uiReachable(w, 1).size());
        check("T-UI-02 (c) the fixture discriminates: distance filter != Move.h's set",
              withinDistance != actual);
        whyInt("hexes within Move by hex distance", withinDistance);
        whyInt("hexes Move.h actually reaches", actual);
    }

    // -----------------------------------------------------------------------
    // GATE-CAP-PARTIAL — §2.8's T-CAP-05
    // -----------------------------------------------------------------------
    std::printf("\n-- GATE-CAP-PARTIAL  a capture in progress counts for nobody ---------\n");
    {
        // N >= 2 is required for a partial state to EXIST, and the shipped scenario
        // ships N = 1 (§2.7), so this fixture configures captureTurns = 2. N is
        // per-scenario data; this is a property of the shipped map, not a licence to
        // invent one, and the state asserted about is one Ferrum Crossing cannot
        // reach. Stated here rather than left to be inferred from the fixture.
        const Hex town    = offsetToAxial(4, 2);
        const Hex factory = offsetToAxial(6, 0);

        UiUnit inf; inf.id = 1; inf.side = 0; inf.defIndex = 0;       // Infantry, canCapture
        inf.hex = town; inf.unit = unitFromDef(units[0]);

        EconomyState e;
        e.captureTurns = 2;
        Objective t1; t1.hex = town;    t1.owner = OWNER_NEUTRAL; t1.terrainIndex = 4;
        Objective f1; f1.hex = factory; f1.owner = 1;              f1.terrainIndex = 6;
        e.objectives = { t1, f1 };
        initSide(e, 0, 200);
        initSide(e, 1, 200);

        TurnState t;
        std::string terr;
        initMatch(t, 0, 20, terr);

        UiWorld w; w.units = { inf }; w.board = buildBoard(w.units);
        w.unitDefs = &units; w.terrain = &terrain; w.economy = &e; w.turn = &t;

        const UiSnapshot before = buildUiSnapshot(w);

        std::vector<CaptureOccupant> occ;
        CaptureOccupant o; o.hex = town; o.unitId = inf.id; o.side = 0; o.canCapture = true;
        occ.push_back(o);
        const std::vector<Hex> flipped = captureTick(e, occ, 0);
        const UiSnapshot mid = buildUiSnapshot(w);

        const UiUnitView* vBefore = findUiUnitView(before, 1);
        const UiUnitView* vMid    = findUiUnitView(mid, 1);

        check("GATE-CAP-PARTIAL (a) progress short of completion leaves BOTH sides' "
              "objectivesHeld unchanged",
              flipped.empty() &&
              mid.side[0].objectivesHeld == before.side[0].objectivesHeld &&
              mid.side[1].objectivesHeld == before.side[1].objectivesHeld);
        whyInt("side 0 objectivesHeld before", before.side[0].objectivesHeld);
        whyInt("side 0 objectivesHeld with a capture in progress", mid.side[0].objectivesHeld);
        whyInt("side 1 objectivesHeld before", before.side[1].objectivesHeld);
        whyInt("side 1 objectivesHeld with a capture in progress", mid.side[1].objectivesHeld);

        // The differential's other half. Without this an implementation that changes
        // NOTHING passes (a) while breaking the game.
        check("GATE-CAP-PARTIAL (b) the unit's captureProgress did rise in that same step",
              vBefore != nullptr && vMid != nullptr &&
              vBefore->captureProgress == 0 && vMid->captureProgress == 1);
        whyInt("captureProgress before", vBefore != nullptr ? vBefore->captureProgress : -1);
        whyInt("captureProgress after one tick", vMid != nullptr ? vMid->captureProgress : -1);

        // And the invariant is not vacuous: the same fixture DOES flip on completion.
        const std::vector<Hex> flipped2 = captureTick(e, occ, 0);
        const UiSnapshot after = buildUiSnapshot(w);
        const UiUnitView* vAfter = findUiUnitView(after, 1);
        check("GATE-CAP-PARTIAL (c) completion DOES move objectivesHeld, and clears progress",
              flipped2.size() == 1 &&
              after.side[0].objectivesHeld == before.side[0].objectivesHeld + 1 &&
              after.side[1].objectivesHeld == before.side[1].objectivesHeld &&
              vAfter != nullptr && vAfter->captureProgress == 0);
        whyInt("side 0 objectivesHeld after the flip", after.side[0].objectivesHeld);
        whyInt("objectiveTotal (the N of X of N)", after.objectiveTotal);
    }

    // -----------------------------------------------------------------------
    // The snapshot itself. Not a numbered invariant -- see the note at the end.
    // -----------------------------------------------------------------------
    std::printf("\n-- snapshot shape (ungated; see the note below) ----------------------\n");
    {
        UiUnit a; a.id = 7; a.side = 0; a.defIndex = 0;
        a.hex = offsetToAxial(0, 4); a.unit = unitFromDef(units[0]);
        UiUnit b; b.id = 3; b.side = 1; b.defIndex = 1;
        b.hex = offsetToAxial(1, 4); b.unit = unitFromDef(units[1]);
        EconomyState e; initSide(e, 0, 200); initSide(e, 1, 200);
        TurnState t; std::string terr; initMatch(t, 0, 20, terr);

        UiWorld w; w.units = { a, b }; w.board = buildBoard(w.units);
        w.unitDefs = &units; w.terrain = &terrain; w.economy = &e; w.turn = &t;

        // The two flags are INDEPENDENT (T-TURN-01). Spend one and the other must not
        // move -- one snapshot field could not express this state at all, which is the
        // drift this row's GDD half repaired.
        BoardSnapshot bs; bs.factoryTotal = 1;
        beginTurn(t, bs);
        // The flags are only spendable in the Actions phase, and it is repair that
        // advances into it (T-TURN-08's moment). An empty subject list heals nobody
        // and still advances, which is what this fixture wants.
        applyStartOfTurnRepair(t, std::vector<RepairSubject>());
        std::string e1;
        const bool moved = markMoved(t, 7, 0, e1);
        if (!moved) std::printf("      (markMoved refused: %s)\n", e1.c_str());
        const UiSnapshot s = buildUiSnapshot(w);
        const UiUnitView* v = findUiUnitView(s, 7);
        check("snapshot carries hasMoved and hasActed as two independent fields",
              v != nullptr && v->hasMoved && !v->hasActed);
        std::string e2;
        const bool acted = markActed(t, 7, 0, e2);
        if (!acted) std::printf("      (markActed refused: %s)\n", e2.c_str());
        const UiUnitView* v2 = findUiUnitView(buildUiSnapshot(w), 7);
        check("spending the act flag leaves the move flag spent, not replaced",
              v2 != nullptr && v2->hasMoved && v2->hasActed);
        check("units are projected in ascending id, hexes in canonical order",
              s.units.size() == 2 && s.units[0].id == 3 && s.units[1].id == 7 &&
              s.hexes.size() == static_cast<std::size_t>(kCols) * kRows &&
              hexLess(s.hexes[0].hex, s.hexes[1].hex));
        check("match view reads turn, cap, side to move, and no result yet",
              s.match.turn == 1 && s.match.turnCap == 20 &&
              s.match.sideToMove == 0 && !s.match.hasResult);
    }

    // -----------------------------------------------------------------------
    std::printf("\n");
    std::printf("NOT RUN  T-UI-03 -- the live standings scoreboard binds 1:1 to snapshot\n");
    std::printf("         fields with no widget-side arithmetic. In-editor Unreal\n");
    std::printf("         Automation over widget bindings (§4.7 Stub 8, Acceptance;\n");
    std::printf("         marked † in §4.11), and no in-editor pass exists at this\n");
    std::printf("         commit. WRITTEN, UNBLOCKED and ASSERTING: what it lacks is a\n");
    std::printf("         harness, not a rule.\n");
    std::printf("NOT RUN  T-UI-04 -- the production menu binds to the buildlist derived\n");
    std::printf("         from the four Stub-2 unit rows plus current fameTotal, and the\n");
    std::printf("         flag never appears. Same reason, same state. No buildlist query\n");
    std::printf("         is offered by this module: whether it reaches the UI as a\n");
    std::printf("         snapshot field or a query is stated nowhere, and inventing one\n");
    std::printf("         here would pre-empt a Director ruling.\n");
    std::printf("\n");
    std::printf("NOTE     The four snapshot-shape checks above are NOT gated by any\n");
    std::printf("         numbered ID, and are not counted as one. No written invariant\n");
    std::printf("         asserts that the snapshot mirrors the rules modules at all --\n");
    std::printf("         T-UI-01..04 each assert a binding DOWNSTREAM of it. That gap is\n");
    std::printf("         filed as a change request against §4.7 Stub 8's Acceptance line\n");
    std::printf("         and is a Director ruling, since numbering it would move §4.5's\n");
    std::printf("         written-ID count from 70 to 71.\n");
    std::printf("NOTE     GATE-CAP-PARTIAL ran on a fixture with captureTurns = 2. The\n");
    std::printf("         shipped scenario ships N = 1 (§2.7), so Ferrum Crossing cannot\n");
    std::printf("         reach the state this gate asserts about. N is per-scenario data\n");
    std::printf("         and the fixture configures it; no map was invented.\n");

    std::printf("\n%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
