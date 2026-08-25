// Stratocracy — UI binding contract, pass 2 (GDD §4.7 Stub 8, §4.11 row 8).
//
// Every value below is READ from the module that owns it. Nothing here computes a
// rule: the snapshot projects, `uiReachable` and `uiForecast` delegate, and
// `uiBuildOptions` assembles an ANSWER out of gates it reads from Turn.h and
// Economy.h without owning one of them. See spec/ui_spec.md.
#include "Ui.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace strat {

// ---------------------------------------------------------------------------
// lookups
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// §2.8's result, whole — the projection `UiMatchView` loses
//
// FOUR ASSIGNMENTS AND NO ARITHMETIC. If this function ever computes anything it is
// wrong: `Turn.h` owns every one of these values and `endTurn`/`checkImmediate`/
// `beginTurn` are the only things that decide them. The one judgement here is the
// null guard, and it defers rather than inventing an answer.
// ---------------------------------------------------------------------------
UiMatchResult uiMatchResult(const UiWorld& w) {
    UiMatchResult out;
    if (w.turn == nullptr) return out;   // no turn state: no match to report on
    const MatchResult& r = w.turn->result;
    out.tier         = r.tier;
    out.cause        = r.cause;
    out.winner       = r.winner;
    out.decidedByKey = r.decidedByKey;
    return out;
}

const UiUnit* findUiUnit(const UiWorld& w, int unitId) {
    for (const UiUnit& u : w.units)
        if (u.id == unitId) return &u;
    return nullptr;
}

const UiUnitView* findUiUnitView(const UiSnapshot& s, int unitId) {
    for (const UiUnitView& v : s.units)
        if (v.id == unitId) return &v;
    return nullptr;
}

namespace {

// Progress is TILE-held and names the unit that accumulated it (Q4, T-FAME-05), so
// this reads the tile's record and reports it under the one unit that can carry it.
// It is derived on every projection and never stored a second time.
int progressForUnit(const EconomyState& e, int unitId) {
    for (const CaptureProgress& c : e.captures)
        if (c.unitId == unitId) return c.turnsHeld;
    return 0;
}

int terrainDefPctAt(const UiWorld& w, const Hex& h) {
    const int ti = w.board.terrainAt(h);
    if (ti < 0 || w.terrain == nullptr || ti >= static_cast<int>(w.terrain->size())) return 0;
    return (*w.terrain)[static_cast<std::size_t>(ti)].defensePct;
}

// `isGuidedMarked`'s derivation, for the PROJECTION only. T-UI-05 clause (b)
// deliberately does NOT call this: it writes the same rule out a second time, so that
// a mistake here fails the invariant instead of being reproduced by it.
//
// The seat is side AND deployment hex: `guidedOpening.infantry` names a hex, and
// T-SCN-02 forbids two placements sharing one, so the pair identifies one unit.
bool guidedMarkedFor(const UiWorld& w, const UiUnit& u) {
    if (w.guided == nullptr) return false;
    for (const ScenarioGuided& g : *w.guided)
        if (g.side == u.side && hexEqual(g.infantry, u.placement)) return true;
    return false;
}

// Is a factory hex a spawn point? §2.7's income and spawn rules both key off the
// terrain table's flag rather than off a name.
bool isFactoryObjective(const UiWorld& w, const Objective& o) {
    if (w.terrain == nullptr) return false;
    if (o.terrainIndex < 0 || static_cast<std::size_t>(o.terrainIndex) >= w.terrain->size())
        return false;
    return (*w.terrain)[static_cast<std::size_t>(o.terrainIndex)].isSpawnPoint;
}

} // namespace

// ---------------------------------------------------------------------------
// derivations the PROJECTION uses. T-UI-05 restates each of them inline rather than
// calling these, on purpose -- see the clause (b) note in Ui.h. They are exposed
// because the driver and the §4.9 bridge need them too, not because the gate does.
// ---------------------------------------------------------------------------

// §2.7's rate over the side's held factories (+100) and towns (+25), read from the
// table's `incomeFame`. This is `accrueIncome`'s loop WITHOUT its turn-1 guard and
// WITHOUT its mutation: the guard is Q8(a)'s accrual rule and belongs to what a turn
// PAYS, while this field is the rate the holdings CARRY. On turn 1 they differ -- the
// side is paid 0 and this reads the standing rate, which is what the stub requires.
int standingIncomeRate(const EconomyState& e, const std::vector<TerrainDef>& terrain,
                       int side) {
    int rate = 0;
    for (const Objective& o : e.objectives) {
        if (o.owner != side) continue;
        if (o.terrainIndex < 0 || static_cast<std::size_t>(o.terrainIndex) >= terrain.size())
            continue;
        rate += terrain[static_cast<std::size_t>(o.terrainIndex)].incomeFame;
    }
    return rate;
}

