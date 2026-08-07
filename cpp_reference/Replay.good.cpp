// Stratocracy — headless replayer + canonical state hash, §4.11 row 10 part (b).
// See Replay.h and spec/replay_spec.md. THIS FILE DECIDES NO RULE: every refusal it
// returns is a refusal some other module produced, forwarded with that module's own
// reason, and every number it writes was computed by the module that owns it.
#include "Replay.h"

#include <algorithm>
#include <string>
#include <vector>

namespace strat {
namespace {

std::string num(int v) { return std::to_string(v); }

// The canonical-order comparator, applied to whatever a group is keyed by. Every
// group below sorts a vector of INDICES so the state itself is never reordered --
// hashing must not be able to mutate the thing it reads.
//
// `tieOf` is NOT decoration. Canonical hex order is a total order over HEXES, not
// over the records keyed by them, and two records can share a hex -- two capture
// entries at one tile, or two units mid-resolution. Falling back on storage order
// there would make the digest depend on vector insertion order, which is exactly
// the leak T-HEX-07 exists to close. The driver's own debug digest breaks the same
// tie the same way, by id.
template <typename F, typename G>
std::vector<int> canonicalIndices(std::size_t n, F keyOf, G tieOf) {
    std::vector<int> idx;
    idx.reserve(n);
    for (std::size_t i = 0; i < n; ++i) idx.push_back(static_cast<int>(i));
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
        if (!hexEqual(keyOf(a), keyOf(b))) return hexLess(keyOf(a), keyOf(b));
        return tieOf(a) < tieOf(b);
    });
    return idx;
}

const UnitDef& defOf(const GameState& g, const RulesTables& t, const GameUnit& u) {
    return (*t.units)[u.defIndex];
}

// The combat stat block for a unit: every field looked up, none stored.
Unit combatUnit(const GameState& g, const RulesTables& t, const GameUnit& u) {
    const UnitDef& d = defOf(g, t, u);
    Unit c;
    c.atk = d.atk; c.def = d.def; c.hp = u.hp; c.hpMax = d.hpMax;
    c.rangeMin = d.rangeMin; c.rangeMax = d.rangeMax;
    c.type = d.type;
    return c;
}

int terrainDefPctAt(const GameState& g, const RulesTables& t, const Hex& h) {
    int col = 0, row = 0;
    axialToOffset(h, col, row);
    if (col < 0 || row < 0 || col >= g.bounds.cols || row >= g.bounds.rows) return 0;
    const int off = row * g.bounds.cols + col;
    if (off < 0 || off >= static_cast<int>(g.terrain.size())) return 0;
    return (*t.terrain)[g.terrain[off]].defensePct;
}

GameUnit* mutableUnit(GameState& g, int id) {
    for (GameUnit& u : g.units) if (u.id == id) return &u;
    return nullptr;
}

// Is any enemy unit on one of this unit's six in-bounds neighbours? Hex.h enumerates
// the neighbours; this adds no adjacency notion of its own.
bool enemyAdjacent(const GameState& g, const GameUnit& u) {
    Hex around[HEX_DIRECTIONS];
    const int n = neighbors(u.hex, g.bounds, around);
    for (int i = 0; i < n; ++i)
        for (const GameUnit& other : g.units)
            if (other.side != u.side && hexEqual(other.hex, around[i])) return true;
    return false;
}

