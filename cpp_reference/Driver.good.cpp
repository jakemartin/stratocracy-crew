// Stratocracy — debug-command driver implementation (§4.4 week 1).
//
// CONTAINS NO RULES. Every rule decision below is a call into Hex.h, Data.h,
// Move.h, Combat.h, Economy.h, Turn.h, Ai.h, Scenario.h or Ui.h. What a scenario file
// looks like is one of them: `scenario load` hands the path to Scenario.h and
// installs whatever it returns, refusing whatever it refuses. HOW A WIDGET IS FED is
// now another: row 8 landed, so `snapshot` prints §4.7 Stub 8's view model rather
// than refusing, and the projection is Ui.h's (spec/driver_spec.md).
#include "Driver.h"

#include <algorithm>
#include <sstream>

namespace strat {

namespace {

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string tok;
    while (in >> tok) out.push_back(tok);
    return out;
}

bool parseInt(const std::string& s, int& out) {
    if (s.empty()) return false;
    std::size_t i = (s[0] == '-') ? 1 : 0;
    if (i >= s.size()) return false;
    for (std::size_t k = i; k < s.size(); ++k)
        if (s[k] < '0' || s[k] > '9') return false;
    out = std::atoi(s.c_str());
    return true;
}

std::string num(int v) { return std::to_string(v); }

// One character per terrain, for `map`. Driven off the loaded row Id, so a terrain
// the table does not define renders as '?' rather than as a guess.
char glyphFor(const std::string& id) {
    if (id == "Plains")    return '.';
    if (id == "Woods")     return 'F';
    if (id == "Mountains") return 'M';
    if (id == "Water")     return '~';
    if (id == "Town")      return 'T';
    if (id == "Bridge")    return 'B';
    if (id == "Factory")   return 'X';
    return '?';
}

} // namespace

const DriverUnit* findUnitById(const Session& s, int id) {
    for (const DriverUnit& u : s.units) if (u.id == id) return &u;
    return nullptr;
}

static DriverUnit* mutableUnitById(Session& s, int id) {
    for (DriverUnit& u : s.units) if (u.id == id) return &u;
    return nullptr;
}

// ---------------------------------------------------------------------------
// delegation helpers -- the only places the driver touches module state
// ---------------------------------------------------------------------------
static Board buildBoard(const Session& s) {
    Board b;
    b.bounds  = s.bounds;
    b.terrain = s.terrain;
    b.occupant.assign(s.terrain.size(), OCCUPANT_NONE);
    for (const DriverUnit& u : s.units) {
        const int i = b.index(u.hex);
        if (i >= 0) b.occupant[i] = u.id;
    }
    return b;
}

// A Combat.h participant built from the loaded UnitDef plus current HP. The driver
// stores no stat of its own; every field is looked up.
static Unit combatUnit(const Session& s, const DriverUnit& u) {
    const UnitDef& d = s.unitDefs[u.defIndex];
    Unit c;
    c.atk = d.atk; c.def = d.def; c.hp = u.hp; c.hpMax = d.hpMax;
    c.rangeMin = d.rangeMin; c.rangeMax = d.rangeMax; c.type = d.type;
    return c;
}

// Row 8's world, GATHERED and not decided: the board from the session's terrain, the
// units from its roster, ownership and Fame from Economy.h, the turn and both per-unit
// flags from Turn.h, the flag designation from the same field `snapshotOf` reads.
UiWorld uiWorldOf(const Session& s) {
    UiWorld w;
    w.board    = buildBoard(s);
    w.unitDefs = &s.unitDefs;
    w.terrain  = &s.terrainDefs;
    w.economy  = &s.economy;
    w.turn     = &s.match;
    // The guided seats come from the loaded file and from nowhere else. A built-in
    // fixture has no guided opening (`fixture` says so), so this stays null and every
    // unit is unmarked -- absence, not a default.
    w.guided   = s.scenarioLoaded ? &s.scenario.guided : nullptr;
    for (const DriverUnit& u : s.units) {
        UiUnit v;
        v.id       = u.id;
        v.side     = u.side;
        v.defIndex = u.defIndex;
        v.hex      = u.hex;
        v.placement = u.placement;
        v.unit     = combatUnit(s, u);
        v.isFlag   = (u.side >= 0 && u.side < SIDE_COUNT && s.flagUnit[u.side] == u.id);
        w.units.push_back(v);
    }
    return w;
}

// Renders §4.7 Stub 8's view model. Every value printed is READ off the projection --
// the driver computes none of them, which is what GATE-DRV-12 asserts by rebuilding
// the same snapshot through Ui.h and comparing. Hexes are omitted from the render
// because `map` already prints the board; the per-hex half of the snapshot is
// exercised by the gate rather than by the eye.
//
// It renders the SNAPSHOT, which is not the whole view model: the presentation block's
// two members (§2.11.1's DONE bit, §2.11.6's `lockedThisTurn`) are owned by the
// selection machine and the guidance layer, and this driver runs neither. They are
// absent here because nothing in a headless session produces them, not because the
// view model omits them.
static void printUiSnapshot(const Session& s, std::vector<std::string>& out) {
    const UiWorld    w = uiWorldOf(s);
    const UiSnapshot v = buildUiSnapshot(w);
    out.push_back("view model (§4.7 Stub 8) -- " + num(static_cast<int>(v.hexes.size())) +
                  " hexes, " + num(static_cast<int>(v.units.size())) + " units");
    out.push_back("match: turn " + num(v.match.turn) + "/" + num(v.match.turnCap) +
                  " sideToMove " + num(v.match.sideToMove) + " result " +
                  (v.match.hasResult ? tierName(v.match.resultTier) : std::string("null")));
    for (int i = 0; i < SIDE_COUNT; ++i) {
        out.push_back("side " + num(i) + ": fameTotal " + num(v.side[i].fameTotal) +
                      " fameCombat " + num(v.side[i].fameCombat) +
                      " objectivesHeld " + num(v.side[i].objectivesHeld) + " of " +
                      num(v.objectiveTotal) +
                      " survivingHP " + num(v.side[i].survivingHp) +
                      " incomePerTurn " + num(v.side[i].incomePerTurn));
    }
    for (const UiUnitView& u : v.units) {
        int col = 0, row = 0;
        axialToOffset(u.hex, col, row);
        // The two flags print SEPARATELY. One combined "done" column could not show a
        // unit that has spent exactly one of them, which is the whole content of
        // T-TURN-01's split -- and neither is §2.11.1's DONE bit, which the snapshot
        // deliberately does not carry.
        out.push_back("unit " + num(u.id) + " side " + num(u.side) + " " +
                      s.unitDefs[u.unitId].id + " (" + num(col) + "," + num(row) + ") hp " +
                      num(u.hp) + "/" + num(u.hpMax) +
                      (u.isFlag ? " FLAG" : "") +
                      " hasMoved " + num(u.hasMoved ? 1 : 0) +
                      " hasActed " + num(u.hasActed ? 1 : 0) +
                      " captureProgress " + num(u.captureProgress) +
                      (u.isGuidedMarked ? " GUIDED-MARKED" : ""));
    }
    // The per-factory group. `spawnBlocked` and `buildWaiting` print SEPARATELY and
    // are never folded into one column: the state §2.11.5 must display is a boxed-in
    // factory with NOTHING queued, which is blocked and not waiting, and a single
    // column could not tell it from a factory that simply has a build in flight.
    for (const UiFactoryView& f : v.factories) {
        int col = 0, row = 0;
        axialToOffset(f.hex, col, row);
        out.push_back("factory (" + num(col) + "," + num(row) + ") owner " +
                      (f.owner == OWNER_NEUTRAL ? std::string("neutral") : num(f.owner)) +
                      " hasBuiltThisTurn " + num(f.hasBuiltThisTurn ? 1 : 0) +
                      " buildWaiting " + num(f.buildWaiting ? 1 : 0) +
                      " spawnBlocked " + num(f.spawnBlocked ? 1 : 0));
    }
}

