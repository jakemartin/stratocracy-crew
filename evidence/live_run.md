# Live crew run — evidence transcript

**Run date:** 2026-07-29 · **Commit:** `5ffa8d6` · **Model:** `anthropic/claude-sonnet-5`
· **Framework:** CrewAI v1.14.6, `Process.sequential` · **Verdict:** certified **17/17**

## Why this file exists

`build/` is gitignored, so `build/run_log.md` — where `run.py` writes the full console
trail on every run — never reaches a clone, and later runs overwrite it in place. Without
this file the only evidence a reader gets from the repo is `crew_evidence.html`, and the
only way to see the crew work is to supply an API key. What follows is the record of the
live run, transcribed from the evidence page generated at the time of that run.

Reproduce it yourself with `python run.py --online` and an `ANTHROPIC_API_KEY`; the trail
lands in `build/run_log.md`. The deterministic `python run.py --offline` path needs no key
and exercises the same 17-invariant gate.

## What the crew did

**Systems Engineer.** Received the Director's contract — `spec/combat_spec.md` plus
`spec/combat_spec_addendum.md` (type-effectiveness + repair) — and authored the whole of
`build/Combat.cpp`: the damage formula, the range-band counter check, a neutral
`effectiveness()` table, and `repairAmount()`. Compiled and self-tested its own output
through `run_test_gate`, iterating until green, then handed off.

**Test Engineer.** Independently certified via `certify_build`: a real `g++` compile and
run of all 17 invariants → **17/17**, writing `build/acceptance.json`.

**Balance Analyst.** Gated behind that record, ran self-play and reported.

## The gate — 17 invariants, all passing

| # | Invariant | ID |
|---|---|---|
| ✓ | Determinism | T-COMBAT-01 |
| ✓ | Min-damage floor (≥1) | T-COMBAT-02 |
| ✓ | Terrain reduces damage | T-COMBAT-03 |
| ✓ | Wounded attacker is weaker | T-COMBAT-04 |
| ✓ | Terrain = defender's hex only | T-COMBAT-05 |
| ✓ | Counter only within range band | T-COMBAT-06 |
| ✓ | **Artillery immune to range-1 counter** | T-COMBAT-07 |
| ✓ | Artillery counters at range 2–3 | T-COMBAT-08 |
| ✓ | Type-effectiveness ships neutral (all 1.0) | T-COMBAT-09 |
| ✓ | Neutral eff preserves the baseline | T-COMBAT-10 |
| ✓ | No heal at full HP | T-REPAIR-01 |
| ✓ | Basic heal on owned objective (+25%) | T-REPAIR-02 |
| ✓ | **Anti-fortress: no heal in enemy contact** | T-REPAIR-03 |
| ✓ | Must be on an owned objective | T-REPAIR-04 |
| ✓ | Never overheal past max | T-REPAIR-05 |
| ✓ | Min-1 heal floor | T-REPAIR-06 |
| ✓ | Determinism | T-REPAIR-07 |

The two bolded rows are the hallucination gates. `T-COMBAT-07` catches the counter rule
written as `distance <= rangeMax` (dropping `rangeMin`, letting Artillery counter in
melee); `T-REPAIR-03` catches the repair rule with the `!enemyAdjacent` clause dropped,
which turns a front-line factory into an unkillable fortress. Each passes every other test
and fails only its own. The offline run injects both deliberately: **pass 1 blocks on
T-COMBAT-07 + T-REPAIR-03 → pass 2 passes 17/17.**

## The agent-authored module

Written by the live Systems Engineer, verbatim:

```cpp
#include "Combat.h"
#include <cmath>
#include <algorithm>

namespace strat {

double effectiveness(UnitType attacker, UnitType defender) {
    // Neutral stub this revision: positional triangle only (GDD Pillar 3),
    // no type counter-chart yet. Every ordered pair maps to 1.0.
    (void)attacker;
    (void)defender;
    return 1.0;
}

int resolveDamage(const Unit& attacker, const Unit& defender, int terrainDefPct) {
    double eff = effectiveness(attacker.type, defender.type);
    double hpFrac = static_cast<double>(attacker.hp) / static_cast<double>(attacker.hpMax);
    double terrainFactor = 1.0 - (static_cast<double>(terrainDefPct) / 100.0);
    double raw = static_cast<double>(attacker.atk) * eff * hpFrac * terrainFactor;
    int rounded = static_cast<int>(std::lround(raw));
    int dmg = rounded - defender.def;
    return std::max(1, dmg);
}

bool defenderCanCounter(const Unit& defender, int distance) {
    // Full range band [rangeMin, rangeMax] — both ends matter (T-COMBAT-07).
    return distance >= defender.rangeMin && distance <= defender.rangeMax;
}

int repairAmount(const Unit& u, bool onOwnedObjective, bool enemyAdjacent) {
    if (!onOwnedObjective || enemyAdjacent || u.hp >= u.hpMax) {
        return 0;                       // anti-fortress clause (T-REPAIR-03)
    }
    int base = static_cast<int>(std::floor(0.25 * static_cast<double>(u.hpMax)));
    if (base < 1) { base = 1; }         // min-1 floor
    int room = u.hpMax - u.hp;
    return std::min(base, room);        // never overheal
}

} // namespace strat
```

## Balance Analyst — self-play findings

12 one-versus-one duels on plains. Win tally: **Tank 6 · Recon 3 · Artillery 2 ·
Infantry 1**.

The agent's read, not just the tally: Tank goes 6/6, but Artillery-vs-Tank is a
**methodology artifact**. The harness forces every duel to distance 1, where Artillery
(range 2–3) is correctly barred from countering — T-COMBAT-07/08 confirm the band gating
works — so it fights as "a worse Tank" in the one range where its standoff design gives it
nothing. Its proposal is **a fix to the test, not a stat nerf**: run each matchup at its
native range (add a distance-2 opening for `rangeMax ≥ 2`) so Artillery's real envelope
surfaces before anyone re-tunes numbers.

That proposal is still open — see the repo README.

## Provenance ledger rows this run backs (GDD §3)

| System | Author | Verified | Evidence |
|---|---|---|---|
| Combat resolution | agent | ✓ | `Combat.cpp` · T-COMBAT-01..10 (10/10) |
| Test suite | agent | ✓ | `test_combat.cpp` · 17/17 |
| Repair | agent | ✓ | `Combat.cpp::repairAmount` · T-REPAIR-01..07 (7/7) |
| Type-effectiveness | agent | ✓ | `Combat.cpp::effectiveness` · T-COMBAT-09..10 (neutral) |

All four are agent-authored and gate-verified by this run, committed at `5ffa8d6`. Every
other game system stays honestly marked *pending build*.

Because `build/` is gitignored, the committed home of each implementation is
`cpp_reference/*.good.cpp` and the harness that gates it is `cpp_reference/test_*.cpp`.
Cite those paths, not `build/`.