// §2.7's spawn rule as a predicate, mirroring Economy.h::resolveBuilds EXACTLY: the
// factory hex if free, else any free in-bounds neighbour, else the build waits.
//
// OCCUPANCY ONLY, and deliberately so. `resolveBuilds` never consults terrain -- it
// asks `isOccupied` and nothing else -- so a passability filter here would report a
// factory blocked at a hex the shipped spawner would happily place on, and the screen
// would contradict the simulation about the one thing this row exists to prevent.
// §4.10's parenthetical calls `spawnBlocked` a function of unit positions AND terrain;
// that reading is not the shipped rule, and it is filed rather than implemented.
bool spawnHexesBlocked(const UiWorld& w, const Hex& factoryHex) {
    if (w.board.occupantAt(factoryHex) == OCCUPANT_NONE) return false;
    Hex adj[HEX_DIRECTIONS];
    const int n = neighbors(factoryHex, w.board.bounds, adj);
    for (int i = 0; i < n; ++i)
        if (w.board.occupantAt(adj[i]) == OCCUPANT_NONE) return false;
    return true;
}

// ---------------------------------------------------------------------------
// the projection
// ---------------------------------------------------------------------------
UiSnapshot buildUiSnapshot(const UiWorld& w) {
    UiSnapshot s;
    if (w.economy == nullptr || w.turn == nullptr) return s;
    const EconomyState& e = *w.economy;
    const TurnState&    t = *w.turn;

    // per-hex, canonical order (ascending r, then ascending q) so two runs on the
    // same state produce the same bytes.
    std::vector<Hex> all;
    all.reserve(static_cast<std::size_t>(w.board.bounds.cols) *
                static_cast<std::size_t>(w.board.bounds.rows));
    for (int row = 0; row < w.board.bounds.rows; ++row)
        for (int col = 0; col < w.board.bounds.cols; ++col)
            all.push_back(offsetToAxial(col, row));
    sortCanonical(all);

    s.hexes.reserve(all.size());
    for (const Hex& h : all) {
        UiHexView v;
        v.hex       = h;
        v.terrainId = w.board.terrainAt(h);
        // Ownership belongs to the economy's objective list. A hex that is not an
        // objective is OWNER_NEUTRAL here because it is owned by nobody, not because
        // the field is unknown.
        const Objective* o = findObjective(e, h);
        v.owner = (o != nullptr) ? o->owner : OWNER_NEUTRAL;
        s.hexes.push_back(v);
    }

    // per-unit, ascending id.
    std::vector<const UiUnit*> ordered;
    ordered.reserve(w.units.size());
    for (const UiUnit& u : w.units) ordered.push_back(&u);
    std::sort(ordered.begin(), ordered.end(),
              [](const UiUnit* a, const UiUnit* b) { return a->id < b->id; });

    s.units.reserve(ordered.size());
    for (const UiUnit* u : ordered) {
        UiUnitView v;
        v.id     = u->id;
        v.side   = u->side;
        v.unitId = u->defIndex;
        v.hex    = u->hex;
        v.hp     = u->unit.hp;
        v.hpMax  = u->unit.hpMax;
        v.isFlag = u->isFlag;
        // The two flags come from Turn.h's two SEPARATE sets. Reading either from
        // the other is the pass-1 reading of a one-flag snapshot and is exactly what
        // T-TURN-01 refuses.
        v.hasMoved = hasMoved(t, u->id);
        v.hasActed = hasActed(t, u->id);
        v.captureProgress = progressForUnit(e, u->id);
        // Read off the PLACEMENT, never off `hex`. Beat 1a's whole content is that
        // the marked Infantry moves, so a derivation keyed on the current hex would
        // unmark it at exactly the moment the guidance layer still needs the mark.
        v.isGuidedMarked = guidedMarkedFor(w, *u);
        s.units.push_back(v);
    }

    // per-factory, canonical hex order. The factories are the objectives the terrain
    // table flags as spawn points -- the same test §2.7's income and spawn rules use.
    {
        std::vector<const Objective*> facs;
        for (const Objective& o : e.objectives)
            if (isFactoryObjective(w, o)) facs.push_back(&o);
        std::sort(facs.begin(), facs.end(),
                  [](const Objective* a, const Objective* b) { return hexLess(a->hex, b->hex); });

        s.factories.reserve(facs.size());
        for (const Objective* o : facs) {
            UiFactoryView v;
            v.hex   = o->hex;
            v.owner = o->owner;
            // T-TURN-10's allowance, read from the module that owns it. The slot and
            // the allowance are two rules, not one, so the two fields below are read
            // from two places and never from each other.
            v.hasBuiltThisTurn = hasBuiltThisTurn(t, o->hex);
            v.buildWaiting     = false;
            for (const PendingBuild& p : e.pending)
                if (hexEqual(p.factoryHex, o->hex)) { v.buildWaiting = true; break; }
            v.spawnBlocked = spawnHexesBlocked(w, o->hex);
            s.factories.push_back(v);
        }
    }

    // per-side.
    for (int i = 0; i < SIDE_COUNT; ++i) {
        s.side[i].fameTotal  = e.side[i].fameTotal;
        s.side[i].fameCombat = e.side[i].fameCombat;
        // OWNERSHIP ONLY. A capture in progress contributes nothing until the
        // objective flips (Q14, ruled; T-CAP-05 / GATE-CAP-PARTIAL): partial credit
        // would need a fractional-count rule and would invert the invariant.
        int held = 0;
        for (const Objective& o : e.objectives)
            if (o.owner == i) ++held;
        s.side[i].objectivesHeld = held;

        int hp = 0;
        for (const UiUnit& u : w.units)
            if (u.side == i) hp += u.unit.hp;
        s.side[i].survivingHp = hp;

        // The STANDING rate, not this turn's accrual. `accrueIncome` would return 0
        // here on turn 1 and would move the purse besides; neither is what the field
        // means (Q8(a) pays 0 on turn 1, and the holdings still carry their rate).
        s.side[i].incomePerTurn =
            (w.terrain != nullptr) ? standingIncomeRate(e, *w.terrain, i) : 0;
    }
    s.objectiveTotal = static_cast<int>(e.objectives.size());

    s.match.turn       = t.turnNumber;
    s.match.turnCap    = t.turnCap;
    s.match.sideToMove = t.activeSide;
    s.match.resultTier = t.result.tier;
    s.match.hasResult  = (t.result.tier != ResultTier::InProgress);
    return s;
}