// The §2.8 snapshot row 5 grades against. Built exactly as the driver builds it --
// every criterion read from the module that owns it, and "which tile is a factory"
// answered by the TABLE's IsSpawnPoint column rather than here.
BoardSnapshot snapshotOf(const GameState& g, const RulesTables& t) {
    BoardSnapshot b;
    for (const GameUnit& u : g.units) {
        if (u.side < 0 || u.side >= SIDE_COUNT) continue;
        b.side[u.side].survivingHp += u.hp;
    }
    // A side that designates no flag cannot lose by flag death, so this reports it
    // alive rather than inventing a designation -- the driver's rule.
    for (int i = 0; i < SIDE_COUNT; ++i) {
        b.side[i].fameCombat = g.economy.side[i].fameCombat;
        b.side[i].flagAlive  =
            (g.flagUnit[i] < 0) || (findGameUnit(g, g.flagUnit[i]) != nullptr);
    }
    for (const Objective& o : g.economy.objectives) {
        const bool isFactory =
            o.terrainIndex >= 0 &&
            static_cast<std::size_t>(o.terrainIndex) < t.terrain->size() &&
            (*t.terrain)[o.terrainIndex].isSpawnPoint;
        if (isFactory) b.factoryTotal += 1;
        if (o.owner < 0 || o.owner >= SIDE_COUNT) continue;
        b.side[o.owner].objectivesHeld += 1;          // factories AND towns
        if (isFactory) b.side[o.owner].factoriesHeld += 1;
    }
    return b;
}

ReplayResult okResult(int applied) {
    ReplayResult r;
    r.ok = true;
    r.applied = applied;
    return r;
}

ReplayResult refuse(const std::string& reason) {
    ReplayResult r;
    r.ok = false;
    r.failedId = "T-SAVE-05";
    r.reason = reason;
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
Board boardOf(const GameState& g) {
    Board b;
    b.bounds  = g.bounds;
    b.terrain = g.terrain;
    b.occupant.assign(static_cast<std::size_t>(g.bounds.cols) * g.bounds.rows,
                      OCCUPANT_NONE);
    for (const GameUnit& u : g.units) {
        const int i = b.index(u.hex);
        if (i >= 0) b.occupant[static_cast<std::size_t>(i)] = u.id;
    }
    return b;
}

const GameUnit* findGameUnit(const GameState& g, int id) {
    for (const GameUnit& u : g.units) if (u.id == id) return &u;
    return nullptr;
}

bool isFlagUnit(const GameState& g, const GameUnit& u) {
    return u.side >= 0 && u.side < SIDE_COUNT && g.flagUnit[u.side] == u.id;
}

// ---------------------------------------------------------------------------
// §4.10's canonical state hash.
// ---------------------------------------------------------------------------
std::string canonicalStateBytes(const GameState& g) {
    std::string s;
    auto put = [&s](const std::string& tag, int v) { s += tag; s += ':'; s += num(v); s += ';'; };

    // turn counter; side to move
    put("turn", g.turn.turnNumber);
    put("side", g.turn.activeSide);

    // per side: fameTotal, fameCombat
    for (int i = 0; i < SIDE_COUNT; ++i) {
        put("fT", g.economy.side[i].fameTotal);
        put("fC", g.economy.side[i].fameCombat);
    }

    // objective ownership, canonical hex order
    {
        const std::vector<Objective>& v = g.economy.objectives;
        for (int i : canonicalIndices(v.size(), [&](int k) { return v[k].hex; },
                                      [&](int k) { return v[k].owner; })) {
            put("oq", v[i].hex.q); put("or", v[i].hex.r); put("ow", v[i].owner);
        }
    }

    // per unit, canonical hex order. hasMoved/hasActed are read off TurnState, which
    // is where row 5 holds them -- they are per-unit FACTS, not per-unit FIELDS.
    {
        const std::vector<GameUnit>& v = g.units;
        for (int i : canonicalIndices(v.size(), [&](int k) { return v[k].hex; },
                                      [&](int k) { return v[k].id; })) {
            put("ui", v[i].id);
            put("us", v[i].side);
            put("uq", v[i].hex.q);
            put("ur", v[i].hex.r);
            put("uh", v[i].hp);
            put("uf", isFlagUnit(g, v[i]) ? 1 : 0);
            put("um", hasMoved(g.turn, v[i].id) ? 1 : 0);
            put("ua", hasActed(g.turn, v[i].id) ? 1 : 0);
        }
    }

    // capture progress -- PER TILE, naming the unit. Economy.h holds it this way and
    // Q4/T-FAME-05 is why: progress can never transfer.
    {
        const std::vector<CaptureProgress>& v = g.economy.captures;
        for (int i : canonicalIndices(v.size(), [&](int k) { return v[k].hex; },
                                      [&](int k) { return v[k].unitId; })) {
            put("cq", v[i].hex.q); put("cr", v[i].hex.r);
            put("cu", v[i].unitId); put("ct", v[i].turnsHeld);
        }
    }

    // per-factory build allowance -- TurnState::builtThisTurn (T-TURN-10).
    {
        const std::vector<Hex>& v = g.turn.builtThisTurn;
        for (int i : canonicalIndices(v.size(), [&](int k) { return v[k]; },
                                      [](int k) { return k; })) {
            put("bq", v[i].q); put("br", v[i].r); put("bb", 1);
        }
    }

    // pending builds -- keyed by factoryHex. `buildWaiting` is NOT emitted: it is
    // exactly "a pending build stands here", recomputable from this group, and
    // §4.10 omits anything recomputable from what it already carries.
    {
        const std::vector<PendingBuild>& v = g.economy.pending;
        for (int i : canonicalIndices(v.size(), [&](int k) { return v[k].factoryHex; },
                                      [&](int k) { return v[k].defIndex; })) {
            put("pq", v[i].factoryHex.q); put("pr", v[i].factoryHex.r);
            put("ps", v[i].side); put("pd", v[i].defIndex);
        }
    }

    return s;
}

std::string canonicalStateHash(const GameState& g) {
    const std::string bytes = canonicalStateBytes(g);
    unsigned long long h = 14695981039346656037ULL;      // FNV-1a 64 offset basis
    for (unsigned char c : bytes) {
        h ^= static_cast<unsigned long long>(c);
        h *= 1099511628211ULL;                            // FNV prime
    }
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = "0123456789abcdef"[h & 0xFULL];
        h >>= 4;
    }
    return out;
}