static int terrainDefPctAt(const Session& s, const Hex& h) {
    Board b; b.bounds = s.bounds; b.terrain = s.terrain;
    b.occupant.assign(s.terrain.size(), OCCUPANT_NONE);
    const int t = b.terrainAt(h);
    if (t < 0 || static_cast<std::size_t>(t) >= s.terrainDefs.size()) return 0;
    return s.terrainDefs[t].defensePct;
}

static bool enemyAdjacent(const Session& s, const DriverUnit& u) {
    Hex adj[HEX_DIRECTIONS];
    const int n = neighbors(u.hex, s.bounds, adj);
    for (int i = 0; i < n; ++i)
        for (const DriverUnit& o : s.units)
            if (o.side != u.side && hexEqual(o.hex, adj[i])) return true;
    return false;
}

// ---------------------------------------------------------------------------
// THE attack computation. `forecast` and `attack` both call this and nothing else,
// so "the forecast is exactly what resolves" (§2.6) is structural here, not merely
// asserted by GATE-DRV-03.
// ---------------------------------------------------------------------------
struct AttackOutcome {
    bool legal = false;
    std::string reason;
    int  distance = 0;
    int  damage = 0;
    bool defenderDies = false;
    bool counterFires = false;
    int  counterDamage = 0;
    bool attackerDies = false;
};

static AttackOutcome computeAttack(const Session& s, int atkId, int defId) {
    AttackOutcome o;
    const DriverUnit* a = findUnitById(s, atkId);
    const DriverUnit* d = findUnitById(s, defId);
    if (a == nullptr) { o.reason = "no unit " + num(atkId); return o; }
    if (d == nullptr) { o.reason = "no unit " + num(defId); return o; }
    if (a->id == d->id) { o.reason = "a unit cannot attack itself"; return o; }
    if (a->side == d->side) { o.reason = "units " + num(atkId) + " and " + num(defId) +
                                         " are on the same side"; return o; }

    o.distance = hexDistance(a->hex, d->hex);                 // Hex.h decides distance
    const UnitDef& ad = s.unitDefs[a->defIndex];
    if (o.distance < ad.rangeMin || o.distance > ad.rangeMax) {
        o.reason = "distance " + num(o.distance) + " is outside " +
                   s.unitDefs[a->defIndex].id + "'s range " +
                   num(ad.rangeMin) + "-" + num(ad.rangeMax);
        return o;
    }

    const Unit au = combatUnit(s, *a);
    const Unit du = combatUnit(s, *d);
    o.damage = resolveDamage(au, du, terrainDefPctAt(s, d->hex));  // Combat.h decides damage
    const int defHpAfter = d->hp - o.damage;
    o.defenderDies = defHpAfter <= 0;

    if (!o.defenderDies) {
        Unit duAfter = du;
        duAfter.hp = defHpAfter;                              // a wounded defender counters weaker
        if (defenderCanCounter(duAfter, o.distance)) {        // Combat.h decides eligibility
            o.counterFires  = true;
            o.counterDamage = resolveDamage(duAfter, au, terrainDefPctAt(s, a->hex));
            o.attackerDies  = (a->hp - o.counterDamage) <= 0;
        }
    }
    o.legal = true;
    return o;
}

// ---------------------------------------------------------------------------
// fixtures -- built in. The FILE format is Scenario.h's (row 7); these are the
// hand-built boards that predate it and they carry no ownership or starting force.
// ---------------------------------------------------------------------------
namespace {
struct Fixture { const char* name; int cols; int rows; const char* const* glyphs; };

const char* const kRiver[5] = { "...~...", ".F.~...", "...B...", ".M.~...", "...~..." };
const char* const kOpen[3]  = { ".....", ".....", "....." };
// Objectives to exercise row 4: a factory per side, a neutral factory between them,
// and two towns. All start NEUTRAL — initial ownership is scenario data (Stub 7,
// unbuilt), so the driver leaves it unset rather than inventing a starting layout.
const char* const kContested[3] = { "X.....X", "...X...", "..T.T.." };

const Fixture kFixtures[3] = {
    {"river",     7, 5, kRiver},      // a Water column crossed by one Bridge
    {"open",      5, 3, kOpen},       // flat plains, for range and counter cases
    {"contested", 7, 3, kContested},  // 3 factories + 2 towns, for capture and Fame
};
} // namespace

std::vector<std::string> fixtureNames() {
    std::vector<std::string> v;
    for (const Fixture& f : kFixtures) v.push_back(f.name);
    return v;
}

static int terrainIndexFor(const Session& s, char glyph) {
    for (std::size_t i = 0; i < s.terrainDefs.size(); ++i)
        if (glyphFor(s.terrainDefs[i].id) == glyph) return static_cast<int>(i);
    return -1;
}

bool loadFixture(Session& s, const std::string& name, std::string& err) {
    for (const Fixture& f : kFixtures) {
        if (name != f.name) continue;
        Session next = s;                       // build aside; commit only on success
        next.bounds.cols = f.cols;
        next.bounds.rows = f.rows;
        next.terrain.assign(static_cast<std::size_t>(f.cols) * f.rows, -1);
        for (int row = 0; row < f.rows; ++row) {
            for (int col = 0; col < f.cols; ++col) {
                const int t = terrainIndexFor(s, f.glyphs[row][col]);
                if (t < 0) { err = "fixture uses a terrain the table does not define"; return false; }
                next.terrain[static_cast<std::size_t>(row) * f.cols + col] = t;
            }
        }
        next.units.clear();
        next.nextUnitId = 1;
        next.loaded = true;

        // Row 4 state. Objectives are every capturable tile the TABLE marks -- the
        // driver does not decide what is capturable, Data.h does. Ownership starts
        // neutral because initial ownership is scenario data (Stub 7, unbuilt), and
        // both sides open on the §2.7 Normal-tier value; `initSide` takes it as an
        // argument so no tier is baked in here (Q8).
        next.economy = EconomyState();
        next.economy.captureTurns = 1;              // N = 1, the shipped scenario's value
        next.scenarioLoaded = false;                // a fixture is not a scenario
        next.scenario       = Scenario();
        next.scenarioReport = ScenarioLoadResult();
        for (int row = 0; row < f.rows; ++row) {
            for (int col = 0; col < f.cols; ++col) {
                const int ti = next.terrain[static_cast<std::size_t>(row) * f.cols + col];
                if (!s.terrainDefs[ti].capturable) continue;
                Objective o;
                o.hex = offsetToAxial(col, row);
                o.owner = OWNER_NEUTRAL;
                o.terrainIndex = ti;
                next.economy.objectives.push_back(o);
            }
        }
        initSide(next.economy, 0, 200);
        initSide(next.economy, 1, 200);
        next.turnNumber = 1;

        // Row 5. A fresh board is a fresh sandbox: no match is running until `match`
        // starts one, and no unit is designated a flag until `flag` names one.
        next.match = TurnState();
        for (int i = 0; i < SIDE_COUNT; ++i) next.flagUnit[i] = -1;

        s = next;
        return true;
    }
    err = "no fixture named '" + name + "'";
    return false;
}