// ---------------------------------------------------------------------------
// T-UI-02 — the reachable-hex highlight
// ---------------------------------------------------------------------------
std::vector<ReachEntry> uiReachable(const UiWorld& w, int unitId) {
    std::vector<ReachEntry> out;
    const UiUnit* u = findUiUnit(w, unitId);
    if (u == nullptr || w.unitDefs == nullptr || w.terrain == nullptr) return out;
    if (u->defIndex < 0 || u->defIndex >= static_cast<int>(w.unitDefs->size())) return out;
    // Move.h decides reachability. This function chooses nothing -- not the cost
    // model, not the blocking rule, not the order -- which is the whole content of
    // "the UI queries the module and never recomputes movement" (§2.5).
    const UnitDef& def = (*w.unitDefs)[static_cast<std::size_t>(u->defIndex)];
    return reachable(w.board, *w.terrain, u->hex, def.move);
}

// ---------------------------------------------------------------------------
// T-UI-01 — forecast = resolution
// ---------------------------------------------------------------------------
UiForecast uiForecast(const UiWorld& w, int attackerId, const Hex& defenderHex) {
    UiForecast f;
    const UiUnit* a = findUiUnit(w, attackerId);
    if (a == nullptr) { f.reason = "no such unit"; return f; }
    if (w.unitDefs == nullptr || w.terrain == nullptr) { f.reason = "no tables"; return f; }

    const UiUnit* d = nullptr;
    for (const UiUnit& o : w.units)
        if (hexEqual(o.hex, defenderHex)) { d = &o; break; }
    if (d == nullptr) { f.reason = "no unit on that hex"; return f; }
    if (d->id == a->id)     { f.reason = "a unit cannot attack itself"; return f; }
    if (d->side == a->side) { f.reason = "same side"; return f; }

    f.distance = hexDistance(a->hex, d->hex);          // Hex.h decides distance
    const UnitDef& ad = (*w.unitDefs)[static_cast<std::size_t>(a->defIndex)];
    if (f.distance < ad.rangeMin || f.distance > ad.rangeMax) {
        f.reason = "out of range";
        return f;
    }

    // Combat.h decides damage -- resolveDamage and defenderCanCounter, verified at
    // 5ffa8d6, and nothing else. No local formula stands beside them.
    f.damage = resolveDamage(a->unit, d->unit, terrainDefPctAt(w, d->hex));
    const int defHpAfter = d->unit.hp - f.damage;
    f.defenderDies = (defHpAfter <= 0);

    if (!f.defenderDies) {
        Unit wounded = d->unit;
        wounded.hp = defHpAfter;                        // a wounded defender counters weaker
        if (defenderCanCounter(wounded, f.distance)) {  // Combat.h decides eligibility
            f.counterFires  = true;
            f.counterDamage = resolveDamage(wounded, a->unit, terrainDefPctAt(w, a->hex));
        }
    }
    f.legal = true;
    return f;
}

// Applies exactly what the forecast above reported and adds no arithmetic, so the
// gate can measure "identical numbers" at the resolution end too.
UiResolution uiResolveForGate(const UiWorld& w, int attackerId, const Hex& defenderHex) {
    UiResolution r;
    r.forecast = uiForecast(w, attackerId, defenderHex);
    if (!r.forecast.legal) return r;

    const UiUnit* a = findUiUnit(w, attackerId);
    const UiUnit* d = nullptr;
    for (const UiUnit& o : w.units)
        if (hexEqual(o.hex, defenderHex)) { d = &o; break; }
    if (a == nullptr || d == nullptr) return r;

    r.defenderHpAfter = d->unit.hp - r.forecast.damage;
    if (r.defenderHpAfter < 0) r.defenderHpAfter = 0;
    r.attackerHpAfter = a->unit.hp - (r.forecast.counterFires ? r.forecast.counterDamage : 0);
    if (r.attackerHpAfter < 0) r.attackerHpAfter = 0;
    r.applied = true;
    return r;
}