// ---------------------------------------------------------------------------
void openTurn(GameState& g, const RulesTables& t) {
    if (!g.turn.running) return;
    // Row 5 owns the boundary and may end the match on it.
    if (beginTurn(g.turn, snapshotOf(g, t)).tier != ResultTier::InProgress) return;

    // Start-of-turn repair. Every input is the caller's; Combat.h decides the amount
    // and Turn.h decides whether the moment is live.
    std::vector<RepairSubject> subjects;
    for (const GameUnit& u : g.units) {
        RepairSubject rs;
        rs.unitId = u.id;
        rs.side   = u.side;
        rs.unit   = combatUnit(g, t, u);
        const Objective* o = findObjective(g.economy, u.hex);   // Economy.h owns it
        rs.onOwnedObjective = (o != nullptr && o->owner == u.side);
        rs.enemyAdjacent    = enemyAdjacent(g, u);
        subjects.push_back(rs);
    }
    for (const RepairApplied& a : applyStartOfTurnRepair(g.turn, subjects)) {
        if (a.amount <= 0) continue;
        GameUnit* u = mutableUnit(g, a.unitId);
        if (u != nullptr) u->hp += a.amount;
    }

    // Then income, then the capture tick -- the ruled order, not the other way round.
    const int side = g.turn.activeSide;
    accrueIncome(g.economy, *t.terrain, side, g.turn.turnNumber);
    {
        std::vector<CaptureOccupant> occ;
        for (const GameUnit& u : g.units) {
            CaptureOccupant c;
            c.hex = u.hex; c.unitId = u.id; c.side = u.side;
            c.canCapture = (*t.units)[u.defIndex].canCapture;
            occ.push_back(c);
        }
        captureTick(g.economy, occ, side);
    }
}

