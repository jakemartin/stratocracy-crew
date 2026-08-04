// Stratocracy — UI binding contract, PASS 1 (GDD §4.7 Stub 8, §4.11 row 8).
//
// This file carries a plausible misreading of spec/ui_spec.md and §4.7 Stub 8, and
// is expected to be BLOCKED. Two defects, both readings a careful implementer
// reaches from the document alone:
//
//   (1) T-UI-02 -- the highlight is recomputed as "every hex within the unit's Move
//       by hex distance" instead of being queried from Move.h. It agrees on open
//       ground and diverges the moment terrain costs more than 1, is impassable, or
//       an occupant blocks a route.
//
//   (2) GATE-CAP-PARTIAL -- a tile with capture progress counts toward the capturing
//       side's `objectivesHeld`. "Objectives held X of N" beside a `captureProgress`
//       field reads as though progress were partial credit, which is exactly the
//       reading Q14 refuses.
//
// T-UI-01 is correct here: the break is a proper subset, so the gate demonstrates
// which IDs catch what rather than failing wholesale.
#include "Ui.h"

#include <algorithm>

namespace strat {

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

// DEFECT (2)'s helper: which side, if any, is part-way through capturing this hex.
int capturingSide(const UiWorld& w, const EconomyState& e, const Hex& h) {
    for (const CaptureProgress& c : e.captures) {
        if (!hexEqual(c.hex, h) || c.turnsHeld <= 0) continue;
        for (const UiUnit& u : w.units)
            if (u.id == c.unitId) return u.side;
    }
    return OWNER_NEUTRAL;
}

} // namespace

UiSnapshot buildUiSnapshot(const UiWorld& w) {
    UiSnapshot s;
    if (w.economy == nullptr || w.turn == nullptr) return s;
    const EconomyState& e = *w.economy;
    const TurnState&    t = *w.turn;

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
        const Objective* o = findObjective(e, h);
        v.owner = (o != nullptr) ? o->owner : OWNER_NEUTRAL;
        s.hexes.push_back(v);
    }

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
        v.hasMoved = hasMoved(t, u->id);
        v.hasActed = hasActed(t, u->id);
        v.captureProgress = progressForUnit(e, u->id);
        s.units.push_back(v);
    }

    for (int i = 0; i < SIDE_COUNT; ++i) {
        s.side[i].fameTotal  = e.side[i].fameTotal;
        s.side[i].fameCombat = e.side[i].fameCombat;

        int held = 0;
        for (const Objective& o : e.objectives) {
            if (o.owner == i) { ++held; continue; }
            // ---- DEFECT (2): partial credit. A capture short of completion counts
            // for the capturing side. Q14 rules it counts for NOBODY until the
            // objective flips.
            if (capturingSide(w, e, o.hex) == i) ++held;
        }
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

std::vector<ReachEntry> uiReachable(const UiWorld& w, int unitId) {
    std::vector<ReachEntry> out;
    const UiUnit* u = findUiUnit(w, unitId);
    if (u == nullptr || w.unitDefs == nullptr || w.terrain == nullptr) return out;
    if (u->defIndex < 0 || u->defIndex >= static_cast<int>(w.unitDefs->size())) return out;
    const UnitDef& def = (*w.unitDefs)[static_cast<std::size_t>(u->defIndex)];

    // ---- DEFECT (1): the highlight is recomputed here as a distance filter instead
    // of being queried from Move.h. Terrain cost, impassability and Q3's blocking
    // occupants are all invisible to it.
    std::vector<Hex> all;
    for (int row = 0; row < w.board.bounds.rows; ++row)
        for (int col = 0; col < w.board.bounds.cols; ++col)
            all.push_back(offsetToAxial(col, row));
    sortCanonical(all);
    for (const Hex& h : all) {
        const int dist = hexDistance(u->hex, h);
        if (dist <= def.move) {
            ReachEntry re;
            re.hex  = h;
            re.cost = dist;
            out.push_back(re);
        }
    }
    return out;
}

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

    f.distance = hexDistance(a->hex, d->hex);
    const UnitDef& ad = (*w.unitDefs)[static_cast<std::size_t>(a->defIndex)];
    if (f.distance < ad.rangeMin || f.distance > ad.rangeMax) {
        f.reason = "out of range";
        return f;
    }

    f.damage = resolveDamage(a->unit, d->unit, terrainDefPctAt(w, d->hex));
    const int defHpAfter = d->unit.hp - f.damage;
    f.defenderDies = (defHpAfter <= 0);

    if (!f.defenderDies) {
        Unit wounded = d->unit;
        wounded.hp = defHpAfter;
        if (defenderCanCounter(wounded, f.distance)) {
            f.counterFires  = true;
            f.counterDamage = resolveDamage(wounded, a->unit, terrainDefPctAt(w, a->hex));
        }
    }
    f.legal = true;
    return f;
}

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