// ---------------------------------------------------------------------------
// T-UI-04 — the production menu's buildlist (§2.11.5)
// ---------------------------------------------------------------------------
std::vector<UiBuildOption> uiBuildOptions(const UiWorld& w, int side,
                                          const Hex& factoryHex) {
    std::vector<UiBuildOption> out;
    if (w.unitDefs == nullptr) return out;   // no table: no rows exist to enumerate

    // AVAILABILITY IS COMPUTED ONCE, ABOVE THE LOOP, and that is not an optimisation.
    // Every gate below is a property of the FACTORY and the SIDE, so a per-row
    // computation could only ever differ by leaking a per-TYPE rule -- which is
    // precisely the AI's population cap that ruling (c) keeps out of this query.
    std::string why;                          // empty == available
    if (side < 0 || side >= SIDE_COUNT) {
        why = "invalid side";
    } else if (w.economy == nullptr || w.turn == nullptr || w.terrain == nullptr) {
        why = "no tables";
    } else {
        const EconomyState& e = *w.economy;
        const TurnState&    t = *w.turn;
        const Objective*    o = findObjective(e, factoryHex);

        // T-TURN-10 FIRST, because Turn.h says in as many words that a caller must
        // consult it BEFORE it charges anything. `canBuildAt` DECIDES; the ladder
        // inside only WORDS what it already decided, in `markBuilt`'s own words for
        // the same refusals. If a branch here were wrong the VERDICT would still be
        // right and only the sentence would differ -- which is the point of asking
        // the module for the answer and the module's neighbour for the phrasing.
        if (!canBuildAt(t, factoryHex, side)) {
            if (!t.running)                         why = "no match is running";
            else if (t.phase == Phase::MatchOver)   why = "the match is over";
            else if (t.phase == Phase::TurnPending) why = "the turn has not begun";
            else if (t.phase == Phase::StartOfTurn)
                why = "start-of-turn repair has not been applied yet";
            else if (side != t.activeSide)
                why = "side " + std::to_string(side) + " is not the active side";
            // Nothing else can have refused it: the allowance is the last thing
            // `canBuildAt` asks about.
            else why = "that factory has already taken its build this turn";
        }
        // Then `queueBuild`'s own ladder, in ITS order and ITS words, so an option
        // this query calls available is one `queueBuild` refuses for no reason other
        // than cost -- and cost is `affordable`, which is a different field.
        else if (o == nullptr)     { why = "no objective at that hex"; }
        else if (o->owner != side) { why = "factory is not held by this side"; }
        else if (o->terrainIndex < 0 ||
                 static_cast<std::size_t>(o->terrainIndex) >= w.terrain->size()) {
            why = "objective has no terrain row";
        } else if (!(*w.terrain)[static_cast<std::size_t>(o->terrainIndex)].isSpawnPoint) {
            why = "not a build point";
        } else {
            for (const PendingBuild& p : e.pending)
                if (hexEqual(p.factoryHex, factoryHex)) {
                    why = "factory already has a pending build";
                    break;
                }
        }
        // `spawnHexesBlocked` is DELIBERATELY NOT CONSULTED (Q31, ruled 2026-08-22).
        // A boxed-in factory still takes the build; `buildWaiting` holds it.
    }

    const int purse = (w.economy != nullptr && side >= 0 && side < SIDE_COUNT)
                          ? w.economy->side[side].fameTotal
                          : 0;

    out.reserve(w.unitDefs->size());
    for (std::size_t i = 0; i < w.unitDefs->size(); ++i) {
        const UnitDef& d = (*w.unitDefs)[i];
        UiBuildOption op;
        op.defIndex   = static_cast<int>(i);
        op.id         = d.id;
        op.costFame   = d.costFame;
        op.affordable = (d.costFame <= purse);
        op.available  = why.empty();
        op.reason     = why;
        out.push_back(op);
    }
    return out;
}