bool seedFromScenario(GameState& g, const Scenario& sc, const RulesTables& t,
                      int firstSide, std::string& err) {
    if (t.units == nullptr || t.terrain == nullptr) {
        err = "no data tables supplied";
        return false;
    }
    // The board is indexed row-major over bounds, so a terrain list of any other
    // length would be read out of range below. loadScenario validates this; the
    // check is here because this function is public and a caller may hand it a
    // Scenario that never went through the loader.
    if (sc.terrainId.size() !=
        static_cast<std::size_t>(sc.bounds.cols) * static_cast<std::size_t>(sc.bounds.rows)) {
        err = "scenario terrain length does not match its bounds";
        return false;
    }

    GameState next;                    // built aside; assigned only on success

    next.bounds = sc.bounds;
    next.terrain.assign(sc.terrainId.size(), -1);
    for (std::size_t i = 0; i < sc.terrainId.size(); ++i) {
        int idx = -1;
        for (std::size_t k = 0; k < t.terrain->size(); ++k)
            if ((*t.terrain)[k].id == sc.terrainId[i]) idx = static_cast<int>(k);
        if (idx < 0) {
            err = "terrain Id '" + sc.terrainId[i] + "' is in no loaded row";
            return false;
        }
        next.terrain[i] = idx;
    }

    next.nextUnitId = 1;
    // N is not a Stub 7 field. §2.7 fixes it at 1 (Q4), read here the same way
    // Driver's installScenario reads it, rather than inventing a scenario field.
    next.economy.captureTurns = 1;
    for (int i = 0; i < SIDE_COUNT; ++i) next.flagUnit[i] = -1;

    for (const ScenarioPlacement& p : sc.placements) {
        int defIndex = -1;
        for (std::size_t k = 0; k < t.units->size(); ++k)
            if ((*t.units)[k].id == p.unitId) defIndex = static_cast<int>(k);
        if (defIndex < 0) {
            err = "unit Id '" + p.unitId + "' is in no loaded row";
            return false;
        }
        GameUnit u;
        u.id        = next.nextUnitId++;
        u.side      = p.side;
        u.defIndex  = defIndex;
        u.hex       = p.hex;
        u.placement = p.hex;          // the file's deployment hex; guided reads this
        u.hp        = (*t.units)[defIndex].hpMax;
        next.units.push_back(u);
        // The designation comes from the FILE. T-SCN-01 has already checked it is
        // exactly one Tank per side, so this copies rather than chooses.
        if (p.isFlag) next.flagUnit[p.side] = u.id;
    }

    // Objectives are every capturable tile the TABLE marks -- Data.h decides what is
    // capturable -- owned as the FILE says. A capturable hex the file does not name
    // is unowned (§2.7).
    for (int row = 0; row < sc.bounds.rows; ++row) {
        for (int col = 0; col < sc.bounds.cols; ++col) {
            const std::size_t i =
                static_cast<std::size_t>(row) * static_cast<std::size_t>(sc.bounds.cols) + col;
            const int ti = next.terrain[i];
            if (ti < 0 || !(*t.terrain)[ti].capturable) continue;
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
    // scenario field, so the file's value is what initSide gets (Q8).
    initSide(next.economy, 0, sc.startingFame[0]);
    initSide(next.economy, 1, sc.startingFame[1]);

    // Row 5 owns the match. The first turn is opened the same way every later one
    // is, so what this returns is a state a real match reaches.
    if (!initMatch(next.turn, firstSide, sc.turnCap, err)) return false;
    openTurn(next, t);

    g = next;
    return true;
}

// ---------------------------------------------------------------------------
// The replayer.
// ---------------------------------------------------------------------------
ReplayResult applyCommand(GameState& g, const SaveCommand& c, const RulesTables& t) {
    if (t.units == nullptr || t.terrain == nullptr)
        return refuse("no data tables supplied");

    // The turn and side a log entry is tagged with must be the turn and side the
    // rules say is live. A log that disagrees is a log from another match.
    if (g.turn.running) {
        if (c.turn != g.turn.turnNumber)
            return refuse("entry is tagged turn " + num(c.turn) + " but turn " +
                          num(g.turn.turnNumber) + " is live");
        if (c.side != g.turn.activeSide)
            return refuse("entry is tagged side " + num(c.side) + " but side " +
                          num(g.turn.activeSide) + " is to move");
    }

    switch (c.kind) {
    case SaveCommandKind::Move: {
        const GameUnit* u = findGameUnit(g, c.unitId);
        if (u == nullptr) return refuse("no unit " + num(c.unitId));
        if (u->side != c.side) return refuse("unit " + num(c.unitId) + " is not side " +
                                             num(c.side) + "'s to move");
        if (!inBounds(c.hex, g.bounds)) return refuse("destination is out of bounds");
        const Board b = boardOf(g);
        std::vector<Hex> route;
        int cost = 0;
        // Row 3 decides reachability and the route. Asked BEFORE anything mutates.
        if (!findPath(b, *t.terrain, u->hex, c.hex, (*t.units)[u->defIndex].move,
                      route, cost))
            return refuse("destination is not reachable within " +
                          num((*t.units)[u->defIndex].move) + " movement");
        // Row 5 decides whether this unit may still move, and it is the MOVE flag a
        // move spends -- not the act (T-TURN-01).
        if (g.turn.running) {
            std::string e;
            if (!markMoved(g.turn, c.unitId, u->side, e)) return refuse(e);
        }
        mutableUnit(g, c.unitId)->hex = c.hex;
        return okResult(1);
    }

    case SaveCommandKind::Attack: {
        const GameUnit* a = findGameUnit(g, c.unitId);
        if (a == nullptr) return refuse("no unit " + num(c.unitId));
        if (a->side != c.side) return refuse("unit " + num(c.unitId) + " is not side " +
                                             num(c.side) + "'s to move");
        const Board b = boardOf(g);
        const int defId = b.occupantAt(c.hex);
        if (defId == OCCUPANT_NONE) return refuse("no unit stands on the target hex");
        const GameUnit* d = findGameUnit(g, defId);
        if (d == nullptr) return refuse("no unit " + num(defId));
        if (d->side == a->side) return refuse("units " + num(a->id) + " and " +
                                              num(d->id) + " are on the same side");
        const int distance = hexDistance(a->hex, d->hex);     // Hex.h decides distance
        const UnitDef& ad = (*t.units)[a->defIndex];
        if (distance < ad.rangeMin || distance > ad.rangeMax)
            return refuse("distance " + num(distance) + " is outside " + ad.id +
                          "'s range " + num(ad.rangeMin) + "-" + num(ad.rangeMax));

        const Unit au = combatUnit(g, t, *a);
        const Unit du = combatUnit(g, t, *d);
        // Combat.h decides damage. Computed in full before any HP changes, so a
        // refusal below leaves the state untouched.
        const int damage = resolveDamage(au, du, terrainDefPctAt(g, t, d->hex));
        const int defHpAfter = d->hp - damage;
        const bool defenderDies = defHpAfter <= 0;

        if (g.turn.running) {
            std::string e;
            if (!markActed(g.turn, c.unitId, a->side, e)) return refuse(e);
        }

        // EVERY scalar this block needs is captured BEFORE anything erases, because
        // erasing from `units` invalidates `a` and `d`. Reading a stat off a
        // destroyed unit's pointer is the kind of defect that passes every
        // happy-path test and diverges only when a kill lands.
        const int  atkId = a->id, atkSide = a->side;
        const int  atkDefIndex = a->defIndex, atkHp = a->hp;
        const int  defId2 = d->id, defSide = d->side, defDefIndex = d->defIndex;
        const bool victimIsFlag   = isFlagUnit(g, *d);
        const bool attackerIsFlag = isFlagUnit(g, *a);
        if (defenderDies) {
            const int deadId = defId2;
            g.units.erase(std::remove_if(g.units.begin(), g.units.end(),
                          [deadId](const GameUnit& u) { return u.id == deadId; }),
                          g.units.end());
            // Row 4 decides the amount.
            awardKill(g.economy, atkSide, (*t.units)[defDefIndex], victimIsFlag);
            // Row 5 decides whether a downed flag ends the match, and when.
            if (g.turn.running) checkImmediate(g.turn, snapshotOf(g, t));
            return okResult(1);
        }
        const int atkHex_defPct = terrainDefPctAt(g, t, a->hex);
        mutableUnit(g, defId)->hp = defHpAfter;
        Unit duAfter = du;
        duAfter.hp = defHpAfter;                    // a wounded defender counters weaker
        if (defenderCanCounter(duAfter, distance)) {  // Combat.h decides eligibility
            const int counter = resolveDamage(duAfter, au, atkHex_defPct);
            if (atkHp - counter <= 0) {
                g.units.erase(std::remove_if(g.units.begin(), g.units.end(),
                              [atkId](const GameUnit& u) { return u.id == atkId; }),
                              g.units.end());
                awardKill(g.economy, defSide, (*t.units)[atkDefIndex], attackerIsFlag);
                if (g.turn.running) checkImmediate(g.turn, snapshotOf(g, t));
            } else {
                mutableUnit(g, atkId)->hp = atkHp - counter;
            }
        }
        return okResult(1);
    }

    case SaveCommandKind::Build: {
        // §4.9 tags Build `{factoryHex, unitId}`: the hex is the factory and the unit
        // is the TYPE built, which is Save.h's own reading of the field.
        if (c.unitId < 0 || c.unitId >= static_cast<int>(t.units->size()))
            return refuse("no unit type at index " + num(c.unitId));
        // Row 5, T-TURN-10: one build per factory per turn, asked as a PREDICATE
        // before anything mutates, because Fame commits at queue time (Q8(c)) and is
        // not refundable.
        if (g.turn.running && !canBuildAt(g.turn, c.hex, c.side)) {
            std::string why;
            markBuilt(g.turn, c.hex, c.side, why);   // refuses and changes nothing
            return refuse(why);
        }
        std::string e;
        if (!queueBuild(g.economy, *t.units, *t.terrain, c.side, c.hex, c.unitId, e))
            return refuse(e);
        // Spent only once the build actually queued: a build refused as unaffordable
        // leaves the factory free to try again this turn.
        if (g.turn.running) { std::string me; markBuilt(g.turn, c.hex, c.side, me); }
        std::vector<Hex> occupied;
        for (const GameUnit& u : g.units) occupied.push_back(u.hex);
        const std::vector<SpawnResult> spawns = resolveBuilds(g.economy, g.bounds, occupied);
        for (const SpawnResult& sp : spawns) {
            if (!sp.spawned) continue;              // boxed in: waits, holds the slot
            GameUnit u;
            u.id = g.nextUnitId++;
            u.side = sp.side; u.defIndex = sp.defIndex; u.hex = sp.hex;
            u.placement = sp.hex;                   // spawned, not deployed
            u.hp = (*t.units)[sp.defIndex].hpMax;
            g.units.push_back(u);
        }
        return okResult(1);
    }

    case SaveCommandKind::Capture: {
        std::vector<CaptureOccupant> occ;
        for (const GameUnit& u : g.units) {
            CaptureOccupant o;
            o.hex = u.hex; o.unitId = u.id; o.side = u.side;
            o.canCapture = (*t.units)[u.defIndex].canCapture;
            occ.push_back(o);
        }
        captureTick(g.economy, occ, c.side);        // Row 4 decides what flips
        return okResult(1);
    }

    case SaveCommandKind::EndTurn: {
        if (!g.turn.running) return refuse("no match is running");
        const MatchResult r = endTurn(g.turn, snapshotOf(g, t));  // Row 5 owns it
        // The next side's turn opens here. A replay that ends a turn without opening
        // the next one never accrues income, never repairs and never ticks a capture
        // after turn 1 -- it diverges from the match it replays at the first boundary.
        if (r.tier == ResultTier::InProgress) openTurn(g, t);
        return okResult(1);
    }
    }
    return refuse("unknown command kind");
}

ReplayResult replayLog(GameState& g, const std::vector<SaveCommand>& log,
                       const RulesTables& t) {
    // ALL OR NOTHING (T-SAVE-05). Every command is applied to a COPY, and the
    // caller's state is assigned only after the last one succeeds -- so a log that
    // fails at index k leaves `g` byte-identical to what it was.
    GameState work = g;
    for (std::size_t i = 0; i < log.size(); ++i) {
        ReplayResult r = applyCommand(work, log[i], t);
        if (!r.ok) {
            r.failedIndex = static_cast<int>(i);
            r.applied = 0;
            return r;
        }
    }
    g = work;
    return okResult(static_cast<int>(log.size()));
}

} // namespace strat
