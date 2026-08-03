// Test Engineer's gate for §4.11 row 6 — Opponent AI (T-AI-01..06 + the self-play
// smoke run §4.11 names alongside them).
//
// The AI is exercised THROUGH THE DRIVER, not through a referee written here. Every
// command it emits is rendered to the same command line a human types and applied by
// the same `execute`, so T-AI-01's "validated like any player command" is structural
// rather than asserted -- and no second rules composition exists in this file to
// disagree with the modules.
//
// The smoke run is named GATE-AI-SMOKE rather than T-AI-07: §4.11 row 6's acceptance
// is "T-AI-01..06 + self-play smoke", so the smoke is acceptance but carries no
// numbered ID, and minting one here would move §4.5's count.
#include "Ai.h"
#include "Driver.h"

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

static std::vector<std::string> run(Session& s, const std::string& cmd) {
    std::vector<std::string> out;
    execute(s, cmd, out);
    return out;
}

static bool anyContains(const std::vector<std::string>& out, const std::string& needle) {
    for (const std::string& l : out) if (l.find(needle) != std::string::npos) return true;
    return false;
}

static int defIndexOf(const std::vector<UnitDef>& u, const std::string& id) {
    for (std::size_t i = 0; i < u.size(); ++i) if (u[i].id == id) return static_cast<int>(i);
    return -1;
}

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? std::string(argv[1]) : std::string("../data");

    Session probe;
    std::string err;
    if (!sessionInit(probe, dir, err)) {
        std::printf("FAIL  T-AI-00 session-init (%s)\n\n0/1 passed\n", err.c_str());
        return 1;
    }
    const int INF   = defIndexOf(probe.unitDefs, "Infantry");
    const int TANK  = defIndexOf(probe.unitDefs, "Tank");
    const int ARTY  = defIndexOf(probe.unitDefs, "Artillery");
    const int RECON = defIndexOf(probe.unitDefs, "Recon");
    if (INF < 0 || TANK < 0 || ARTY < 0 || RECON < 0) {
        std::printf("FAIL  T-AI-00 table-lookup\n\n0/1 passed\n");
        return 1;
    }

    // One self-play game, driven entirely through the driver. Returns every line the
    // AI's own commands produced, so the caller can look for a refusal.
    struct GameResult {
        std::vector<std::string> aiLines;
        bool ended = false;
        ResultTier tier = ResultTier::InProgress;
        ResultCause cause = ResultCause::None;
        int turnsPlayed = 0;
    };
    auto playGame = [&](const char* fixture, int firstSide, int cap,
                        const std::vector<std::string>& setup) -> GameResult {
        GameResult g;
        Session t;
        std::string e;
        sessionInit(t, dir, e);
        if (!loadFixture(t, fixture, e)) return g;
        for (const std::string& c : setup) run(t, c);
        run(t, "ai buildlist Infantry Recon");
        run(t, "match " + std::to_string(firstSide) + " " + std::to_string(cap));
        for (int half = 0; half < cap * SIDE_COUNT + 4 && t.match.running; ++half) {
            const std::vector<std::string> a = run(t, "ai");
            for (const std::string& l : a) g.aiLines.push_back(l);
            run(t, "endturn");
        }
        g.ended = !t.match.running;
        g.tier = t.match.result.tier;
        g.cause = t.match.result.cause;
        g.turnsPlayed = t.match.turnNumber;
        return g;
    };

    // Six distinct openings, so "N games" is a set of different boards rather than
    // one board run six times.
    struct Opening { const char* fixture; int first; int cap; std::vector<std::string> setup; };
    const std::vector<Opening> openings = {
        {"contested", 0, 6, {"place 0 Infantry 0 1", "place 1 Infantry 6 1",
                             "place 0 Tank 1 2",     "place 1 Tank 5 2", "flag 1 4", "flag 0 3"}},
        {"contested", 1, 5, {"place 0 Infantry 0 0", "place 1 Infantry 6 2",
                             "place 0 Artillery 1 1","place 1 Recon 5 1", "flag 0 1", "flag 1 2"}},
        {"open",      0, 4, {"place 0 Infantry 0 1", "place 1 Tank 4 1", "flag 1 2"}},
        {"open",      1, 4, {"place 0 Recon 0 0",    "place 1 Artillery 4 2"}},
        {"river",     0, 5, {"place 0 Infantry 0 2", "place 1 Infantry 6 2",
                             "place 0 Tank 0 0",     "place 1 Tank 6 0"}},
        {"contested", 0, 3, {"place 0 Infantry 2 1", "place 1 Infantry 4 1"}},
    };

    std::vector<GameResult> games;
    for (const Opening& o : openings)
        games.push_back(playGame(o.fixture, o.first, o.cap, o.setup));

    // --- T-AI-01 ---------------------------------------------------------------
    // Legality: every AI command passes the same validation a player's does. Zero
    // rejected commands across the whole set, and no turn that ran away.
    bool ok01 = true;
    {
        int commands = 0;
        for (const GameResult& g : games) {
            for (const std::string& l : g.aiLines) {
                if (l.find("  ai> ") != std::string::npos) ++commands;
                if (l.find("refused") != std::string::npos) ok01 = false;
                if (l.find("STOPPED") != std::string::npos) ok01 = false;
            }
        }
        if (commands < 20) ok01 = false;      // a silent AI is not a legal one
        std::printf("      (T-AI-01: %d AI commands issued across %d games)\n",
                    commands, static_cast<int>(games.size()));
    }
    check("T-AI-01 legality-zero-rejected-commands", ok01);

    // --- T-AI-02 ---------------------------------------------------------------
    // Economy phase runs FIRST and spends: at a held, affordable factory the very
    // first command of the turn is a Build there.
    bool ok02 = true;
    {
        Session t;
        std::string e;
        sessionInit(t, dir, e);
        loadFixture(t, "contested", e);
        run(t, "place 0 Infantry 0 1");
        run(t, "place 1 Infantry 6 1");
        run(t, "ai buildlist Infantry");
        // Give side 0 a factory. Ownership is scenario data, so it is set through
        // the economy the same way a capture would set it.
        for (Objective& o : t.economy.objectives)
            if (t.terrainDefs[o.terrainIndex].isSpawnPoint) { o.owner = 0; break; }
        run(t, "match 0 20");

        const int fameBefore = t.economy.side[0].fameTotal;
        const AiCommand c = nextCommand(aiStateOf(t), 0);
        if (c.kind != AiCommandKind::Build) ok02 = false;
        if (c.defIndex != INF) ok02 = false;
        const Objective* target = findObjective(t.economy, c.hex);
        if (target == nullptr || target->owner != 0) ok02 = false;

        const std::vector<std::string> out = run(t, renderAiCommand(t, c));
        if (anyContains(out, "refused")) ok02 = false;
        if (t.economy.side[0].fameTotal != fameBefore - t.unitDefs[INF].costFame) ok02 = false;

        // It does not build twice at the same factory in one turn (§2.7).
        const AiCommand again = nextCommand(aiStateOf(t), 0);
        if (again.kind == AiCommandKind::Build && hexEqual(again.hex, c.hex)) ok02 = false;

        // Unaffordable: nothing is built, and the AI does not stall on it.
        Session poor = t;
        initSide(poor.economy, 0, 0);
        const AiCommand none = nextCommand(aiStateOf(poor), 0);
        if (none.kind == AiCommandKind::Build) ok02 = false;
        if (chooseBuild(aiStateOf(poor), 0) != -1) ok02 = false;
    }
    check("T-AI-02 economy-phase-first-and-spends", ok02);

    // --- T-AI-03 ---------------------------------------------------------------
    // Capture behaviour: an idle Infantry that can reach an uncaptured, undefended
    // objective moves onto it; and once standing on one it holds rather than wandering.
    bool ok03 = true;
    {
        Session t;
        std::string e;
        sessionInit(t, dir, e);
        loadFixture(t, "contested", e);
        run(t, "place 0 Infantry 2 1");        // #1, near the centre factory (3,1)
        run(t, "place 1 Infantry 6 2");        // far away, so nothing is defended
        run(t, "match 0 20");

        // (3,1) is the centre factory, one hex away. (0,0) is also an uncaptured
        // objective and is canonically FIRST, so this fixture distinguishes "nearest"
        // from "canonically first" -- the AI must take the near one.
        const Hex centre = offsetToAxial(3, 1);
        const Hex farCorner = offsetToAxial(0, 0);
        if (findObjective(t.economy, centre) == nullptr) ok03 = false;
        if (findObjective(t.economy, farCorner) == nullptr) ok03 = false;
        if (!hexLess(farCorner, centre)) ok03 = false;          // the fixture must pose it
        const AiCommand c = nextCommand(aiStateOf(t), 0);
        if (c.kind != AiCommandKind::Move) ok03 = false;
        if (!hexEqual(c.hex, centre)) ok03 = false;
        if (c.unitId != 1) ok03 = false;

        run(t, renderAiCommand(t, c));
        // Standing on it now: it holds, so the turn-boundary tick can complete the
        // capture. The next command must not move this unit off the objective.
        const AiCommand hold = nextCommand(aiStateOf(t), 0);
        if (hold.kind == AiCommandKind::Move && hold.unitId == 1) ok03 = false;

        // A defended objective is not walked onto: an enemy beside it is enough.
        Session d;
        sessionInit(d, dir, e);
        loadFixture(d, "contested", e);
        run(d, "place 0 Infantry 2 1");
        run(d, "place 1 Tank 4 1");            // adjacent to (3,1)
        run(d, "match 0 20");
        const AiCommand guarded = nextCommand(aiStateOf(d), 0);
        if (guarded.kind == AiCommandKind::Move && hexEqual(guarded.hex, centre)) ok03 = false;
    }
    check("T-AI-03 capture-behaviour-moves-onto-undefended-objective", ok03);

    // --- T-AI-04 ---------------------------------------------------------------
    // Attack preference: the flag outright; else the best expected damage, compared
    // against a direct Combat.h computation; Artillery keeps its standoff.
    bool ok04 = true;
    {
        Session t;
        std::string e;
        sessionInit(t, dir, e);
        loadFixture(t, "open", e);
        run(t, "place 0 Tank 2 1");            // #1
        run(t, "place 1 Infantry 1 1");        // #2 — the softer target
        run(t, "place 1 Recon 3 1");           // #3 — designated the flag
        run(t, "flag 1 3");
        run(t, "match 0 20");

        const AiState a = aiStateOf(t);
        const AiUnit* atk = findAiUnit(a, 1);
        const AiUnit* soft = findAiUnit(a, 2);
        const AiUnit* flag = findAiUnit(a, 3);
        if (atk == nullptr || soft == nullptr || flag == nullptr) ok04 = false;
        else {
            // The flag is chosen even though the OTHER target takes more damage --
            // the fixture must pose that choice, or the assertion proves nothing.
            if (expectedDamage(a, *atk, *soft) > expectedDamage(a, *atk, *flag)) {
                const AiCommand c = nextCommand(a, 0);
                if (c.kind != AiCommandKind::Attack || c.targetId != 3) ok04 = false;
            } else ok04 = false;
        }

        // With no flag on the board the best expected-damage target wins, and the
        // AI's notion of "best" equals a direct resolveDamage comparison.
        Session n;
        sessionInit(n, dir, e);
        loadFixture(n, "open", e);
        run(n, "place 0 Tank 2 1");
        run(n, "place 1 Infantry 1 1");
        run(n, "place 1 Recon 3 1");
        run(n, "match 0 20");
        const AiState an = aiStateOf(n);
        const AiCommand cn = nextCommand(an, 0);
        if (cn.kind != AiCommandKind::Attack) ok04 = false;
        else {
            const AiUnit* chosen = findAiUnit(an, cn.targetId);
            const AiUnit* me = findAiUnit(an, 1);
            int best = 0;
            for (const AiUnit& u : an.units) {
                if (u.side == me->side) continue;
                const int d = expectedDamage(an, *me, u);
                if (d > best) best = d;
            }
            if (chosen == nullptr || expectedDamage(an, *me, *chosen) != best) ok04 = false;
        }

        // Artillery advancing on a distant enemy stops at its standoff rather than
        // closing to contact, so it never walks into a counter (§2.9).
        Session s2;
        sessionInit(s2, dir, e);
        loadFixture(s2, "river", e);
        run(s2, "place 0 Artillery 0 2");
        run(s2, "place 1 Tank 6 2");
        run(s2, "match 0 20");
        const AiState aa = aiStateOf(s2);
        const AiCommand ca = nextCommand(aa, 0);
        if (ca.kind != AiCommandKind::Move) ok04 = false;
        else {
            const AiUnit* enemy = findAiUnit(aa, 2);
            const int after = hexDistance(ca.hex, enemy->hex);
            if (after < s2.unitDefs[ARTY].rangeMax) ok04 = false;   // never inside the band
        }
    }
    check("T-AI-04 attack-preference-and-standoff", ok04);

    // --- T-AI-05 ---------------------------------------------------------------
    // The strictly-losing guard, with BOTH halves binding. Asserted structurally over
    // the whole shipped table rather than on one hand-picked fixture: there must exist
    // at least one exchange in which the counter kills the attacker and the attack is
    // still permitted, or the guard has collapsed into "never die".
    bool ok05 = true;
    {
        Session t;
        std::string e;
        sessionInit(t, dir, e);
        loadFixture(t, "open", e);

        int deaths = 0, permittedDeaths = 0, skipped = 0;
        for (int ai_ = 0; ai_ < 4; ++ai_)
        for (int di = 0; di < 4; ++di)
        for (int ahp = 1; ahp <= t.unitDefs[ai_].hpMax; ++ahp)
        for (int dhp = 1; dhp <= t.unitDefs[di].hpMax; ++dhp)
        for (int flag = 0; flag < 2; ++flag) {
            AiState w = aiStateOf(t);
            AiUnit A; A.id = 1; A.side = 0; A.defIndex = ai_; A.hex = offsetToAxial(1, 1); A.hp = ahp;
            AiUnit D; D.id = 2; D.side = 1; D.defIndex = di;  D.hex = offsetToAxial(2, 1); D.hp = dhp;
            D.isFlag = (flag != 0);
            w.units.clear(); w.units.push_back(A); w.units.push_back(D);

            const int dmg = expectedDamage(w, A, D);
            if (dmg <= 0 || dhp - dmg <= 0) continue;
            Unit wounded;
            const UnitDef& dd = w.unitDefs[di];
            wounded.atk = dd.atk; wounded.def = dd.def; wounded.hp = dhp - dmg; wounded.hpMax = dd.hpMax;
            wounded.rangeMin = dd.rangeMin; wounded.rangeMax = dd.rangeMax; wounded.type = dd.type;
            if (!defenderCanCounter(wounded, 1)) continue;
            Unit au;
            const UnitDef& ad = w.unitDefs[ai_];
            au.atk = ad.atk; au.def = ad.def; au.hp = ahp; au.hpMax = ad.hpMax;
            au.rangeMin = ad.rangeMin; au.rangeMax = ad.rangeMax; au.type = ad.type;
            if (ahp - resolveDamage(wounded, au, 0) > 0) continue;    // attacker survives

            ++deaths;                                                  // the counter kills it
            if (isStrictlyLosing(w, A, D)) ++skipped;
            else {
                ++permittedDeaths;
                // Permitted only because the exchange does not trade down.
                if (exchangeValueLost(w, A) > exchangeValueDealt(w, A, D)) ok05 = false;
            }
        }
        std::printf("      (T-AI-05: %d lethal exchanges, %d skipped, %d permitted)\n",
                    deaths, skipped, permittedDeaths);
        if (deaths == 0) ok05 = false;              // a vacuous sweep is not a pass
        if (skipped == 0) ok05 = false;             // the guard must actually skip something
        if (permittedDeaths == 0) ok05 = false;     // ...and must NOT be "never die"

        // And the clear down-trade is refused: a nearly-dead Infantry into a Tank.
        AiState w = aiStateOf(t);
        AiUnit A; A.id = 1; A.side = 0; A.defIndex = INF;  A.hex = offsetToAxial(1, 1); A.hp = 1;
        AiUnit D; D.id = 2; D.side = 1; D.defIndex = TANK; D.hex = offsetToAxial(2, 1); D.hp = 20;
        w.units.clear(); w.units.push_back(A); w.units.push_back(D);
        if (!isStrictlyLosing(w, A, D)) ok05 = false;
        if (exchangeValueLost(w, A) <= exchangeValueDealt(w, A, D)) ok05 = false;
    }
    check("T-AI-05 strictly-losing-guard-both-halves-bind", ok05);

    // --- T-AI-06 ---------------------------------------------------------------
    // Determinism, and Q9's stated tie-breaks: build priority is ascending §2.4 COST,
    // which is NOT the order §2.4's table prints.
    bool ok06 = true;
    {
        // Build priority: with only Tank and Recon affordable, Recon is built --
        // Recon is dearer than Infantry but cheaper than Tank, and the printed table
        // order would pick Tank.
        Session t;
        std::string e;
        sessionInit(t, dir, e);
        loadFixture(t, "contested", e);
        run(t, "ai buildlist Tank Recon");
        for (Objective& o : t.economy.objectives)
            if (t.terrainDefs[o.terrainIndex].isSpawnPoint) { o.owner = 0; break; }
        initSide(t.economy, 0, 400);                 // both affordable
        run(t, "match 0 20");
        if (chooseBuild(aiStateOf(t), 0) != RECON) ok06 = false;

        // The priority is a function of cost, checked against the loaded table.
        if (!buildPriorityLess(t.unitDefs[INF],   t.unitDefs[RECON])) ok06 = false;
        if (!buildPriorityLess(t.unitDefs[RECON], t.unitDefs[ARTY]))  ok06 = false;
        if (!buildPriorityLess(t.unitDefs[ARTY],  t.unitDefs[TANK]))  ok06 = false;
        if (buildPriorityLess(t.unitDefs[TANK],   t.unitDefs[RECON])) ok06 = false;
        // ...and it is NOT the printed order, or this invariant would be vacuous.
        if (static_cast<int>(t.unitDefs[TANK].type) >= static_cast<int>(t.unitDefs[RECON].type))
            ok06 = false;

        // Same state -> same command, twice, on a board with several live choices.
        Session d1, d2;
        sessionInit(d1, dir, e); loadFixture(d1, "contested", e);
        sessionInit(d2, dir, e); loadFixture(d2, "contested", e);
        const char* setup[] = {"place 0 Infantry 0 1", "place 0 Tank 1 2",
                               "place 1 Infantry 6 1", "place 1 Recon 5 2",
                               "ai buildlist Infantry Recon", "match 0 20"};
        for (const char* c : setup) { run(d1, c); run(d2, c); }
        for (int i = 0; i < 12; ++i) {
            const AiCommand a = nextCommand(aiStateOf(d1), d1.match.activeSide);
            const AiCommand b = nextCommand(aiStateOf(d2), d2.match.activeSide);
            if (a.kind != b.kind || a.unitId != b.unitId || a.targetId != b.targetId ||
                a.defIndex != b.defIndex || !hexEqual(a.hex, b.hex)) { ok06 = false; break; }
            if (a.kind == AiCommandKind::EndTurn) { run(d1, "endturn"); run(d2, "endturn"); }
            else { run(d1, renderAiCommand(d1, a)); run(d2, renderAiCommand(d2, b)); }
        }

        // Two whole games from the same opening are identical line for line.
        const GameResult g1 = playGame("contested", 0, 4,
            {"place 0 Infantry 0 1", "place 1 Infantry 6 1", "place 0 Tank 1 2"});
        const GameResult g2 = playGame("contested", 0, 4,
            {"place 0 Infantry 0 1", "place 1 Infantry 6 1", "place 0 Tank 1 2"});
        if (g1.aiLines.size() != g2.aiLines.size() || g1.aiLines.empty()) ok06 = false;
        else for (std::size_t i = 0; i < g1.aiLines.size(); ++i)
            if (g1.aiLines[i] != g2.aiLines[i]) ok06 = false;
        if (g1.tier != g2.tier || g1.cause != g2.cause) ok06 = false;
    }
    check("T-AI-06 determinism-and-Q9-tie-breaks", ok06);

    // --- GATE-AI-SMOKE ---------------------------------------------------------
    // §4.11 row 6's self-play smoke: N headless AI-vs-AI games all terminate at or
    // before the cap with a valid result tier. Not a numbered acceptance ID.
    bool okSmoke = true;
    {
        for (std::size_t i = 0; i < games.size(); ++i) {
            const GameResult& g = games[i];
            if (!g.ended) { okSmoke = false; continue; }
            if (g.tier != ResultTier::Decisive && g.tier != ResultTier::Marginal &&
                g.tier != ResultTier::Draw) okSmoke = false;
            if (g.cause == ResultCause::None) okSmoke = false;
            if (g.turnsPlayed > openings[i].cap) okSmoke = false;
            std::printf("      (game %d: %s/%s after %d turns, cap %d)\n",
                        static_cast<int>(i + 1), tierName(g.tier), causeName(g.cause),
                        g.turnsPlayed, openings[i].cap);
        }
        if (games.empty()) okSmoke = false;
    }
    check("GATE-AI-SMOKE self-play-games-terminate-with-a-tier", okSmoke);

    std::printf("\n%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
