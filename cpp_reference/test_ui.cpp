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
    // T-UI-05 — snapshot fidelity (§4.7 Stub 8, minted 2026-08-04)
    //
    // The check under test recomputes every expected value from the owning module and
    // never calls buildUiSnapshot, so it CAN disagree with the projection. (d) below
    // measures that it does: a check that agreed with everything would make (a), (b)
    // and (c) assert nothing, which is the same trap T-UI-02 (c) exists to close.
    // -----------------------------------------------------------------------
    std::printf("\n-- T-UI-05  snapshot fidelity ----------------------------------------\n");
    {
        // A fixture with something in every group: two sides, a Factory and a Town as
        // objectives, a guided-opening seat, and a unit that will move.
        std::vector<UiUnit> us;
        {
            UiUnit a; a.id = 1; a.side = 0; a.defIndex = 0;      // Infantry, the marked seat
            a.hex = offsetToAxial(1, 2); a.placement = a.hex;
            a.unit = unitFromDef(units[0]);
            us.push_back(a);
            UiUnit b; b.id = 2; b.side = 1; b.defIndex = 1;      // Tank
            b.hex = offsetToAxial(5, 2); b.placement = b.hex;
            b.unit = unitFromDef(units[1]);
            us.push_back(b);
        }
        UiWorld w;
        w.units = us;
        w.board = buildBoard(w.units);
        w.unitDefs = &units;
        w.terrain  = &terrain;

        EconomyState econ;
        econ.captureTurns = 2;
        econ.objectives.push_back(Objective{offsetToAxial(6, 0), 0, 6});   // Factory, side 0
        econ.objectives.push_back(Objective{offsetToAxial(4, 2), 1, 4});   // Town, side 1
        initSide(econ, 0, 200);
        initSide(econ, 1, 200);
        w.economy = &econ;

        // The guided-opening seat names side 0's Infantry BY ITS DEPLOYMENT HEX.
        std::vector<ScenarioGuided> guided;
        {
            ScenarioGuided g; g.side = 0; g.infantry = offsetToAxial(1, 2);
            g.objective = offsetToAxial(6, 0);
            guided.push_back(g);
        }
        w.guided = &guided;

        TurnState t;
        std::string terr;
        initMatch(t, 0, 20, terr);
        w.turn = &t;
        BoardSnapshot bs; bs.factoryTotal = 1;
        beginTurn(t, bs);
        applyStartOfTurnRepair(t, std::vector<RepairSubject>());

        // (a) the projection is faithful, and the check saw every field.
        {
            const UiSnapshot s = buildUiSnapshot(w);
            const UiFidelityResult f = uiCheckSnapshotFidelity(w, s);
            check("T-UI-05 (a) the projection is faithful under all three clauses", f.ok);
            if (!f.ok)
                for (const UiFidelityFailure& x : f.failures)
                    std::printf("      (clause %s  %s: %s)\n",
                                x.clause.c_str(), x.field.c_str(), x.detail.c_str());
            check("T-UI-05 (a2) the check examined both kinds, not just one",
                  f.mirrorsChecked > 0 && f.derivedChecked > 0);
            whyInt("mirrors checked", f.mirrorsChecked);
            whyInt("declared-derived checked", f.derivedChecked);
            whyInt("fields enumerated", f.fieldsEnumerated);
        }

        // (b) the contract IS the stub's field list: 27 fields, 22 mirror, 5 derived.
        {
            const std::vector<UiFieldContractEntry>& c = uiFieldContract();
            int mirrors = 0, deriveds = 0;
            for (const UiFieldContractEntry& e : c)
                (e.kind == UiFieldKind::Mirror ? mirrors : deriveds) += 1;
            check("T-UI-05 (b) the contract transcribes Stub 8's field list exactly",
                  static_cast<int>(c.size()) == kUiSnapshotFieldCount &&
                  mirrors == kUiMirrorFieldCount && deriveds == kUiDerivedFieldCount);
            bool everyRowStatesASource = true;
            for (const UiFieldContractEntry& e : c)
                if (e.source == nullptr || *e.source == '\0') everyRowStatesASource = false;
            check("T-UI-05 (b2) every contract row names a module-side value or states "
                  "a derivation", everyRowStatesASource);
        }

        // (c) THE DISCRIMINATION TEST. Each clause must reject the defect it exists
        // for. Without this the suite would pass against a check that returned ok
        // unconditionally, and would be measuring nothing at all.
        {
            const UiSnapshot good = buildUiSnapshot(w);

            UiSnapshot badMirror = good;                 // clause (a): a mirror that lies
            badMirror.units[0].hp += 1;
            const UiFidelityResult fa = uiCheckSnapshotFidelity(w, badMirror);
            bool caughtA = false;
            for (const UiFidelityFailure& x : fa.failures) if (x.clause == "a") caughtA = true;
            check("T-UI-05 (c) clause (a) rejects a mirror that does not equal the module",
                  !fa.ok && caughtA);

            UiSnapshot badDerived = good;                // clause (b): a wrong derivation
            badDerived.side[0].incomePerTurn += 25;
            const UiFidelityResult fb = uiCheckSnapshotFidelity(w, badDerived);
            bool caughtB = false;
            for (const UiFidelityFailure& x : fb.failures) if (x.clause == "b") caughtB = true;
            check("T-UI-05 (c) clause (b) rejects a derivation the stub does not state",
                  !fb.ok && caughtB);

            // A derived field READ BACK from the snapshot rather than recomputed would
            // pass any comparison. This is the case that proves it is not: the guided
            // mark is flipped on a unit the scenario does not name.
            UiSnapshot badMark = good;
            for (UiUnitView& v : badMark.units) v.isGuidedMarked = !v.isGuidedMarked;
            const UiFidelityResult fm = uiCheckSnapshotFidelity(w, badMark);
            check("T-UI-05 (c) clause (b) rejects a guided mark the scenario does not name",
                  !fm.ok);

            UiSnapshot badShape = good;                  // clause (c): a group that lost a row
            if (!badShape.factories.empty()) badShape.factories.pop_back();
            const UiFidelityResult fc = uiCheckSnapshotFidelity(w, badShape);
            check("T-UI-05 (c) clause (c) rejects a snapshot whose shape left the contract",
                  !fc.ok);
        }

        // (d) RULING G — the standing rate, not turn 1's accrual. Side 0 holds one
        // Factory, so the rate is 100 while Q8(a) pays 0 on turn 1. A fixture where
        // the two agreed would not discriminate, and this asserts that they differ.
        {
            const UiSnapshot s = buildUiSnapshot(w);
            EconomyState probe = econ;
            const int paidOnTurn1 = accrueIncome(probe, terrain, 0, /*turnNumber=*/1);
            check("T-UI-05 (d) incomePerTurn is the STANDING rate on turn 1, not the "
                  "accrual", s.side[0].incomePerTurn == 100 && paidOnTurn1 == 0);
            whyInt("standing rate", s.side[0].incomePerTurn);
            whyInt("accrued on turn 1", paidOnTurn1);
            check("T-UI-05 (d2) the Town rate is read from the table too, not hardcoded "
                  "to factories", s.side[1].incomePerTurn == 25);
        }

        // (e) RULING F/J — the mark is a property of the PLACEMENT. It must survive the
        // marked unit moving, which is the one thing beat 1a asks it to do.
        {
            const UiUnitView* before = findUiUnitView(buildUiSnapshot(w), 1);
            check("T-UI-05 (e) the scenario's guided seat is marked, and only it",
                  before != nullptr && before->isGuidedMarked &&
                  findUiUnitView(buildUiSnapshot(w), 2) != nullptr &&
                  !findUiUnitView(buildUiSnapshot(w), 2)->isGuidedMarked);

            UiWorld moved = w;
            moved.units[0].hex = offsetToAxial(2, 2);      // placement deliberately unchanged
            moved.board = buildBoard(moved.units);
            const UiSnapshot s2 = buildUiSnapshot(moved);
            const UiUnitView* after = findUiUnitView(s2, 1);
            check("T-UI-05 (e2) the mark does not move when the unit does",
                  after != nullptr && after->isGuidedMarked &&
                  !hexEqual(after->hex, moved.units[0].placement));
            check("T-UI-05 (e3) and the moved world is still faithful",
                  uiCheckSnapshotFidelity(moved, s2).ok);
        }

        // (f) RULING E — spawnBlocked and buildWaiting are DISTINCT, and the case that
        // separates them is a boxed-in factory with NOTHING QUEUED. buildWaiting alone
        // cannot express it, which is why the field was ruled.
        {
            UiWorld boxed = w;
            // The Factory at (6,0) is a corner: three in-bounds neighbours. Fill the
            // factory hex and all three, and no build could place.
            const int cols[4] = {6, 5, 5, 6};
            const int rows[4] = {0, 0, 1, 1};
            boxed.units.clear();
            for (int i = 0; i < 4; ++i) {
                UiUnit u; u.id = 10 + i; u.side = 0; u.defIndex = 0;
                u.hex = offsetToAxial(cols[i], rows[i]); u.placement = u.hex;
                u.unit = unitFromDef(units[0]);
                boxed.units.push_back(u);
            }
            boxed.board = buildBoard(boxed.units);
            const UiSnapshot s = buildUiSnapshot(boxed);
            check("T-UI-05 (f) a boxed-in factory reports spawnBlocked with NOTHING "
                  "queued, which buildWaiting alone cannot express",
                  s.factories.size() == 1 && s.factories[0].spawnBlocked &&
                  !s.factories[0].buildWaiting);
            check("T-UI-05 (f2) and the derivation still agrees with the check",
                  uiCheckSnapshotFidelity(boxed, s).ok);

            // The fixture must be able to report NOT blocked, or (f) asserts only that
            // the field exists.
            UiWorld freed = boxed;
            freed.units.pop_back();
            freed.board = buildBoard(freed.units);
            const UiSnapshot s2 = buildUiSnapshot(freed);
            check("T-UI-05 (f3) the fixture discriminates: freeing one neighbour clears "
                  "spawnBlocked",
                  s2.factories.size() == 1 && !s2.factories[0].spawnBlocked);
        }

        // (g) THE STUB'S OWN ASSERTION METHOD: rebuild the snapshot after each command
        // of a fixed command sequence and compare every field under (a), (b) and (c).
        {
            int steps = 0, faithful = 0;
            auto step = [&](const char* what) {
                ++steps;
                const UiSnapshot s = buildUiSnapshot(w);
                const UiFidelityResult f = uiCheckSnapshotFidelity(w, s);
                if (f.ok) ++faithful;
                else {
                    std::printf("      (after %s:)\n", what);
                    for (const UiFidelityFailure& x : f.failures)
                        std::printf("        clause %s  %s: %s\n",
                                    x.clause.c_str(), x.field.c_str(), x.detail.c_str());
                }
            };

            std::string e;
            markMoved(t, 1, 0, e);                                   step("Move");
            markActed(t, 1, 0, e);                                   step("Attack");
            queueBuild(econ, units, terrain, 0, offsetToAxial(6, 0), 0, e);
                                                                     step("Build (queued)");
            markBuilt(t, offsetToAxial(6, 0), 0, e);                 step("Build (allowance)");
            // The queued build now holds the factory's slot; the snapshot must say so.
            const UiSnapshot mid = buildUiSnapshot(w);
            check("T-UI-05 (g) buildWaiting and hasBuiltThisTurn are read from two "
                  "places and both are true after a queued build",
                  mid.factories.size() == 1 && mid.factories[0].buildWaiting &&
                  mid.factories[0].hasBuiltThisTurn);
            endTurn(t, bs);                                          step("EndTurn");
            beginTurn(t, bs);                                        step("the next turn beginning");

            check("T-UI-05 (g2) the snapshot is faithful after EVERY command of the "
                  "sequence", steps > 0 && faithful == steps);
            whyInt("commands replayed", steps);
        }

        // (h) The presentation block is NOT in this invariant's subject. It has no
        // module-side counterpart, so nothing here compares it -- and its two members
        // have DIFFERENT lifecycles, which is why they are two members and not one.
        {
            UiPresentation p;
            UiPresentationUnit a; a.id = 1; a.done = false; a.lockedThisTurn = true;
            p.units.push_back(a);
            UiViewModel vm;
            vm.snapshot = buildUiSnapshot(w);
            vm.presentation = p;
            check("T-UI-05 (h) a unit can be locked and not done at once, so the block's "
                  "two members are not one field",
                  vm.presentation.units[0].lockedThisTurn && !vm.presentation.units[0].done);
            check("T-UI-05 (h2) the block is outside the invariant's subject: fidelity "
                  "does not read it",
                  uiCheckSnapshotFidelity(w, vm.snapshot).ok);
        }
    }

    // -----------------------------------------------------------------------
    // GATE-BUILDLIST -- §2.11.5's production menu query. T-UI-04's buildlist shape
    // was ruled 2026-08-20 (a query, not a snapshot field) and its three residual
    // questions ruled 2026-08-22; this gates what those rulings actually say.
    //
    // THIS IS NOT T-UI-04, and it is not counted as it. That ID asserts the
    // production MENU BINDS -- in-editor Automation over a widget that does not
    // exist yet -- and row 8's ledger row does not flip on anything below. What runs
    // here is the QUERY the menu will bind to.
    //
    // NO FLAG CLAUSE IS WRITTEN, deliberately. `isFlag` is a Scenario.h PLACEMENT
    // field and data/units.csv has four rows with no flag among them, so a clause
    // asserting the flag's absence from this query's output CANNOT FAIL: it passes on
    // an empty implementation and a wrong one alike. It would be evidence of nothing.
    // -----------------------------------------------------------------------
    std::printf("\n-- GATE-BUILDLIST  the production menu's query (§2.11.5) --------------\n");
    {
        const Hex fac  = offsetToAxial(6, 0);        // 'F' in the fixture board
        const Hex town = offsetToAxial(4, 2);        // 'T' -- an objective, not a build point

        // The purse is DERIVED from the table, never typed: at the cheapest row's
        // cost exactly the cheapest tier is affordable, whatever data/units.csv says.
        int minCost = units[0].costFame, maxCost = units[0].costFame;
        for (const UnitDef& d : units) {
            if (d.costFame < minCost) minCost = d.costFame;
            if (d.costFame > maxCost) maxCost = d.costFame;
        }

        EconomyState econ;
        econ.objectives.push_back(Objective{fac,  0, 6});    // Factory, side 0
        econ.objectives.push_back(Objective{town, 1, 4});    // Town, side 1
        initSide(econ, 0, minCost);
        initSide(econ, 1, minCost);

        TurnState t;
        std::string terr;
        initMatch(t, 0, 20, terr);
        BoardSnapshot bs; bs.factoryTotal = 1;
        beginTurn(t, bs);
        applyStartOfTurnRepair(t, std::vector<RepairSubject>());

        UiWorld w;
        w.board = buildBoard(w.units);                // no units: the factory is open
        w.unitDefs = &units;
        w.terrain  = &terrain;
        w.economy  = &econ;
        w.turn     = &t;

        // Availability must never vary by row -- that invariance is the observable
        // form of "the population cap is AI policy" (ai_spec.md, narrowed 2026-08-22).
        auto uniform = [](const std::vector<UiBuildOption>& v) {
            if (v.empty()) return false;
            for (const UiBuildOption& o : v)
                if (o.available != v[0].available || o.reason != v[0].reason) return false;
            return true;
        };

        const std::vector<UiBuildOption> open = uiBuildOptions(w, 0, fac);

        // The fixture must be able to tell the answers apart, or nothing below bites.
        check("GATE-BUILDLIST (pre) the fixture discriminates: the table has more than "
              "one price, and this factory is NOT boxed in",
              minCost < maxCost && !spawnHexesBlocked(w, fac));

        // -- ruling (a): all four rows, table order, costs mirrored ---------------
        bool rowsMatch = (open.size() == units.size());
        for (std::size_t i = 0; rowsMatch && i < units.size(); ++i)
            rowsMatch = (open[i].defIndex == static_cast<int>(i) &&
                         open[i].id       == units[i].id &&
                         open[i].costFame == units[i].costFame);
        check("GATE-BUILDLIST (a) EVERY unit-table row is returned, in table order, "
              "each mirroring its own id and costFame",
              rowsMatch);
        whyInt("rows returned", static_cast<int>(open.size()));

        // -- ruling (a): affordability is computed HERE and actually splits -------
        int aff = 0, expectAff = 0;
        for (const UiBuildOption& o : open) if (o.affordable) ++aff;
        for (const UnitDef& d : units) if (d.costFame <= minCost) ++expectAff;
        check("GATE-BUILDLIST (b) `affordable` is module-computed (T-UI-03) and "
              "discriminates: it agrees with the table and is neither all nor none",
              aff == expectAff && aff > 0 && aff < static_cast<int>(units.size()));
        whyInt("affordable rows at the cheapest price", aff);

        // -- the healthy case ----------------------------------------------------
        bool allOpen = true;
        for (const UiBuildOption& o : open) allOpen = allOpen && o.available && o.reason.empty();
        check("GATE-BUILDLIST (c) at an owned, idle, un-queued factory every row is "
              "available and carries no reason",
              allOpen && uniform(open));

        // -- ruling (b): hasBuiltThisTurn DOES gate (T-TURN-10) -------------------
        std::vector<UiBuildOption> built;
        {
            TurnState t2 = t;
            std::string e2;
            const bool marked = markBuilt(t2, fac, 0, e2);
            UiWorld w2 = w; w2.turn = &t2;
            built = uiBuildOptions(w2, 0, fac);
            bool closed = marked && !built.empty();
            for (const UiBuildOption& o : built) closed = closed && !o.available;
            check("GATE-BUILDLIST (d) T-TURN-10's spent allowance closes the whole "
                  "menu, in the words the module refuses in",
                  closed && uniform(built) &&
                  built[0].reason == "that factory has already taken its build this turn");
        }

        // -- ruling (b): buildWaiting DOES gate ----------------------------------
        std::vector<UiBuildOption> queued;
        {
            EconomyState e3 = econ;
            PendingBuild pb; pb.factoryHex = fac; pb.side = 0; pb.defIndex = 0;
            e3.pending.push_back(pb);
            UiWorld w3 = w; w3.economy = &e3;
            queued = uiBuildOptions(w3, 0, fac);
            bool closed = !queued.empty();
            for (const UiBuildOption& o : queued) closed = closed && !o.available;
            check("GATE-BUILDLIST (e) a factory already holding a waiting build offers "
                  "no new option, in queueBuild's own words",
                  closed && uniform(queued) &&
                  queued[0].reason == "factory already has a pending build");
        }

        // -- ruling (b) / Q31: spawnBlocked does NOT gate -------------------------
        std::vector<UiBuildOption> boxed;
        {
            UiWorld w4 = w;
            const int fi = w4.board.index(fac);
            if (fi >= 0) w4.board.occupant[static_cast<std::size_t>(fi)] = 901;
            Hex adj[HEX_DIRECTIONS];
            const int an = neighbors(fac, w4.board.bounds, adj);
            for (int i = 0; i < an; ++i) {
                const int ai2 = w4.board.index(adj[i]);
                if (ai2 >= 0) w4.board.occupant[static_cast<std::size_t>(ai2)] = 910 + i;
            }
            // POSITIVE CONTROL. Without this the clause below passes on a fixture that
            // was never boxed in, which is a green light for the opposite ruling.
            check("GATE-BUILDLIST (f-control) the boxed-in fixture really is boxed in: "
                  "spawnHexesBlocked says so",
                  spawnHexesBlocked(w4, fac));

            boxed = uiBuildOptions(w4, 0, fac);
            bool stillOpen = !boxed.empty();
            for (const UiBuildOption& o : boxed) stillOpen = stillOpen && o.available;
            check("GATE-BUILDLIST (f) Q31, RULED: a boxed-in factory still offers every "
                  "row -- spawnBlocked is informational and never folds into availability",
                  stillOpen && uniform(boxed));
        }

        // -- T-TURN-10 is consulted BEFORE queueBuild's ladder, so a query for a side
        //    that is not up is refused there and never reaches ownership ------------
        const std::vector<UiBuildOption> offTurn = uiBuildOptions(w, 1, fac);
        check("GATE-BUILDLIST (g0) a side that is not the active side is refused by "
              "T-TURN-10 first, in markBuilt's words -- the allowance outranks the "
              "ownership question because a caller must consult it first",
              !offTurn.empty() && !offTurn[0].available && uniform(offTurn) &&
              offTurn[0].reason == "side 1 is not the active side");

        // -- queueBuild's structural refusals, in its words. Both are asked AS THE
        //    ACTIVE SIDE, so T-TURN-10 passes and the ladder below is what answers.
        std::vector<UiBuildOption> foreign;
        {
            EconomyState enemyHeld = econ;                   // side 1 now holds the factory
            for (Objective& o : enemyHeld.objectives)
                if (hexEqual(o.hex, fac)) o.owner = 1;
            UiWorld w6 = w; w6.economy = &enemyHeld;
            foreign = uiBuildOptions(w6, 0, fac);
            check("GATE-BUILDLIST (g) a factory the active side does not hold is "
                  "refused in queueBuild's words",
                  !foreign.empty() && !foreign[0].available && uniform(foreign) &&
                  foreign[0].reason == "factory is not held by this side");
        }

        std::vector<UiBuildOption> notFactory;
        {
            EconomyState townHeld = econ;                    // side 0 now holds the Town
            for (Objective& o : townHeld.objectives)
                if (hexEqual(o.hex, town)) o.owner = 0;
            UiWorld w7 = w; w7.economy = &townHeld;
            notFactory = uiBuildOptions(w7, 0, town);
            check("GATE-BUILDLIST (h) an objective the side DOES hold but that is not a "
                  "build point is refused in queueBuild's words",
                  !notFactory.empty() && !notFactory[0].available && uniform(notFactory) &&
                  notFactory[0].reason == "not a build point");
        }

        // -- ruling (c): availability never varies by row, in ANY state -----------
        check("GATE-BUILDLIST (i) across every state above, `available` is a property "
              "of the FACTORY and never of the row -- the cap cannot be leaking here",
              uniform(open) && uniform(built) && uniform(queued) && uniform(boxed) &&
              uniform(offTurn) && uniform(foreign) && uniform(notFactory));

        // -- the two fields are INDEPENDENT, both directions ----------------------
        bool someAffordableWhileClosed = false;
        for (const UiBuildOption& o : built)
            if (o.affordable && !o.available) someAffordableWhileClosed = true;
        check("GATE-BUILDLIST (j) unavailable does not imply unaffordable: a closed "
              "factory still reports what the side could pay for",
              someAffordableWhileClosed);

        {
            EconomyState broke = econ;
            initSide(broke, 0, 0);
            UiWorld w5 = w; w5.economy = &broke;
            const std::vector<UiBuildOption> poor = uiBuildOptions(w5, 0, fac);
            bool noneAfford = !poor.empty(), allAvail = !poor.empty();
            for (const UiBuildOption& o : poor) {
                noneAfford = noneAfford && !o.affordable;
                allAvail   = allAvail && o.available;
            }
            check("GATE-BUILDLIST (k) and unaffordable does not imply unavailable: at "
                  "zero Fame every row is still offered, priced and greyable",
                  noneAfford && allAvail && uniform(poor));
        }
    }

    // -----------------------------------------------------------------------
    // GATE-MATCHRESULT -- the result the snapshot's `match` block cannot carry.
    //
    // WHY THIS GATE EXISTS. `UiMatchView` mirrors `MatchResult::tier` and drops
    // `cause`, `winner` and `decidedByKey`. So every consumer downstream of the
    // projection can report *Decisive* and cannot report FOR WHOM -- while T-TURN-02
    // grades a flag kill "Decisive win for the KILLER" and T-TURN-04 decides a capped
    // match on a NAMED criterion. `uiMatchResult` carries all four.
    //
    // EVERY EXPECTATION HERE IS THE TURN MODULE'S OWN RETURN VALUE, never a literal.
    // `checkImmediate` and `endTurn` each return the `MatchResult` they recorded, so
    // this suite compares the query against that struct field for field. Comparing it
    // against a typed `1` would assert that someone typed the same number twice.
    // -----------------------------------------------------------------------
    std::printf("\n-- GATE-MATCHRESULT  who won, and by what (SEC 2.8) ------------------\n");
    {
        std::string terr;

        // -- (a) a flag kill, graded by the module -----------------------------
        //
        // THE FIXTURE IS BUILT SO THAT THE WINNER IS NOT THE SIDE TO MOVE. Side 0 is
        // active and side 0's flag is the one that falls, so the killer -- and the
        // winner -- is side 1. That inequality is the whole point of the gate: it is
        // the one arrangement under which a consumer that had quietly derived the
        // winner from `sideToMove` gives a DIFFERENT answer, and every check below
        // would pass against that wrong derivation without it.
        TurnState t;
        initMatch(t, 0, 20, terr);
        BoardSnapshot b;
        b.factoryTotal = 1;
        b.side[0].flagAlive = false;      // side 0's flag is down
        b.side[1].flagAlive = true;
        const MatchResult mr = checkImmediate(t, b);

        // `buildUiSnapshot` returns a DEFAULT snapshot when `economy` is null, so
        // check (b) below would compare against an empty struct and fail for a
        // reason having nothing to do with the result. `uiMatchResult` needs only
        // `turn`; the snapshot needs both, and this fixture feeds the snapshot.
        EconomyState econ;
        initSide(econ, 0, 0);
        initSide(econ, 1, 0);

        UiWorld w;
        w.board    = buildBoard(w.units);
        w.unitDefs = &units;
        w.terrain  = &terrain;
        w.economy  = &econ;
        w.turn     = &t;

        const UiMatchResult q  = uiMatchResult(w);
        const UiSnapshot    sn = buildUiSnapshot(w);

        check("GATE-MATCHRESULT (pre) the fixture discriminates: the module's winner "
              "is NOT the side to move, so deriving one from the other would differ",
              mr.winner != t.activeSide && mr.winner != SIDE_NONE);
        whyInt("module winner", mr.winner);
        whyInt("sideToMove", t.activeSide);

        check("GATE-MATCHRESULT (a) all four fields mirror the MatchResult the turn "
              "module itself returned",
              q.tier == mr.tier && q.cause == mr.cause &&
              q.winner == mr.winner && q.decidedByKey == mr.decidedByKey);
        why(tierName(q.tier));
        why(causeName(q.cause));

        check("GATE-MATCHRESULT (b) the snapshot agrees on the TIER and is silent on "
              "the winner -- the projection loss this query exists to close",
              sn.match.hasResult && sn.match.resultTier == q.tier &&
              sn.match.sideToMove != q.winner);

        // -- (c) a mutual-passivity draw at the cap ----------------------------
        //
        // Driven to the cap through `endTurn` rather than by calling `resolveAtCap`,
        // because it is `TurnState::result` this query reads and only the turn loop
        // writes it. cap = 1 makes the opening round the last one.
        TurnState d;
        initMatch(d, 0, 1, terr);
        BoardSnapshot db;
        db.factoryTotal = 1;
        db.side[0].fameCombat = 0;        // both silent: the passivity guard bites
        db.side[1].fameCombat = 0;
        // `beginTurn` leaves the phase at StartOfTurn; `applyStartOfTurnRepair` is
        // what advances it to Actions, and `endTurn` resolves only from there. The
        // first draft of this fixture called `beginTurn` alone, never reached the
        // cap, and check (c) below is what caught it -- see the note on that check.
        const std::vector<RepairSubject> noSubjects;
        beginTurn(d, db);
        applyStartOfTurnRepair(d, noSubjects);
        endTurn(d, db);
        beginTurn(d, db);
        applyStartOfTurnRepair(d, noSubjects);
        const MatchResult dr = endTurn(d, db);

        UiWorld dw;
        dw.board    = buildBoard(dw.units);
        dw.unitDefs = &units;
        dw.terrain  = &terrain;
        dw.turn     = &d;
        const UiMatchResult dq = uiMatchResult(dw);

        // THIS CHECK HAS ALREADY EARNED ITS PLACE. On the first run of this gate it
        // FAILED while (d) below PASSED -- the fixture never reached the cap, so both
        // sides of (d) were the same InProgress/SIDE_NONE default and (d) was
        // comparing a blank against a blank. A mirror check whose subject was never
        // switched on is a check measuring its fixture.
        check("GATE-MATCHRESULT (c) the draw fixture actually reached a result -- "
              "without this the mirror check below passes on an in-progress default",
              dr.tier != ResultTier::InProgress);

        check("GATE-MATCHRESULT (d) a draw reports SIDE_NONE and mirrors the module's "
              "own cause and key, with no side invented for it",
              dq.tier == dr.tier && dq.cause == dr.cause &&
              dq.winner == dr.winner && dq.winner == SIDE_NONE &&
              dq.decidedByKey == dr.decidedByKey);
        why(tierName(dq.tier));
        why(causeName(dq.cause));

        // -- (e) a missing input defers rather than inventing -------------------
        UiWorld nw;
        nw.board    = buildBoard(nw.units);
        nw.unitDefs = &units;
        nw.terrain  = &terrain;
        nw.turn     = nullptr;
        const UiMatchResult nq = uiMatchResult(nw);
        check("GATE-MATCHRESULT (e) a world with no turn state reports InProgress and "
              "SIDE_NONE rather than grading a match it cannot see",
              nq.tier == ResultTier::InProgress && nq.cause == ResultCause::None &&
              nq.winner == SIDE_NONE && nq.decidedByKey == 0);
    }

    // -----------------------------------------------------------------------
    std::printf("\n");
    std::printf("NOT RUN  T-UI-03 -- the live standings scoreboard binds 1:1 to snapshot\n");
    std::printf("         fields with no widget-side arithmetic. In-editor Unreal\n");
    std::printf("         Automation over widget bindings (§4.7 Stub 8, Acceptance;\n");
    std::printf("         marked † in §4.11). An in-editor pass now EXISTS; what\n");
    std::printf("         these two lack are the real Stratocracy widgets they\n");
    std::printf("         bind over. WRITTEN, UNBLOCKED and ASSERTING: what they\n");
    std::printf("         lack is a subject, not a rule.\n");
    std::printf("NOT RUN  T-UI-04 -- the production menu binds to the buildlist derived\n");
    std::printf("         from the four Stub-2 unit rows plus current fameTotal, and the\n");
    std::printf("         flag never appears. Same reason, same state. The buildlist\n");
    std::printf("         QUERY now EXISTS -- uiBuildOptions, ruled 2026-08-20, gated\n");
    std::printf("         above as GATE-BUILDLIST -- so what this ID still lacks is the\n");
    std::printf("         WIDGET that binds to it, not a shape. Row 8 does not flip.\n");
    std::printf("\n");
    std::printf("NOTE     The gap this suite filed at 7c36303 -- that no written invariant\n");
    std::printf("         asserted the snapshot mirrors the rules modules at all, while\n");
    std::printf("         T-UI-01..04 each assert a binding DOWNSTREAM of it -- was RULED\n");
    std::printf("         on 2026-08-04. It is T-UI-05, numbered, headless and unmarked,\n");
    std::printf("         and §4.5's written-ID count moved 70 -> 71. It runs above; the\n");
    std::printf("         four snapshot-shape checks are now upstream of a gated ID\n");
    std::printf("         rather than standing in for one.\n");
    std::printf("NOTE     GATE-CAP-PARTIAL ran on a fixture with captureTurns = 2. The\n");
    std::printf("         shipped scenario ships N = 1 (§2.7), so Ferrum Crossing cannot\n");
    std::printf("         reach the state this gate asserts about. N is per-scenario data\n");
    std::printf("         and the fixture configures it; no map was invented.\n");

    std::printf("\n%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