bool installScenario(Session& s, const Scenario& sc, std::string& err) {
    Session next = s;                       // build aside; commit only on success
    next.bounds = sc.bounds;
    next.terrain.assign(sc.terrainId.size(), -1);
    for (std::size_t i = 0; i < sc.terrainId.size(); ++i) {
        int idx = -1;
        for (std::size_t k = 0; k < s.terrainDefs.size(); ++k)
            if (s.terrainDefs[k].id == sc.terrainId[i]) idx = static_cast<int>(k);
        if (idx < 0) { err = "terrain Id '" + sc.terrainId[i] + "' is in no loaded row"; return false; }
        next.terrain[i] = idx;
    }
    next.units.clear();
    next.nextUnitId = 1;
    next.loaded     = true;
    next.economy    = EconomyState();
    // N is not a Stub 7 field. §2.7 fixes it at 1 (Q4) and the driver reads that the
    // same way loadFixture already does, rather than inventing a scenario field.
    next.economy.captureTurns = 1;
    for (int i = 0; i < SIDE_COUNT; ++i) next.flagUnit[i] = -1;

    for (const ScenarioPlacement& p : sc.placements) {
        int defIndex = -1;
        for (std::size_t k = 0; k < s.unitDefs.size(); ++k)
            if (s.unitDefs[k].id == p.unitId) defIndex = static_cast<int>(k);
        if (defIndex < 0) { err = "unit Id '" + p.unitId + "' is in no loaded row"; return false; }
        DriverUnit u;
        u.id       = next.nextUnitId++;
        u.side     = p.side;
        u.defIndex = defIndex;
        u.hex      = p.hex;
        u.placement = p.hex;        // the file's deployment hex; the guided seat reads this
        u.hp       = s.unitDefs[defIndex].hpMax;
        next.units.push_back(u);
        // The designation comes from the FILE now. T-SCN-01 has already checked it is
        // exactly one Tank per side, so the driver copies rather than chooses.
        if (p.isFlag) next.flagUnit[p.side] = u.id;
    }

    // Objectives are every capturable tile the TABLE marks -- Data.h decides what is
    // capturable -- owned as the FILE says. A capturable hex the file does not name
    // is unowned (§2.7), which is what Scenario.h validated T-SCN-03 against.
    for (int row = 0; row < sc.bounds.rows; ++row) {
        for (int col = 0; col < sc.bounds.cols; ++col) {
            const std::size_t i = static_cast<std::size_t>(row) * sc.bounds.cols + col;
            const int ti = next.terrain[i];
            if (ti < 0 || !s.terrainDefs[ti].capturable) continue;
            Objective o;
            o.hex          = offsetToAxial(col, row);
            o.owner        = OWNER_NEUTRAL;
            o.terrainIndex = ti;
            for (const ScenarioOwner& w : sc.ownership)
                if (hexEqual(w.hex, o.hex)) o.owner = w.owner;
            next.economy.objectives.push_back(o);
        }
    }
    // §2.9's difficulty handicap is a match-setup parameter applied on top, not a
    // scenario field, so the file's value is what `initSide` gets and nothing here
    // adjusts it (Q8).
    initSide(next.economy, 0, sc.startingFame[0]);
    initSide(next.economy, 1, sc.startingFame[1]);
    next.turnNumber = 1;
    next.match      = TurnState();
    next.scenarioLoaded = true;
    next.scenario       = sc;
    s = next;
    return true;
}

bool sessionInit(Session& s, const std::string& dataDir, std::string& err) {
    double eff[UNIT_TYPE_COUNT][UNIT_TYPE_COUNT];
    if (!loadUnits(dataDir + "/units.csv", s.unitDefs, err)) return false;
    if (!loadTerrain(dataDir + "/terrain.csv", s.terrainDefs, err)) return false;
    if (!loadEffectiveness(dataDir + "/effectiveness.csv", eff, err)) return false;
    return true;
}

AiState aiStateOf(const Session& s) {
    AiState a;
    a.bounds      = s.bounds;
    a.terrain     = s.terrain;
    a.unitDefs    = s.unitDefs;
    a.terrainDefs = s.terrainDefs;
    a.economy     = s.economy;
    a.turn        = s.match;
    a.buildlist   = s.buildlist;
    a.builtThisTurn = s.match.builtThisTurn;   // row 5 owns it now, not the driver
    for (const DriverUnit& u : s.units) {
        AiUnit au;
        au.id = u.id; au.side = u.side; au.defIndex = u.defIndex;
        au.hex = u.hex; au.hp = u.hp;
        au.isFlag = (u.side >= 0 && u.side < SIDE_COUNT && s.flagUnit[u.side] == u.id);
        a.units.push_back(au);
    }
    return a;
}

std::string renderAiCommand(const Session& s, const AiCommand& c) {
    int col = 0, row = 0;
    switch (c.kind) {
        case AiCommandKind::Build:
            axialToOffset(c.hex, col, row);
            return "build " + num(s.match.activeSide) + " " + s.unitDefs[c.defIndex].id +
                   " " + num(col) + " " + num(row);
        case AiCommandKind::Move:
            axialToOffset(c.hex, col, row);
            return "move " + num(c.unitId) + " " + num(col) + " " + num(row);
        case AiCommandKind::Attack:
            return "attack " + num(c.unitId) + " " + num(c.targetId);
        case AiCommandKind::EndTurn:
            return "endturn";
    }
    return "endturn";
}

int currentTurn(const Session& s) {
    // Turn.h owns the number the moment a match exists; before that the debug setter
    // is all there is. One expression, so no second turn counter can drift.
    return s.match.running ? s.match.turnNumber : s.turnNumber;
}

BoardSnapshot snapshotOf(const Session& s) {
    BoardSnapshot b;
    for (int i = 0; i < SIDE_COUNT; ++i) {
        b.side[i].fameCombat = s.economy.side[i].fameCombat;   // Economy.h's counter
        b.side[i].objectivesHeld = 0;
        b.side[i].survivingHp = 0;
        b.side[i].factoriesHeld = 0;
        // No designation -> the flag is not on the board to lose, so the match
        // cannot end that way. The driver never invents one.
        b.side[i].flagAlive =
            (s.flagUnit[i] < 0) || (findUnitById(s, s.flagUnit[i]) != nullptr);
    }
    for (const Objective& o : s.economy.objectives) {
        if (o.owner < 0 || o.owner >= SIDE_COUNT) continue;
        b.side[o.owner].objectivesHeld += 1;                   // factories AND towns
        // Which tile is a factory is the TABLE's answer, not the driver's: the same
        // IsSpawnPoint column Economy.h::queueBuild reads to find a build point.
        if (o.terrainIndex >= 0 &&
            static_cast<std::size_t>(o.terrainIndex) < s.terrainDefs.size() &&
            s.terrainDefs[o.terrainIndex].isSpawnPoint) {
            b.side[o.owner].factoriesHeld += 1;
        }
    }
    for (const Objective& o : s.economy.objectives)
        if (o.terrainIndex >= 0 &&
            static_cast<std::size_t>(o.terrainIndex) < s.terrainDefs.size() &&
            s.terrainDefs[o.terrainIndex].isSpawnPoint) b.factoryTotal += 1;
    for (const DriverUnit& u : s.units)
        if (u.side >= 0 && u.side < SIDE_COUNT) b.side[u.side].survivingHp += u.hp;
    return b;
}

