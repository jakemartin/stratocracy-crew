// Stratocracy — UI binding contract, pass 2 (GDD §4.7 Stub 8, §4.11 row 8).
//
// Every value below is READ from the module that owns it. Nothing here computes a
// rule: the snapshot projects, and both queries delegate. See spec/ui_spec.md.
#include "Ui.h"

#include <algorithm>

namespace strat {

// ---------------------------------------------------------------------------
// lookups
// ---------------------------------------------------------------------------
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

} // namespace

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
        s.units.push_back(v);
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

} // namespace strat