// ---------------------------------------------------------------------------
// T-UI-05 — the field contract
//
// §4.7 Stub 8's field list, transcribed row for row. This table is the document's
// claim about the snapshot; the enumeration below is the snapshot's actual surface;
// clause (c) is the assertion that they are the same set. A field added to the struct
// and to the enumeration but not to this table has no stated kind and fails (c) --
// which is the whole mechanism that "stops a field entering the snapshot without a
// contract".
// ---------------------------------------------------------------------------
const std::vector<UiFieldContractEntry>& uiFieldContract() {
    static const std::vector<UiFieldContractEntry> kContract = {
        // per-hex (2)
        {"per-hex", "terrainId", UiFieldKind::Mirror, "Board::terrainAt, the §4.8 terrain row at that hex"},
        {"per-hex", "owner",     UiFieldKind::Mirror, "EconomyState objective owner at that hex, OWNER_NEUTRAL where none"},
        // per-unit (11)
        {"per-unit", "id",       UiFieldKind::Mirror, "UiUnit::id"},
        {"per-unit", "side",     UiFieldKind::Mirror, "UiUnit::side"},
        {"per-unit", "unitId",   UiFieldKind::Mirror, "UiUnit::defIndex, the §2.4 row"},
        {"per-unit", "hex",      UiFieldKind::Mirror, "UiUnit::hex, the unit's current hex"},
        {"per-unit", "hp",       UiFieldKind::Mirror, "Unit::hp"},
        {"per-unit", "hpMax",    UiFieldKind::Mirror, "Unit::hpMax, from data/units.csv"},
        {"per-unit", "isFlag",   UiFieldKind::Mirror, "UiUnit::isFlag, the Stub-7 placement field"},
        {"per-unit", "hasMoved", UiFieldKind::Mirror, "Turn.h::hasMoved -- T-TURN-01's first flag"},
        {"per-unit", "hasActed", UiFieldKind::Mirror, "Turn.h::hasActed -- T-TURN-01's second flag"},
        {"per-unit", "captureProgress", UiFieldKind::Mirror,
         "CaptureProgress::turnsHeld of the tile record naming this unit, 0 where none"},
        {"per-unit", "isGuidedMarked", UiFieldKind::DeclaredDerived,
         "true exactly when the scenario's guidedOpening entry for this unit's side "
         "names this unit's PLACEMENT hex; false otherwise"},
        // per-factory (5)
        {"per-factory", "hex",   UiFieldKind::Mirror, "Objective::hex, the scenario's factory placement"},
        {"per-factory", "owner", UiFieldKind::Mirror, "Objective::owner"},
        {"per-factory", "hasBuiltThisTurn", UiFieldKind::Mirror, "Turn.h::hasBuiltThisTurn -- T-TURN-10's allowance"},
        {"per-factory", "buildWaiting",     UiFieldKind::Mirror, "a PendingBuild naming this factory hex (T-FAME-04's slot)"},
        {"per-factory", "spawnBlocked",     UiFieldKind::DeclaredDerived,
         "true exactly when the factory hex and every in-bounds neighbour are "
         "occupied -- §2.7's spawn rule, occupancy only, as resolveBuilds applies it"},
        // per-side (5)
        {"per-side", "fameTotal",  UiFieldKind::Mirror, "SideEconomy::fameTotal"},
        {"per-side", "fameCombat", UiFieldKind::Mirror, "SideEconomy::fameCombat"},
        {"per-side", "objectivesHeld X of N", UiFieldKind::DeclaredDerived,
         "§2.8 criterion 2: X is the objectives this side OWNS, N is every objective "
         "the scenario supplies; a capture in progress counts for nobody"},
        {"per-side", "survivingHP", UiFieldKind::DeclaredDerived,
         "§2.8 criterion 3: the sum of hp over this side's units in the per-unit group"},
        {"per-side", "incomePerTurn", UiFieldKind::DeclaredDerived,
         "§2.7's rate over this side's held factories and towns, from the terrain "
         "table's incomeFame; the STANDING rate, not turn 1's 0 accrual"},
        // match (4)
        {"match", "turn",       UiFieldKind::Mirror, "TurnState::turnNumber"},
        {"match", "turnCap",    UiFieldKind::Mirror, "TurnState::turnCap"},
        {"match", "sideToMove", UiFieldKind::Mirror, "TurnState::activeSide"},
        {"match", "resultTier or null", UiFieldKind::Mirror,
         "TurnState::result.tier; InProgress IS the null, no numeric stand-in"},
    };
    return kContract;
}

// ---------------------------------------------------------------------------
// T-UI-05 — the snapshot's observable surface
//
// Emitted at the STUB'S granularity: `hex` travels as {q, r} and stays one field,
// because splitting it into two would assert a contract the document does not state.
// ---------------------------------------------------------------------------
std::vector<UiFieldValue> uiEnumerateSnapshot(const UiSnapshot& s) {
    std::vector<UiFieldValue> out;
    auto emit = [&out](const char* g, int i, const char* f, std::vector<long long> v) {
        UiFieldValue fv; fv.group = g; fv.index = i; fv.field = f; fv.scalars = std::move(v);
        out.push_back(std::move(fv));
    };

    for (std::size_t i = 0; i < s.hexes.size(); ++i) {
        const UiHexView& h = s.hexes[i];
        const int idx = static_cast<int>(i);
        emit("per-hex", idx, "terrainId", {h.terrainId});
        emit("per-hex", idx, "owner",     {h.owner});
    }
    for (std::size_t i = 0; i < s.units.size(); ++i) {
        const UiUnitView& u = s.units[i];
        const int idx = static_cast<int>(i);
        emit("per-unit", idx, "id",       {u.id});
        emit("per-unit", idx, "side",     {u.side});
        emit("per-unit", idx, "unitId",   {u.unitId});
        emit("per-unit", idx, "hex",      {u.hex.q, u.hex.r});
        emit("per-unit", idx, "hp",       {u.hp});
        emit("per-unit", idx, "hpMax",    {u.hpMax});
        emit("per-unit", idx, "isFlag",   {u.isFlag ? 1 : 0});
        emit("per-unit", idx, "hasMoved", {u.hasMoved ? 1 : 0});
        emit("per-unit", idx, "hasActed", {u.hasActed ? 1 : 0});
        emit("per-unit", idx, "captureProgress", {u.captureProgress});
        emit("per-unit", idx, "isGuidedMarked",  {u.isGuidedMarked ? 1 : 0});
    }
    for (std::size_t i = 0; i < s.factories.size(); ++i) {
        const UiFactoryView& f = s.factories[i];
        const int idx = static_cast<int>(i);
        emit("per-factory", idx, "hex",   {f.hex.q, f.hex.r});
        emit("per-factory", idx, "owner", {f.owner});
        emit("per-factory", idx, "hasBuiltThisTurn", {f.hasBuiltThisTurn ? 1 : 0});
        emit("per-factory", idx, "buildWaiting",     {f.buildWaiting ? 1 : 0});
        emit("per-factory", idx, "spawnBlocked",     {f.spawnBlocked ? 1 : 0});
    }
    for (int i = 0; i < SIDE_COUNT; ++i) {
        emit("per-side", i, "fameTotal",  {s.side[i].fameTotal});
        emit("per-side", i, "fameCombat", {s.side[i].fameCombat});
        // X and N together: the stub names ONE field, "objectivesHeld X of N".
        emit("per-side", i, "objectivesHeld X of N", {s.side[i].objectivesHeld, s.objectiveTotal});
        emit("per-side", i, "survivingHP",   {s.side[i].survivingHp});
        emit("per-side", i, "incomePerTurn", {s.side[i].incomePerTurn});
    }
    emit("match", -1, "turn",       {s.match.turn});
    emit("match", -1, "turnCap",    {s.match.turnCap});
    emit("match", -1, "sideToMove", {s.match.sideToMove});
    emit("match", -1, "resultTier or null",
         {s.match.hasResult ? 1 : 0, static_cast<long long>(s.match.resultTier)});
    return out;
}