std::string stateHash(const Session& s) {
    std::vector<const DriverUnit*> ordered;
    for (const DriverUnit& u : s.units) ordered.push_back(&u);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const DriverUnit* a, const DriverUnit* b) {
                         if (!hexEqual(a->hex, b->hex)) return hexLess(a->hex, b->hex);
                         return a->id < b->id;
                     });
    std::string acc = num(s.bounds.cols) + "x" + num(s.bounds.rows) + "|";
    for (int t : s.terrain) acc += num(t) + ",";
    acc += "|";
    for (const DriverUnit* u : ordered) {
        int col = 0, row = 0;
        axialToOffset(u->hex, col, row);
        acc += num(u->id) + ":" + num(u->side) + ":" + num(u->defIndex) + ":" +
               num(col) + ":" + num(row) + ":" + num(u->hp) + ";";
    }
    // Row 4 state is part of the state a refused command must not change, so it is
    // part of the hash (GATE-DRV-06). Objectives and captures are visited in
    // canonical hex order for the same reason the units above are.
    acc += "|turn" + num(s.turnNumber) + "|";
    for (int i = 0; i < SIDE_COUNT; ++i)
        acc += num(s.economy.side[i].fameTotal) + "/" + num(s.economy.side[i].fameCombat) + ";";
    {
        std::vector<Hex> keys;
        for (const Objective& o : s.economy.objectives) keys.push_back(o.hex);
        sortCanonical(keys);
        for (const Hex& k : keys)
            for (const Objective& o : s.economy.objectives)
                if (hexEqual(o.hex, k)) {
                    int c = 0, r = 0;
                    axialToOffset(o.hex, c, r);
                    acc += "o" + num(c) + ":" + num(r) + ":" + num(o.owner) + ";";
                }
        std::vector<Hex> ckeys;
        for (const CaptureProgress& p : s.economy.captures) ckeys.push_back(p.hex);
        sortCanonical(ckeys);
        for (const Hex& k : ckeys)
            for (const CaptureProgress& p : s.economy.captures)
                if (hexEqual(p.hex, k))
                    acc += "c" + num(p.unitId) + ":" + num(p.turnsHeld) + ";";
        for (const PendingBuild& p : s.economy.pending) {
            int c = 0, r = 0;
            axialToOffset(p.factoryHex, c, r);
            acc += "b" + num(c) + ":" + num(r) + ":" + num(p.side) + ":" + num(p.defIndex) + ";";
        }
    }
    // Row 5 state is likewise part of what a refused command must not change, and
    // Turn.h supplies its own digest rather than the driver re-deriving one.
    acc += "|match" + stateDigest(s.match) + "|";
    for (int i = 0; i < SIDE_COUNT; ++i) acc += "f" + num(s.flagUnit[i]) + ";";
    // The per-factory build record is no longer digested here: it moved into
    // TurnState, so `stateDigest(s.match)` above already covers it. Digesting it a
    // second time from a driver-side copy is what would let the two drift.
    for (int i : s.buildlist) acc += "L" + num(i) + ";";
    // Row 7. Which scenario is installed is state a refused command must not change.
    acc += "|scn" + (s.scenarioLoaded ? s.scenario.scenarioId : std::string("-")) + "|";
    unsigned long long h = 1469598103934665603ULL;          // FNV-1a, 64-bit
    for (char c : acc) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ULL; }
    std::string hex;
    for (int i = 15; i >= 0; --i) hex += "0123456789abcdef"[(h >> (i * 4)) & 0xF];
    return hex;
}

// ---------------------------------------------------------------------------
// command execution
// ---------------------------------------------------------------------------
namespace {

void renderMap(const Session& s, std::vector<std::string>& out) {
    Board b = buildBoard(s);
    for (int row = 0; row < s.bounds.rows; ++row) {
        std::string line = (row & 1) ? "  " : "";
        line += num(row) + " ";
        for (int col = 0; col < s.bounds.cols; ++col) {
            const Hex h = offsetToAxial(col, row);
            const int occ = b.occupantAt(h);
            if (occ != OCCUPANT_NONE) {
                const DriverUnit* u = findUnitById(s, occ);
                line += num(u->side) + std::string(1, s.unitDefs[u->defIndex].id[0]) + " ";
            } else {
                line += std::string(1, glyphFor(s.terrainDefs[s.terrain[
                    static_cast<std::size_t>(row) * s.bounds.cols + col]].id)) + "  ";
            }
        }
        out.push_back(line);
    }
    std::string cols = "   ";
    for (int col = 0; col < s.bounds.cols; ++col) cols += num(col) + "  ";
    out.push_back(cols);
}

// One lane, printed with THE RELATION NAMED AT THE SITE. Integer order carries no
// information, so neither line relies on which integer is bigger: the ceiling is
// printed as a ceiling, and the bare "X against Y" pair quantifies its right-hand
// term as the SET MINIMUM over the opposing seat's capturers (Q28), never as a cost
// from one named hex.
std::string laneLine(const ScenarioLane& l, const ScenarioLoadResult& r) {
    std::string line = "side " + num(l.side) + " lane " + hexLabel(l.infantry) + " -> " +
                       hexLabel(l.objective) + ": ";
    if (!l.laneFound) return line + "no Bridge-free land route (T-SCN-06)";
    line += num(l.laneCost) + " MP against the " + num(r.ceiling) +
            " MP ceiling (T-SCN-06)";
    if (!l.opposingFound)
        return line + "; the opposing seat has no route to that objective (T-SCN-11)";
    return line + "; non-contention " + num(l.laneCost) + " against " + num(l.opposingCost) +
           " (T-SCN-11, the set minimum over the opposing seat's capturers, achieved "
           "from " + hexLabel(l.opposingFrom) + ")";
}

std::string describe(const MatchResult& r) {
    std::string line = std::string("match over: ") + tierName(r.tier) +
                       " (" + causeName(r.cause) + ")";
    if (r.winner != SIDE_NONE) line += " — side " + num(r.winner) + " wins";
    if (r.decidedByKey != 0)   line += ", decided at tiebreak key " + num(r.decidedByKey);
    return line;
}

// Starts the active side's turn: Turn.h's beginTurn, then Turn.h's start-of-turn
// repair moment. The amounts come from the verified repairAmount through Turn.h; the
// driver applies the HP it is handed and computes none of it.
void openActiveTurn(Session& s, std::vector<std::string>& out) {
    const BoardSnapshot snap = snapshotOf(s);
    const MatchResult r = beginTurn(s.match, snap);
    if (r.tier != ResultTier::InProgress) { out.push_back(describe(r)); return; }

    std::vector<RepairSubject> subjects;
    for (const DriverUnit& u : s.units) {
        RepairSubject rs;
        rs.unitId = u.id;
        rs.side   = u.side;
        rs.unit   = combatUnit(s, u);
        const Objective* o = findObjective(s.economy, u.hex);
        rs.onOwnedObjective = (o != nullptr && o->owner == u.side);   // Economy.h owns it
        rs.enemyAdjacent    = enemyAdjacent(s, u);
        subjects.push_back(rs);
    }
    const std::vector<RepairApplied> healed = applyStartOfTurnRepair(s.match, subjects);
    for (const RepairApplied& a : healed) {
        if (a.amount <= 0) continue;
        DriverUnit* u = mutableUnitById(s, a.unitId);
        if (u == nullptr) continue;
        u->hp += a.amount;
        out.push_back("  repaired #" + num(a.unitId) + " +" + num(a.amount) +
                      " -> hp " + num(u->hp));
    }

    // The rest of the start-of-turn moment. Row 5 defines WHEN; row 4's own calls do
    // the work, since the turn module accrues no income and ticks no capture itself.
    // spec/turn_spec.md names ONLY `accrueIncome` as a caller call -- it states no
    // capture tick and no order among the three. The order below is therefore this
    // driver's, and it is the order the Director RULED on 2026-08-03: the tick runs
    // AFTER income, so an objective whose capture completes at the start of turn T
    // pays its new owner from T+1.
    const int side = s.match.activeSide;
    // The build allowance is no longer cleared here. `beginTurn` clears it alongside
    // the two per-unit flags (T-TURN-01(e), T-TURN-10), so the renewal moment is one
    // line in the module that owns the turn rather than a driver convention that the
    // AI happened to share and the player's `build` never consulted.
    const int gained = accrueIncome(s.economy, s.terrainDefs, side, s.match.turnNumber);
    if (gained > 0)
        out.push_back("  income +" + num(gained) + " -> fameTotal " +
                      num(s.economy.side[side].fameTotal));
    {
        std::vector<CaptureOccupant> occ;
        for (const DriverUnit& u : s.units) {
            CaptureOccupant c;
            c.hex = u.hex; c.unitId = u.id; c.side = u.side;
            c.canCapture = s.unitDefs[u.defIndex].canCapture;
            occ.push_back(c);
        }
        for (const Hex& h : captureTick(s.economy, occ, side)) {
            int c = 0, r = 0;
            axialToOffset(h, c, r);
            out.push_back("  side " + num(side) + " captured (" + num(c) + "," + num(r) + ")");
        }
    }

    out.push_back("turn " + num(s.match.turnNumber) + "/" + num(s.match.turnCap) +
                  " — side " + num(side) + " to move");
}

// Alternation is Turn.h's, so the driver reads `activeSide` and refuses; it decides
// nothing about whose turn it is.
bool wrongSideForTurn(const Session& s, int side, std::vector<std::string>& out) {
    if (!s.match.running || side == s.match.activeSide) return false;
    out.push_back("refused: side " + num(side) + " is not the active side (side " +
                  num(s.match.activeSide) + " is)");
    return true;
}

} // namespace

bool execute(Session& s, const std::string& line, std::vector<std::string>& out) {
    const std::vector<std::string> t = tokenize(line);
    if (t.empty()) return true;
    const std::string& cmd = t[0];

    if (cmd == "quit" || cmd == "exit") { out.push_back("bye"); return false; }

    if (cmd == "help") {
        out.push_back("map | units | fixture <name> | place <side> <Type> <col> <row>");
        out.push_back("remove <id> | hp <id> <v> | dist <c1> <r1> <c2> <r2>");
        out.push_back("reach <id> | path <id> <col> <row> | move <id> <col> <row>");
        out.push_back("forecast <atk> <def> | attack <atk> <def> | repair <id> <owned 0|1>");
        out.push_back("row 4: fame | objectives | turn <n> | income <side> |");
        out.push_back("       build <side> <Type> <col> <row> | capture <side>");
        out.push_back("row 5: match <firstSide> [<turnCap>] | endturn | standings |");
        out.push_back("       result | flag <side> <id>");
        out.push_back("row 6: ai | ai buildlist <Type>...   (plays the active side's turn)");
        out.push_back("row 7: scenario load <path> | scenario report | scenario hash");
        out.push_back("       (with a scenario loaded, 'match <firstSide>' takes the cap");
        out.push_back("        from the file -- the cap is per-scenario data, Q7)");
        out.push_back("row 8: snapshot   (§4.7 Stub 8's view model; 'scenario snapshot' is");
        out.push_back("       the same command under the spelling row 7 advertised)");
        out.push_back("NOTE: still no UI -- row 8 ships the BINDING CONTRACT, not widgets and");
        out.push_back("not layout (§2.11's lane). Text in, text out. With no match running the");
        out.push_back("board is");
        out.push_back("a free sandbox and 'turn <n>' is a debug setter; 'match' hands the turn");
        out.push_back("number to the turn loop, which then refuses the setter. On a built-in");
        out.push_back("fixture 'flag' is a debug designation; a loaded scenario sets it from");
        out.push_back("the file's isFlag instead.");
        return true;
    }

    if (cmd == "fixture") {
        if (t.size() == 1 || t[1] == "list") {
            std::string names;
            for (const std::string& n : fixtureNames()) names += n + " ";
            out.push_back("fixtures: " + names);
            return true;
        }
        std::string err;
        if (!loadFixture(s, t[1], err)) { out.push_back("refused: " + err); return true; }
        out.push_back("loaded fixture '" + t[1] + "' (" + num(s.bounds.cols) + "x" +
                      num(s.bounds.rows) + ")");
        return true;
    }

    // --- row 7. The file format is Scenario.h's; the driver parses nothing. -----
    if (cmd == "scenario") {
        if (t.size() == 3 && t[1] == "load") {
            Scenario sc;
            const ScenarioLoadResult r = loadScenario(t[2], s.unitDefs, s.terrainDefs, sc);
            if (!r.ok) {
                // A failure refuses the WHOLE file, so nothing is installed -- but the
                // lanes measured before the refusal are still reported, because an
                // author needs a number and not a boolean.
                out.push_back("refused: " + r.failedId + " -- " + r.reason);
                for (const ScenarioLane& l : r.lanes) out.push_back("  " + laneLine(l, r));
                return true;
            }
            std::string e;
            if (!installScenario(s, sc, e)) { out.push_back("refused: " + e); return true; }
            s.scenarioReport = r;
            out.push_back("loaded scenario '" + sc.scenarioId + "' (" + num(sc.bounds.cols) +
                          "x" + num(sc.bounds.rows) + ", " +
                          num(static_cast<int>(sc.placements.size())) + " placements, symmetry " +
                          symmetryName(sc.symmetry) + ", turnCap " + num(sc.turnCap) + ")");
            out.push_back("  hash " + scenarioHash(sc));
            for (const ScenarioLane& l : r.lanes) out.push_back("  " + laneLine(l, r));
            return true;
        }
        if (t.size() == 2 && t[1] == "report") {
            if (!s.scenarioLoaded) {
                out.push_back("refused: no scenario loaded -- run 'scenario load <path>'");
                return true;
            }
            out.push_back("scenario '" + s.scenario.scenarioId + "', capturing row Move " +
                          num(s.scenarioReport.captureMove) + ", ceiling " +
                          num(s.scenarioReport.ceiling) + " MP (2 x Move, derived)");
            for (const ScenarioLane& l : s.scenarioReport.lanes)
                out.push_back("  " + laneLine(l, s.scenarioReport));
            return true;
        }
        if (t.size() == 2 && t[1] == "hash") {
            if (!s.scenarioLoaded) {
                out.push_back("refused: no scenario loaded -- run 'scenario load <path>'");
                return true;
            }
            out.push_back(scenarioHash(s.scenario));
            return true;
        }
        if (t.size() == 2 && t[1] == "snapshot") {
            // Row 8 landed, so this projects rather than refuses. It is the same
            // command as the top-level `snapshot` -- the spelling is kept because the
            // help has advertised it since row 7. A view model of no board is not a
            // thing to invent, so with none loaded it refuses like everything else.
            if (!s.loaded) {
                out.push_back("refused: no board -- run 'fixture <name>' first");
                return true;
            }
            printUiSnapshot(s, out);
            return true;
        }
        out.push_back("refused: usage: scenario load <path> | scenario report | scenario hash");
        return true;
    }

    if (!s.loaded) { out.push_back("refused: no board -- run 'fixture <name>' first"); return true; }

    // Row 8. The driver holds no view model of its own: it composes the world and
    // Ui.h projects it (GATE-DRV-12).
    if (cmd == "snapshot") { printUiSnapshot(s, out); return true; }

    if (cmd == "map")  { renderMap(s, out); return true; }

    if (cmd == "units") {
        if (s.units.empty()) { out.push_back("(no units)"); return true; }
        for (const DriverUnit& u : s.units) {
            int col = 0, row = 0;
            axialToOffset(u.hex, col, row);
            const UnitDef& d = s.unitDefs[u.defIndex];
            out.push_back("#" + num(u.id) + " side " + num(u.side) + " " + d.id +
                          " at (" + num(col) + "," + num(row) + ") hp " + num(u.hp) +
                          "/" + num(d.hpMax) + " move " + num(d.move) +
                          " range " + num(d.rangeMin) + "-" + num(d.rangeMax));
        }
        return true;
    }

    if (cmd == "place") {
        int side = 0, col = 0, row = 0;
        if (t.size() != 5 || !parseInt(t[1], side) || !parseInt(t[3], col) || !parseInt(t[4], row)) {
            out.push_back("refused: usage: place <side> <Type> <col> <row>"); return true;
        }
        if (side != 0 && side != 1) { out.push_back("refused: side must be 0 or 1"); return true; }
        int defIndex = -1;
        for (std::size_t i = 0; i < s.unitDefs.size(); ++i)
            if (s.unitDefs[i].id == t[2]) defIndex = static_cast<int>(i);
        if (defIndex < 0) { out.push_back("refused: no unit type '" + t[2] + "'"); return true; }
        const Hex h = offsetToAxial(col, row);
        if (!inBounds(h, s.bounds)) { out.push_back("refused: (" + num(col) + "," + num(row) +
                                                    ") is out of bounds"); return true; }
        Board b = buildBoard(s);
        if (b.occupantAt(h) != OCCUPANT_NONE) { out.push_back("refused: hex is occupied"); return true; }
        const TerrainDef& td = s.terrainDefs[b.terrainAt(h)];
        if (!td.passLand) { out.push_back("refused: " + td.id + " is not passable to land"); return true; }
        DriverUnit u;
        u.id = s.nextUnitId++;
        u.side = side; u.defIndex = defIndex; u.hex = h; u.hp = s.unitDefs[defIndex].hpMax;
        u.placement = h;            // hand-placed: its own hex, which no guidedOpening names
        s.units.push_back(u);
        out.push_back("placed #" + num(u.id) + " " + s.unitDefs[defIndex].id +
                      " side " + num(side) + " at (" + num(col) + "," + num(row) + ")");
        return true;
    }

    if (cmd == "remove") {
        int id = 0;
        if (t.size() != 2 || !parseInt(t[1], id)) { out.push_back("refused: usage: remove <id>"); return true; }
        if (findUnitById(s, id) == nullptr) { out.push_back("refused: no unit " + num(id)); return true; }
        s.units.erase(std::remove_if(s.units.begin(), s.units.end(),
                       [id](const DriverUnit& u) { return u.id == id; }), s.units.end());
        out.push_back("removed #" + num(id));
        return true;
    }

    if (cmd == "hp") {
        int id = 0, v = 0;
        if (t.size() != 3 || !parseInt(t[1], id) || !parseInt(t[2], v)) {
            out.push_back("refused: usage: hp <id> <value>"); return true; }
        DriverUnit* u = mutableUnitById(s, id);
        if (u == nullptr) { out.push_back("refused: no unit " + num(id)); return true; }
        const int hpMax = s.unitDefs[u->defIndex].hpMax;
        if (v < 1 || v > hpMax) { out.push_back("refused: hp must be 1.." + num(hpMax)); return true; }
        u->hp = v;
        out.push_back("#" + num(id) + " hp = " + num(v));
        return true;
    }

    if (cmd == "dist") {
        int c1 = 0, r1 = 0, c2 = 0, r2 = 0;
        if (t.size() != 5 || !parseInt(t[1], c1) || !parseInt(t[2], r1) ||
            !parseInt(t[3], c2) || !parseInt(t[4], r2)) {
            out.push_back("refused: usage: dist <c1> <r1> <c2> <r2>"); return true; }
        const Hex a = offsetToAxial(c1, r1), b = offsetToAxial(c2, r2);
        if (!inBounds(a, s.bounds) || !inBounds(b, s.bounds)) {
            out.push_back("refused: a hex is out of bounds"); return true; }
        out.push_back("distance = " + num(hexDistance(a, b)));
        return true;
    }

    if (cmd == "reach") {
        int id = 0;
        if (t.size() != 2 || !parseInt(t[1], id)) { out.push_back("refused: usage: reach <id>"); return true; }
        const DriverUnit* u = findUnitById(s, id);
        if (u == nullptr) { out.push_back("refused: no unit " + num(id)); return true; }
        const Board b = buildBoard(s);
        const std::vector<ReachEntry> set =
            reachable(b, s.terrainDefs, u->hex, s.unitDefs[u->defIndex].move);
        out.push_back("reachable (" + num(static_cast<int>(set.size())) + " hexes, cost <= " +
                      num(s.unitDefs[u->defIndex].move) + "):");
        for (const ReachEntry& e : set) {
            int col = 0, row = 0;
            axialToOffset(e.hex, col, row);
            out.push_back("  (" + num(col) + "," + num(row) + ") cost " + num(e.cost));
        }
        return true;
    }

    if (cmd == "path" || cmd == "move") {
        int id = 0, col = 0, row = 0;
        if (t.size() != 4 || !parseInt(t[1], id) || !parseInt(t[2], col) || !parseInt(t[3], row)) {
            out.push_back("refused: usage: " + cmd + " <id> <col> <row>"); return true; }
        const DriverUnit* u = findUnitById(s, id);
        if (u == nullptr) { out.push_back("refused: no unit " + num(id)); return true; }
        const Hex goal = offsetToAxial(col, row);
        if (!inBounds(goal, s.bounds)) { out.push_back("refused: (" + num(col) + "," + num(row) +
                                                       ") is out of bounds"); return true; }
        const Board b = buildBoard(s);
        std::vector<Hex> route;
        int cost = 0;
        if (!findPath(b, s.terrainDefs, u->hex, goal, s.unitDefs[u->defIndex].move, route, cost)) {
            out.push_back("refused: (" + num(col) + "," + num(row) +
                          ") is not reachable within " + num(s.unitDefs[u->defIndex].move) +
                          " movement");
            return true;
        }
        std::string desc;
        for (const Hex& h : route) {
            int c = 0, r = 0;
            axialToOffset(h, c, r);
            desc += "(" + num(c) + "," + num(r) + ")";
        }
        if (cmd == "path") { out.push_back("cost " + num(cost) + ": " + desc); return true; }
        // Row 5: whether this unit may MOVE is Turn.h's answer, and it is the MOVE
        // flag that a move spends -- not the act. Asked AFTER the route is known and
        // BEFORE anything moves, so a refusal changes nothing. A unit that has
        // already attacked from where it stands still reaches this and still moves
        // (T-TURN-01); until the two flags existed this call was `markActed`, which
        // is why §2.1's own move-then-act sequence was unimplementable.
        if (s.match.running) {
            std::string e;
            if (!markMoved(s.match, id, u->side, e)) { out.push_back("refused: " + e); return true; }
        }
        mutableUnitById(s, id)->hex = goal;
        out.push_back("#" + num(id) + " moved, cost " + num(cost) + ": " + desc);
        return true;
    }

    if (cmd == "forecast" || cmd == "attack") {
        int a = 0, d = 0;
        if (t.size() != 3 || !parseInt(t[1], a) || !parseInt(t[2], d)) {
            out.push_back("refused: usage: " + cmd + " <attackerId> <defenderId>"); return true; }
        const AttackOutcome o = computeAttack(s, a, d);
        if (!o.legal) { out.push_back("refused: " + o.reason); return true; }
        const std::string atkName = s.unitDefs[findUnitById(s, a)->defIndex].id;
        const std::string defName = s.unitDefs[findUnitById(s, d)->defIndex].id;
        if (cmd == "forecast") {
            out.push_back("at distance " + num(o.distance) + ": " + atkName + " deals " +
                          num(o.damage) + (o.defenderDies ? " (kills " + defName + ")" : ""));
            out.push_back(o.counterFires
                ? "  counter: " + defName + " returns " + num(o.counterDamage) +
                  (o.attackerDies ? " (kills " + atkName + ")" : "")
                : std::string("  counter: none"));
            return true;
        }
        // Row 5: same discipline as `move` -- the outcome is already computed, so
        // Turn.h's refusal lands before any HP changes.
        if (s.match.running) {
            std::string e;
            if (!markActed(s.match, a, findUnitById(s, a)->side, e)) {
                out.push_back("refused: " + e); return true; }
        }
        // Resolution applies exactly what the forecast above reported (GATE-DRV-03).
        const int atkSide = findUnitById(s, a)->side;
        const int defSide = findUnitById(s, d)->side;
        const int defDef  = findUnitById(s, d)->defIndex;
        out.push_back(atkName + " hits " + defName + " for " + num(o.damage));
        if (o.defenderDies) {
            s.units.erase(std::remove_if(s.units.begin(), s.units.end(),
                           [d](const DriverUnit& u) { return u.id == d; }), s.units.end());
            // Row 4: the kill award. Economy.h decides the amount. Whether the victim
            // was a flag now has an answer -- the `flag` command's debug designation,
            // read off Stub 7's `isFlag` when a scenario was loaded. Undesignated
            // sides pass false, exactly as before.
            const bool victimIsFlag =
                (defSide >= 0 && defSide < SIDE_COUNT && s.flagUnit[defSide] == d);
            awardKill(s.economy, atkSide, s.unitDefs[defDef], victimIsFlag);
            out.push_back(defName + " #" + num(d) + " destroyed — side " + num(atkSide) +
                          " earns " + num(killAward(s.unitDefs[defDef], victimIsFlag)) +
                          (victimIsFlag ? " Fame (flat flag award, replaces the ordinary"
                                          " one, Q5) -> fameCombat "
                                        : " Fame (half cost, Q5) -> fameCombat ") +
                          num(s.economy.side[atkSide].fameCombat));
            // Row 5: a downed flag ends the match at once. Turn.h decides that, and
            // it is asked here rather than at the next turn boundary.
            if (s.match.running) {
                const MatchResult r = checkImmediate(s.match, snapshotOf(s));
                if (r.tier != ResultTier::InProgress) out.push_back(describe(r));
            }
            return true;
        }
        mutableUnitById(s, d)->hp -= o.damage;
        if (o.counterFires) {
            out.push_back(defName + " counters for " + num(o.counterDamage));
            if (o.attackerDies) {
                s.units.erase(std::remove_if(s.units.begin(), s.units.end(),
                               [a](const DriverUnit& u) { return u.id == a; }), s.units.end());
                out.push_back(atkName + " #" + num(a) + " destroyed");
            } else {
                mutableUnitById(s, a)->hp -= o.counterDamage;
            }
        }
        return true;
    }

    // --- row 4 surfaces. Every one of these delegates to Economy.h -------------
    if (cmd == "fame") {
        for (int i = 0; i < SIDE_COUNT; ++i)
            out.push_back("side " + num(i) + ": fameTotal " + num(s.economy.side[i].fameTotal) +
                          ", fameCombat " + num(s.economy.side[i].fameCombat));
        out.push_back(s.match.running
            ? "turn " + num(s.match.turnNumber) + "/" + num(s.match.turnCap) +
              " — side " + num(s.match.activeSide) + " to move"
            : "turn " + num(s.turnNumber) +
              " (no match running; 'turn <n>' sets the sandbox number)");
        return true;
    }

    if (cmd == "objectives") {
        if (s.economy.objectives.empty()) { out.push_back("(no objectives)"); return true; }
        for (const Objective& o : s.economy.objectives) {
            int col = 0, row = 0;
            axialToOffset(o.hex, col, row);
            const TerrainDef& td = s.terrainDefs[o.terrainIndex];
            std::string owner = (o.owner == OWNER_NEUTRAL) ? "neutral" : ("side " + num(o.owner));
            std::string prog;
            for (const CaptureProgress& c : s.economy.captures)
                if (hexEqual(c.hex, o.hex))
                    prog = " [capture " + num(c.turnsHeld) + "/" + num(s.economy.captureTurns) +
                           " by #" + num(c.unitId) + "]";
            out.push_back("(" + num(col) + "," + num(row) + ") " + td.id + " — " + owner +
                          ", income " + num(td.incomeFame) + prog);
        }
        return true;
    }

    if (cmd == "turn") {
        int n = 0;
        if (t.size() != 2 || !parseInt(t[1], n) || n < 1) {
            out.push_back("refused: usage: turn <n>, n >= 1"); return true; }
        if (s.match.running) {
            out.push_back("refused: a match is running — the turn loop owns the turn "
                          "number; 'endturn' advances it");
            return true;
        }
        s.turnNumber = n;
        out.push_back("turn = " + num(n) + " (a debug setter, not a turn loop)");
        return true;
    }

    if (cmd == "income") {
        int side = 0;
        if (t.size() != 2 || !parseInt(t[1], side)) {
            out.push_back("refused: usage: income <side>"); return true; }
        if (side < 0 || side >= SIDE_COUNT) { out.push_back("refused: side must be 0 or 1"); return true; }
        if (wrongSideForTurn(s, side, out)) return true;
        const int turn = currentTurn(s);
        const int gained = accrueIncome(s.economy, s.terrainDefs, side, turn);
        out.push_back("side " + num(side) + " accrued " + num(gained) + " on turn " +
                      num(turn) + (turn <= 1 ? " (no accrual on turn 1 — Q8)" : "") +
                      " -> fameTotal " + num(s.economy.side[side].fameTotal));
        return true;
    }

    if (cmd == "build") {
        int side = 0, col = 0, row = 0;
        if (t.size() != 5 || !parseInt(t[1], side) || !parseInt(t[3], col) || !parseInt(t[4], row)) {
            out.push_back("refused: usage: build <side> <Type> <col> <row>"); return true; }
        int defIndex = -1;
        for (std::size_t i = 0; i < s.unitDefs.size(); ++i)
            if (s.unitDefs[i].id == t[2]) defIndex = static_cast<int>(i);
        if (defIndex < 0) { out.push_back("refused: no unit type '" + t[2] + "'"); return true; }
        if (wrongSideForTurn(s, side, out)) return true;
        const Hex factory = offsetToAxial(col, row);
        std::string e;
        // Row 5, T-TURN-10: one build per factory per turn. Asked as a PREDICATE
        // before anything mutates, because Fame is committed at queue time (Q8(c))
        // and is not refundable -- a refusal arriving after the charge would take
        // Fame for a unit that never queues. Economy.h enforces only the per-PENDING
        // slot half, which is all it can enforce while the turn is an argument to it.
        if (s.match.running && !canBuildAt(s.match, factory, side)) {
            // markBuilt refuses and changes nothing when its predicate is false, so
            // this yields Turn.h's own reason rather than the driver inventing one.
            std::string why;
            markBuilt(s.match, factory, side, why);
            out.push_back("refused: " + why); return true;
        }
        if (!queueBuild(s.economy, s.unitDefs, s.terrainDefs, side, factory, defIndex, e)) {
            out.push_back("refused: " + e); return true; }
        // The allowance is spent only now, once the build has actually queued -- a
        // build refused as unaffordable leaves the factory free to try again.
        if (s.match.running) { std::string me; markBuilt(s.match, factory, side, me); }
        out.push_back("queued " + s.unitDefs[defIndex].id + " at (" + num(col) + "," + num(row) +
                      ") — " + num(s.unitDefs[defIndex].costFame) +
                      " Fame committed at queue time, not refundable (Q8) -> fameTotal " +
                      num(s.economy.side[side].fameTotal));
        std::vector<Hex> occupied;
        for (const DriverUnit& u : s.units) occupied.push_back(u.hex);
        const std::vector<SpawnResult> spawns = resolveBuilds(s.economy, s.bounds, occupied);
        for (const SpawnResult& sp : spawns) {
            if (!sp.spawned) { out.push_back("  boxed in — build waits and holds the slot"); continue; }
            DriverUnit u;
            u.id = s.nextUnitId++;
            u.side = sp.side; u.defIndex = sp.defIndex; u.hex = sp.hex;
            u.placement = sp.hex;   // spawned, not deployed: unmarked by construction
            u.hp = s.unitDefs[sp.defIndex].hpMax;
            s.units.push_back(u);
            int sc = 0, sr = 0;
            axialToOffset(sp.hex, sc, sr);
            out.push_back("  spawned #" + num(u.id) + " at (" + num(sc) + "," + num(sr) + ")");
        }
        return true;
    }

    if (cmd == "capture") {
        int side = 0;
        if (t.size() != 2 || !parseInt(t[1], side)) {
            out.push_back("refused: usage: capture <side>"); return true; }
        if (side < 0 || side >= SIDE_COUNT) { out.push_back("refused: side must be 0 or 1"); return true; }
        if (wrongSideForTurn(s, side, out)) return true;
        std::vector<CaptureOccupant> occ;
        for (const DriverUnit& u : s.units) {
            CaptureOccupant c;
            c.hex = u.hex; c.unitId = u.id; c.side = u.side;
            c.canCapture = s.unitDefs[u.defIndex].canCapture;
            occ.push_back(c);
        }
        const std::vector<Hex> flipped = captureTick(s.economy, occ, side);
        if (flipped.empty()) { out.push_back("nothing changed hands"); return true; }
        for (const Hex& h : flipped) {
            int c = 0, r = 0;
            axialToOffset(h, c, r);
            out.push_back("side " + num(side) + " captured (" + num(c) + "," + num(r) + ")");
        }
        return true;
    }

    // --- row 5 surfaces. Every one of these delegates to Turn.h ----------------
    if (cmd == "match") {
        int first = 0, cap = 0;
        bool haveCap = false;
        if (t.size() == 3 && parseInt(t[1], first) && parseInt(t[2], cap)) {
            haveCap = true;
        } else if (t.size() == 2 && parseInt(t[1], first)) {
            // Q7 ruled the cap is per-scenario data. With a scenario loaded there is
            // a place to read it from, so this form reads it there instead of asking
            // the human to retype a number the file already carries.
            if (!s.scenarioLoaded) {
                out.push_back("refused: no scenario loaded -- give the cap, or run "
                              "'scenario load <path>' so it comes from the file (Q7)");
                return true;
            }
            cap = s.scenario.turnCap;
            haveCap = true;
        }
        if (!haveCap) {
            out.push_back("refused: usage: match <firstSide> [<turnCap>]"); return true; }
        std::string e;
        // The cap is per-scenario data (Q7) and Turn.h refuses an unusable one rather
        // than substituting a default, so no cap value lives in this file.
        if (!initMatch(s.match, first, cap, e)) { out.push_back("refused: " + e); return true; }
        out.push_back("match started — side " + num(first) + " moves first, cap " +
                      num(cap) + " turns (scenario data, Q7)");
        openActiveTurn(s, out);
        return true;
    }

    if (cmd == "endturn") {
        if (t.size() != 1) { out.push_back("refused: usage: endturn"); return true; }
        if (!s.match.running) {
            out.push_back("refused: no match is running — run 'match <firstSide> <turnCap>'");
            return true;
        }
        const MatchResult r = endTurn(s.match, snapshotOf(s));
        if (r.tier != ResultTier::InProgress) { out.push_back(describe(r)); return true; }
        openActiveTurn(s, out);
        return true;
    }

    if (cmd == "standings") {
        const BoardSnapshot b = snapshotOf(s);
        const int objectiveTotal = static_cast<int>(s.economy.objectives.size());
        out.push_back(s.match.running
            ? "turn " + num(s.match.turnNumber) + "/" + num(s.match.turnCap) +
              " — side " + num(s.match.activeSide) + " to move"
            : "turn " + num(currentTurn(s)) + " (no match running)");
        // §2.11.4's three rows, in §2.8's order, because the scoreboard is a
        // restatement of the tiebreak and not a second view of it.
        auto row = [](const std::string& label, const std::string& a,
                      const std::string& c) {
            std::string line = "  " + label;
            while (line.size() < 15) line += ' ';
            line += a;
            while (line.size() < 24) line += ' ';
            return line + c;
        };
        out.push_back("               side 0   side 1");
        out.push_back(row("Destroyed", num(b.side[0].fameCombat), num(b.side[1].fameCombat)));
        out.push_back(row("Objectives",
                          num(b.side[0].objectivesHeld) + "/" + num(objectiveTotal),
                          num(b.side[1].objectivesHeld) + "/" + num(objectiveTotal)));
        out.push_back(row("Unit HP", num(b.side[0].survivingHp), num(b.side[1].survivingHp)));
        // The leader line is the same call the cap would make, so what is displayed
        // during the match and what decides it cannot disagree.
        const MatchResult lead = resolveAtCap(b);
        if (lead.cause == ResultCause::PassivityGuard) out.push_back("  — no engagements —");
        else if (lead.winner == SIDE_NONE)             out.push_back("  leader: none (all keys tied)");
        else out.push_back("  leader: side " + num(lead.winner) + " at key " +
                           num(lead.decidedByKey));
        return true;
    }

    if (cmd == "result") {
        const MatchResult& r = s.match.result;
        if (r.tier != ResultTier::InProgress) { out.push_back(describe(r)); return true; }
        if (!s.match.running) { out.push_back("no match has been played"); return true; }
        out.push_back("in progress — turn " + num(s.match.turnNumber) + "/" +
                      num(s.match.turnCap) + ", side " + num(s.match.activeSide) +
                      " to move");
        return true;
    }

    if (cmd == "flag") {
        int side = 0, id = 0;
        if (t.size() != 3 || !parseInt(t[1], side) || !parseInt(t[2], id)) {
            out.push_back("refused: usage: flag <side> <id>"); return true; }
        if (side < 0 || side >= SIDE_COUNT) { out.push_back("refused: side must be 0 or 1"); return true; }
        const DriverUnit* u = findUnitById(s, id);
        if (u == nullptr) { out.push_back("refused: no unit " + num(id)); return true; }
        if (u->side != side) {
            out.push_back("refused: #" + num(id) + " is on side " + num(u->side));
            return true;
        }
        s.flagUnit[side] = id;
        out.push_back("side " + num(side) + " flag = #" + num(id) +
                      " (a debug designation, for a built-in fixture; a loaded scenario "
                      "sets this from Stub 7's isFlag instead, and Q10 is open on "
                      "exactness either way)");
        return true;
    }

    // --- row 6. The AI decides; `execute` applies. Nothing else changes. --------
    if (cmd == "ai") {
        if (t.size() >= 2 && t[1] == "buildlist") {
            std::vector<int> list;
            for (std::size_t i = 2; i < t.size(); ++i) {
                int defIndex = -1;
                for (std::size_t k = 0; k < s.unitDefs.size(); ++k)
                    if (s.unitDefs[k].id == t[i]) defIndex = static_cast<int>(k);
                if (defIndex < 0) { out.push_back("refused: no unit type '" + t[i] + "'"); return true; }
                list.push_back(defIndex);
            }
            s.buildlist = list;
            std::string names;
            for (int i : list) names += s.unitDefs[i].id + " ";
            out.push_back("buildlist = " + (names.empty() ? std::string("(empty)") : names));
            return true;
        }
        if (t.size() != 1) { out.push_back("refused: usage: ai | ai buildlist <Type>..."); return true; }
        if (!s.match.running) {
            out.push_back("refused: no match is running — run 'match <firstSide> <turnCap>'");
            return true;
        }
        // Ask, render, apply through the SAME door a typed command uses. If a
        // rendered command is refused, that is an AI defect and it is reported
        // rather than swallowed (T-AI-01).
        for (int step = 0; step < 200; ++step) {
            const AiCommand c = nextCommand(aiStateOf(s), s.match.activeSide);
            if (c.kind == AiCommandKind::EndTurn) {
                out.push_back("  ai: end of turn");
                return true;
            }
            const std::string line = renderAiCommand(s, c);
            out.push_back("  ai> " + line);
            std::vector<std::string> sub;
            execute(s, line, sub);
            for (const std::string& l : sub) out.push_back("     " + l);
            for (const std::string& l : sub)
                if (l.find("refused") != std::string::npos) {
                    out.push_back("  ai: STOPPED — its own command was refused");
                    return true;
                }
        }
        out.push_back("  ai: STOPPED — 200 commands without ending the turn");
        return true;
    }

    if (cmd == "repair") {
        int id = 0, owned = 0;
        if (t.size() != 3 || !parseInt(t[1], id) || !parseInt(t[2], owned)) {
            out.push_back("refused: usage: repair <id> <owned 0|1>"); return true; }
        if (owned != 0 && owned != 1) { out.push_back("refused: owned must be 0 or 1"); return true; }
        DriverUnit* u = mutableUnitById(s, id);
        if (u == nullptr) { out.push_back("refused: no unit " + num(id)); return true; }
        const bool adj = enemyAdjacent(s, *u);
        const int amount = repairAmount(combatUnit(s, *u), owned == 1, adj);  // Combat.h decides
        u->hp += amount;
        out.push_back("#" + num(id) + " repaired " + num(amount) + " (owned=" + num(owned) +
                      ", enemyAdjacent=" + (adj ? "1" : "0") + ") -> hp " + num(u->hp));
        return true;
    }

    out.push_back("refused: unknown command '" + cmd + "' -- try 'help'");
    return true;
}

} // namespace strat