// ---------------------------------------------------------------------------
// T-UI-05 — the check
//
// Every expected value here is recomputed FROM THE MODULE, never read back out of the
// snapshot and never obtained by calling buildUiSnapshot. That is what lets this
// disagree with the projection.
// ---------------------------------------------------------------------------
namespace {

std::string fieldPath(const char* group, int index, const char* field) {
    std::string p = group;
    if (index >= 0) { p += "["; p += std::to_string(index); p += "]"; }
    p += "."; p += field;
    return p;
}

} // namespace

UiFidelityResult uiCheckSnapshotFidelity(const UiWorld& w, const UiSnapshot& s) {
    UiFidelityResult r;
    auto fail = [&r](const char* clause, const std::string& field, const std::string& detail) {
        UiFidelityFailure f; f.clause = clause; f.field = field; f.detail = detail;
        r.failures.push_back(f);
    };
    auto mirror = [&](const std::string& path, long long got, long long want) {
        ++r.mirrorsChecked;
        if (got != want)
            fail("a", path, "mirror is " + std::to_string(got) + ", module holds " +
                            std::to_string(want));
    };
    auto derived = [&](const std::string& path, long long got, long long want) {
        ++r.derivedChecked;
        if (got != want)
            fail("b", path, "snapshot states " + std::to_string(got) +
                            ", the stub's derivation gives " + std::to_string(want));
    };

    if (w.economy == nullptr || w.turn == nullptr) {
        fail("a", "world", "no economy or turn state to compare against");
        return r;
    }
    const EconomyState& e = *w.economy;
    const TurnState&    t = *w.turn;

    // --- (a)/(b) per-hex, and the ORDER, which "nothing reordered" makes part of the
    // invariant rather than an implementation detail.
    {
        std::vector<Hex> want;
        for (int row = 0; row < w.board.bounds.rows; ++row)
            for (int col = 0; col < w.board.bounds.cols; ++col)
                want.push_back(offsetToAxial(col, row));
        sortCanonical(want);
        if (s.hexes.size() != want.size()) {
            fail("a", "per-hex", "snapshot has " + std::to_string(s.hexes.size()) +
                                 " hexes, the board has " + std::to_string(want.size()));
        } else {
            for (std::size_t i = 0; i < want.size(); ++i) {
                const int idx = static_cast<int>(i);
                if (!hexEqual(s.hexes[i].hex, want[i])) {
                    fail("a", fieldPath("per-hex", idx, "hex"), "not in canonical order");
                    continue;
                }
                mirror(fieldPath("per-hex", idx, "terrainId"),
                       s.hexes[i].terrainId, w.board.terrainAt(want[i]));
                const Objective* o = findObjective(e, want[i]);
                mirror(fieldPath("per-hex", idx, "owner"),
                       s.hexes[i].owner, (o != nullptr) ? o->owner : OWNER_NEUTRAL);
            }
        }
    }

    // --- (a)/(b) per-unit
    {
        std::vector<const UiUnit*> want;
        for (const UiUnit& u : w.units) want.push_back(&u);
        std::sort(want.begin(), want.end(),
                  [](const UiUnit* a, const UiUnit* b) { return a->id < b->id; });
        if (s.units.size() != want.size()) {
            fail("a", "per-unit", "snapshot has " + std::to_string(s.units.size()) +
                                  " units, the world has " + std::to_string(want.size()));
        } else {
            for (std::size_t i = 0; i < want.size(); ++i) {
                const UiUnitView& v = s.units[i];
                const UiUnit&     u = *want[i];
                const int idx = static_cast<int>(i);
                mirror(fieldPath("per-unit", idx, "id"),     v.id,     u.id);
                mirror(fieldPath("per-unit", idx, "side"),   v.side,   u.side);
                mirror(fieldPath("per-unit", idx, "unitId"), v.unitId, u.defIndex);
                ++r.mirrorsChecked;
                if (!hexEqual(v.hex, u.hex))
                    fail("a", fieldPath("per-unit", idx, "hex"),
                         "snapshot hex is not the unit's current hex");
                mirror(fieldPath("per-unit", idx, "hp"),     v.hp,     u.unit.hp);
                mirror(fieldPath("per-unit", idx, "hpMax"),  v.hpMax,  u.unit.hpMax);
                mirror(fieldPath("per-unit", idx, "isFlag"), v.isFlag ? 1 : 0, u.isFlag ? 1 : 0);
                // The two flags are read SEPARATELY from Turn.h. Comparing one against
                // the other is the one-flag reading T-TURN-01 refuses.
                mirror(fieldPath("per-unit", idx, "hasMoved"),
                       v.hasMoved ? 1 : 0, hasMoved(t, u.id) ? 1 : 0);
                mirror(fieldPath("per-unit", idx, "hasActed"),
                       v.hasActed ? 1 : 0, hasActed(t, u.id) ? 1 : 0);
                long long wantProgress = 0;
                for (const CaptureProgress& c : e.captures)
                    if (c.unitId == u.id) { wantProgress = c.turnsHeld; break; }
                mirror(fieldPath("per-unit", idx, "captureProgress"),
                       v.captureProgress, wantProgress);
                // (b) RECOMPUTED HERE, INLINE, from the stub's words -- deliberately
                // NOT by calling the helper the projection used. A check that shares
                // the projection's helper reproduces a wrong derivation instead of
                // failing on it, which is the one thing clause (b) says it must not do.
                long long wantMarked = 0;
                if (w.guided != nullptr)
                    for (const ScenarioGuided& g : *w.guided)
                        if (g.side == u.side && hexEqual(g.infantry, u.placement)) { wantMarked = 1; break; }
                derived(fieldPath("per-unit", idx, "isGuidedMarked"),
                        v.isGuidedMarked ? 1 : 0, wantMarked);
            }
        }
    }

    // --- (a)/(b) per-factory
    {
        std::vector<const Objective*> want;
        for (const Objective& o : e.objectives)
            if (isFactoryObjective(w, o)) want.push_back(&o);
        std::sort(want.begin(), want.end(),
                  [](const Objective* a, const Objective* b) { return hexLess(a->hex, b->hex); });
        if (s.factories.size() != want.size()) {
            fail("a", "per-factory", "snapshot has " + std::to_string(s.factories.size()) +
                                     " factories, the scenario supplies " +
                                     std::to_string(want.size()));
        } else {
            for (std::size_t i = 0; i < want.size(); ++i) {
                const UiFactoryView& v = s.factories[i];
                const Objective&     o = *want[i];
                const int idx = static_cast<int>(i);
                ++r.mirrorsChecked;
                if (!hexEqual(v.hex, o.hex))
                    fail("a", fieldPath("per-factory", idx, "hex"),
                         "not the scenario's factory placement, or not in canonical order");
                mirror(fieldPath("per-factory", idx, "owner"), v.owner, o.owner);
                mirror(fieldPath("per-factory", idx, "hasBuiltThisTurn"),
                       v.hasBuiltThisTurn ? 1 : 0, hasBuiltThisTurn(t, o.hex) ? 1 : 0);
                long long wantWaiting = 0;
                for (const PendingBuild& p : e.pending)
                    if (hexEqual(p.factoryHex, o.hex)) { wantWaiting = 1; break; }
                mirror(fieldPath("per-factory", idx, "buildWaiting"),
                       v.buildWaiting ? 1 : 0, wantWaiting);
                // (b) §2.7's spawn rule recomputed INLINE, occupancy only, and again
                // not through the projection's helper.
                long long wantBlocked = 1;
                if (w.board.occupantAt(o.hex) == OCCUPANT_NONE) {
                    wantBlocked = 0;
                } else {
                    Hex adj[HEX_DIRECTIONS];
                    const int n = neighbors(o.hex, w.board.bounds, adj);
                    for (int k = 0; k < n; ++k)
                        if (w.board.occupantAt(adj[k]) == OCCUPANT_NONE) { wantBlocked = 0; break; }
                }
                derived(fieldPath("per-factory", idx, "spawnBlocked"),
                        v.spawnBlocked ? 1 : 0, wantBlocked);
            }
        }
    }

    // --- (a)/(b) per-side
    for (int i = 0; i < SIDE_COUNT; ++i) {
        mirror(fieldPath("per-side", i, "fameTotal"),  s.side[i].fameTotal,  e.side[i].fameTotal);
        mirror(fieldPath("per-side", i, "fameCombat"), s.side[i].fameCombat, e.side[i].fameCombat);

        // §2.8 criterion 2, OWNERSHIP ONLY -- a capture in progress counts for nobody.
        long long held = 0;
        for (const Objective& o : e.objectives) if (o.owner == i) ++held;
        const long long total = static_cast<long long>(e.objectives.size());
        ++r.derivedChecked;
        if (s.side[i].objectivesHeld != held || s.objectiveTotal != total)
            fail("b", fieldPath("per-side", i, "objectivesHeld X of N"),
                 "snapshot states " + std::to_string(s.side[i].objectivesHeld) + " of " +
                 std::to_string(s.objectiveTotal) + ", §2.8 criterion 2 gives " +
                 std::to_string(held) + " of " + std::to_string(total));

        long long hp = 0;
        for (const UiUnit& u : w.units) if (u.side == i) hp += u.unit.hp;
        derived(fieldPath("per-side", i, "survivingHP"), s.side[i].survivingHp, hp);

        // §2.7's rate, summed INLINE from the terrain table. Not `standingIncomeRate`
        // (the projection's helper) and not `accrueIncome` (which answers a different
        // question and pays 0 on turn 1).
        long long rate = 0;
        if (w.terrain != nullptr)
            for (const Objective& o : e.objectives) {
                if (o.owner != i) continue;
                if (o.terrainIndex < 0 ||
                    static_cast<std::size_t>(o.terrainIndex) >= w.terrain->size()) continue;
                rate += (*w.terrain)[static_cast<std::size_t>(o.terrainIndex)].incomeFame;
            }
        derived(fieldPath("per-side", i, "incomePerTurn"), s.side[i].incomePerTurn, rate);
    }

    // --- (a) match
    mirror("match.turn",       s.match.turn,       t.turnNumber);
    mirror("match.turnCap",    s.match.turnCap,    t.turnCap);
    mirror("match.sideToMove", s.match.sideToMove, t.activeSide);
    ++r.mirrorsChecked;
    if (s.match.resultTier != t.result.tier ||
        s.match.hasResult != (t.result.tier != ResultTier::InProgress))
        fail("a", "match.resultTier or null", "tier or its null flag disagrees with TurnState");

    // --- (c) NO OTHER KIND
    //
    // Both directions, because each catches a different defect: a field in the
    // snapshot with no contract row is one that entered without a contract, and a
    // contract row nothing emits is a field the document claims and the snapshot does
    // not carry. Neither is caught by (a) or (b), which only ever look at fields both
    // sides already agree exist.
    {
        const std::vector<UiFieldContractEntry>& contract = uiFieldContract();
        const std::vector<UiFieldValue> emitted = uiEnumerateSnapshot(s);
        r.fieldsEnumerated = static_cast<int>(emitted.size());

        auto entryFor = [&contract](const char* group, const char* field) -> const UiFieldContractEntry* {
            for (const UiFieldContractEntry& c : contract)
                if (std::string(c.group) == group && std::string(c.field) == field) return &c;
            return nullptr;
        };

        for (const UiFieldValue& v : emitted) {
            const UiFieldContractEntry* c = entryFor(v.group, v.field);
            if (c == nullptr) {
                fail("c", fieldPath(v.group, v.index, v.field),
                     "in the snapshot with no contract row: neither an unmarked mirror "
                     "of a named module-side value nor a marked field with a stated derivation");
                continue;
            }
            if (c->source == nullptr || *c->source == '\0')
                fail("c", fieldPath(v.group, v.index, v.field),
                     "contract row states no module-side value and no derivation");
        }

        for (const UiFieldContractEntry& c : contract) {
            bool seen = false;
            for (const UiFieldValue& v : emitted)
                if (std::string(v.group) == c.group && std::string(v.field) == c.field) { seen = true; break; }
            // A group the fixture leaves empty emits nothing and is not a defect --
            // a board with no factories has no per-factory row to carry. Only a group
            // that HAS elements and still omits the field is one.
            if (!seen) {
                bool groupHasElements = false;
                for (const UiFieldValue& v : emitted)
                    if (std::string(v.group) == c.group) { groupHasElements = true; break; }
                if (groupHasElements)
                    fail("c", std::string(c.group) + "." + c.field,
                         "the contract states this field and the snapshot does not carry it");
            }
        }

        if (static_cast<int>(contract.size()) != kUiSnapshotFieldCount)
            fail("c", "contract",
                 "the contract has " + std::to_string(contract.size()) +
                 " rows and §4.7 Stub 8 lists " + std::to_string(kUiSnapshotFieldCount));
        int mirrors = 0, deriveds = 0;
        for (const UiFieldContractEntry& c : contract)
            (c.kind == UiFieldKind::Mirror ? mirrors : deriveds) += 1;
        if (mirrors != kUiMirrorFieldCount || deriveds != kUiDerivedFieldCount)
            fail("c", "contract",
                 "the contract splits " + std::to_string(mirrors) + " mirror / " +
                 std::to_string(deriveds) + " derived; the stub marks " +
                 std::to_string(kUiMirrorFieldCount) + " / " +
                 std::to_string(kUiDerivedFieldCount));
    }

    r.ok = r.failures.empty();
    return r;
}

} // namespace strat
