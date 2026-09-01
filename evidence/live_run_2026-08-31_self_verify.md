# Stratocracy crew — run log

_live CrewAI crew_

```
MODE: live CrewAI crew (Anthropic API).

=== Test Engineer certification (for the record) ===
[Test Engineer] certify_build -> GATE PASS — all invariants hold (17/17 passed) | accepted=True
    PASS  T-COMBAT-01 determinism
    PASS  T-COMBAT-02 min-damage-floor
    PASS  T-COMBAT-03 terrain-reduces-damage
    PASS  T-COMBAT-04 wounded-attacker-weaker
    PASS  T-COMBAT-05 terrain-defender-only
    PASS  T-COMBAT-06 counter-within-range
    PASS  T-COMBAT-07 artillery-counter-immunity
    PASS  T-COMBAT-08 artillery-counters-at-range-2
    PASS  T-COMBAT-09 eff-neutral-stub
    PASS  T-COMBAT-10 eff-baseline-preserved
    PASS  T-REPAIR-01 full-hp-no-heal
    PASS  T-REPAIR-02 basic-heal
    PASS  T-REPAIR-03 anti-fortress
    PASS  T-REPAIR-04 must-own
    PASS  T-REPAIR-05 no-overheal
    PASS  T-REPAIR-06 min-1-floor
    PASS  T-REPAIR-07 determinism
    
    17/17 passed

=== Agent-authored build/Combat.cpp ===
    #include "Combat.h"
    #include <cmath>
    #include <algorithm>
    
    namespace strat {
    
    double effectiveness(UnitType attacker, UnitType defender) {
        (void)attacker;
        (void)defender;
        return 1.0;
    }
    
    int resolveDamage(const Unit& attacker, const Unit& defender, int terrainDefPct) {
        double eff = effectiveness(attacker.type, defender.type);
        double hpRatio = static_cast<double>(attacker.hp) / static_cast<double>(attacker.hpMax);
        double terrainFactor = 1.0 - (static_cast<double>(terrainDefPct) / 100.0);
        double raw = attacker.atk * eff * hpRatio * terrainFactor;
        int rounded = static_cast<int>(std::lround(raw));
        int dmg = rounded - defender.def;
        if (dmg < 1) dmg = 1;
        return dmg;
    }
    
    bool defenderCanCounter(const Unit& defender, int distance) {
        return distance >= defender.rangeMin && distance <= defender.rangeMax;
    }
    
    int repairAmount(const Unit& u, bool onOwnedObjective, bool enemyAdjacent) {
        if (!onOwnedObjective || enemyAdjacent || u.hp >= u.hpMax) return 0;
        int base = static_cast<int>(std::floor(0.25 * u.hpMax));
        if (base < 1) base = 1;
        int room = u.hpMax - u.hp;
        return std::min(base, room);
    }
    
    } // namespace strat

=== Self-play (Balance Analyst input) ===
    Stratocracy self-play - 1v1 duels on plains (adjacent, distance 1)
    attacker   vs defender   -> winner    (rounds)
    --------------------------------------------------------
    Infantry   vs Tank       -> Tank      (2)
    Infantry   vs Artillery  -> Infantry  (3)
    Infantry   vs Recon      -> Recon     (6)
    Tank       vs Infantry   -> Tank      (2)
    Tank       vs Artillery  -> Tank      (2)
    Tank       vs Recon      -> Tank      (3)
    Artillery  vs Infantry   -> Artillery (2)
    Artillery  vs Tank       -> Tank      (2)
    Artillery  vs Recon      -> Artillery (2)
    Recon      vs Infantry   -> Recon     (4)
    Recon      vs Tank       -> Tank      (3)
    Recon      vs Artillery  -> Recon     (2)
    
    Win tally (as attacker or counter-killer), 12 duels:
      Infantry   1
      Tank       6
      Artillery  2
      Recon      3

=== Final crew narrative (Balance Analyst) ===
# Balance Report — Stratocracy Self-Play (Certified Build)

## Win Tally (12 duels, adjacent/distance-1 only)
```
Tank       6   <- wins every matchup, as both attacker and defender
Recon      3
Artillery  2
Infantry   1
```

## Read-out
- **Tank is strictly dominant.** It wins all 6 of its duels — as attacker against Infantry, Artillery, and Recon, *and* as defender against the same three. There is no unit in this roster that beats Tank at melee range under any role assignment. That's not "strong," that's a solved matchup: any list-builder or matchmaking logic built on this table will just spam Tank.
- **Artillery only wins when it attacks first** (Artillery vs Infantry, Artillery vs Recon), and loses outright when anyone else initiates on it (Infantry vs Artillery still favors Infantry in 3 rounds; Tank vs Artillery and Recon vs Artillery both go to the non-Artillery side). Its win/loss pattern is driven entirely by first-strike order, not by any actual standoff capability — because every duel is forced to distance 1.

## Balance Risk: the melee-range assumption erases Artillery's identity and inflates Tank's dominance
T-COMBAT-07/08 already prove `defenderCanCounter` correctly denies Artillery a counter at distance 1 and grants it at distance 2–3. But `run_self_play` never *exercises* that branch — every duel here is pinned at distance 1, so Artillery's rangeMin=2 standoff advantage (its entire design premise) never gets to fire a shot in its own defense. The table we got is really "who wins at knife-fight range," which is a legitimate question for Infantry/Tank/Recon but a category error for Artillery. It's simultaneously making Tank look unbeatable (it's just the best *melee* unit, which is the only mode being tested) and making Artillery look like a weak-when-attacked unit (when its actual weakness is "if it gets caught at melee," which is a positioning/AI failure, not a stat failure).

## Proposed Change (pick one, methodology fix)
**Extend `run_self_play` to sweep multiple engagement distances (1, 2, 3) for any unit whose `rangeMin/rangeMax` differs from melee, and report a per-distance table instead of a single distance-1 grid.** Concretely: for Artillery duels, run the matchup at distance 2 (its rangeMin) in addition to distance 1, and tally wins separately as "melee" vs "standoff." This would let the Director see the real question — "does Artillery win decisively at range 2+ to justify its melee vulnerability?" — instead of conflating "Artillery is bad" with "Artillery was forced into the one range band it's explicitly built to avoid." Until that's in place, any stat nerf to Tank or buff to Artillery based on this table alone would be tuning against a mismeasured baseline.

=== Measured cost for this run ===
    # Crew run cost — AFTER - self-verifying Systems Engineer (b21504e architecture, current)
    
    - **Model:** `claude-sonnet-5`
    - **Measured at:** 2026-09-01T03:53:21+00:00
    - **Total measured cost:** **$0.067557**
    - **Most expensive step:** Systems Engineer
    - **API requests:** 7
    
    Rates (USD per 1M tokens): input $2.00 · output $10.00 · cache read $0.20 · cache write $2.50
    
    | Step | Uncached in | Cache read | Cache write | Output | Requests | Cost |
    |---|---:|---:|---:|---:|---:|---:|
    | Systems Engineer | 0 | 1,694 | 6,164 | 1,393 | 3 | $0.029679 |
    | Test Engineer | 448 | 0 | 1,749 | 761 | 2 | $0.012878 |
    | Balance Analyst | 411 | 0 | 1,551 | 2,030 | 2 | $0.025000 |
    | **Total** | **859** | **1,694** | **9,464** | **4,184** | **7** | **$0.067557** |
    -> cost_report_after_self_verify.json, cost_report_after_self_verify.md
[Test Engineer] combat provenance OK -- build/Combat.cpp matches the certified sha256 0e60eca76fd1.

==============================================================================
WEEK 1 — GDD §4.11 rows 1-8 + the debug-command driver
==============================================================================

[Director -> Systems Engineer] spec/hex_spec.md handed over (row 1 — Hex grid & math).
[Systems Engineer] wrote 1966 bytes to build/Hex.cpp (row 1 — Hex grid & math)
[Systems Engineer] authored — axial (q,r), the six-direction fixed order, canonical order r-asc then q-asc.
[Systems Engineer · self-test] row 1 (Hex grid & math) GATE PASS — T-HEX-01..07 (7/7 passed)
    PASS  T-HEX-01 six-candidates-fixed-order-filter-only-oob
    PASS  T-HEX-02 distance-is-a-metric
    PASS  T-HEX-03 distance-1-iff-neighbor
    PASS  T-HEX-04 direction-fairness
    PASS  T-HEX-05 inbounds-matches-Q1-dimensions
    PASS  T-HEX-06 combat-consumes-this-distance
    PASS  T-HEX-07 canonical-order-and-determinism
    
    7/7 passed

[Director -> Systems Engineer] spec/data_spec.md handed over (row 2 — Data tables (units/terrain)).
[Systems Engineer] wrote 12361 bytes to build/Data.cpp (row 2 — Data tables (units/terrain))
[Systems Engineer] authored — the §4.8 CSVs — hard fail on a missing column, never a silent default.
[Systems Engineer · self-test] row 2 (Data tables (units/terrain)) GATE PASS — T-DATA-01..04, 06 (6/6 passed)
    PASS  T-DATA-01 units-equal-the-2.4-table
    PASS  T-DATA-02 terrain-equals-the-2.3-table
    PASS  T-DATA-03 exactly-one-capturer-is-infantry
    PASS  T-DATA-04 sanity-costs-ranges-hp
    PASS  T-DATA-06 effectiveness-4x4-pinned-neutral
    PASS  GATE-DATA-HARDFAIL no-silent-defaults (4.8 contract)
    
    6/6 passed

[Director -> Systems Engineer] spec/move_spec.md handed over (row 3 — Movement & pathfinding; §4.11 says it depends on rows 1 and 2, both now green).
[Systems Engineer] wrote 6886 bytes to build/Move.cpp (row 3 — Movement & pathfinding)
[Systems Engineer] pass 1 authored — the reachable set is computed as 'every hex within hexDistance <= move'. Terrain cost is never consulted.

[Systems Engineer · self-test] row 3 (Movement & pathfinding) GATE BLOCK — failing: T-MOVE-01, T-MOVE-02, T-MOVE-03 (3/6 passed)
    FAIL  T-MOVE-01 reachable-set-is-exact
    FAIL  T-MOVE-02 terrain-costs-and-bridge-only-crossing
    FAIL  T-MOVE-03 occupancy-blocks-end-and-crossing
    PASS  T-MOVE-04 minimal-cost-canonical-tiebreak
    PASS  T-MOVE-05 no-zones-of-control
    PASS  T-MOVE-06 determinism
    
    3/6 passed

[Systems Engineer · self-test] BLOCK — T-MOVE-01, T-MOVE-02, T-MOVE-03 caught it. T-MOVE-01 compares the set against an independent shortest-path pass, so an estimate cannot pose as the real move set; T-MOVE-02 prices Woods at 2 and refuses Water; T-MOVE-03 catches that a blocked bridge no longer blocks. Fixing before hand-off.

[Systems Engineer] re-fed T-MOVE-01/02/03; replacing the estimate with Dijkstra over terrain cost, ties broken by canonical hex order.
[Systems Engineer] wrote 6383 bytes to build/Move.cpp (row 3 — Movement & pathfinding)
[Systems Engineer · self-test] row 3 (Movement & pathfinding) GATE PASS — T-MOVE-01..06 (6/6 passed)
    PASS  T-MOVE-01 reachable-set-is-exact
    PASS  T-MOVE-02 terrain-costs-and-bridge-only-crossing
    PASS  T-MOVE-03 occupancy-blocks-end-and-crossing
    PASS  T-MOVE-04 minimal-cost-canonical-tiebreak
    PASS  T-MOVE-05 no-zones-of-control
    PASS  T-MOVE-06 determinism
    
    6/6 passed

[Director -> Systems Engineer] spec/economy_spec.md handed over (row 4 — Capture & Fame economy). Four of its nine invariants encode a RULED question (Q4, Q5, Q6, Q8), so the gate asserts the ruling and not the intuition it overturned.
[Systems Engineer] wrote 8559 bytes to build/Economy.cpp (row 4 — Capture & Fame economy)
[Systems Engineer] pass 1 authored — income accrues on turn 1 (every strategy game pays you on turn 1), and passive income also credits fameCombat (Fame is one pool, so surely every source touches every counter).

[Systems Engineer · self-test] row 4 (Capture & Fame economy) GATE BLOCK — failing: T-FAME-01, T-FAME-02, T-FAME-08 (6/9 passed)
    FAIL  T-FAME-01 single-pool-and-separate-combat-counter
    FAIL  T-FAME-02 income-values-timing-and-configured-start
    PASS  T-FAME-03 exact-costs-refusal-never-negative
    PASS  T-FAME-04 spawn-fallback-wait-and-committed-fame
    PASS  T-FAME-05 capture-infantry-tile-held-resets-never-transfers
    PASS  T-FAME-06 income-flips-with-ownership
    PASS  T-FAME-07 kill-awards-flag-replaces-no-bonus
    FAIL  T-FAME-08 no-fame-cap
    PASS  T-FAME-09 determinism
    
    6/9 passed

[Systems Engineer · self-test] BLOCK — T-FAME-01, T-FAME-02, T-FAME-08 caught it. Q8 ruled that turn-1 buying power is starting Fame ALONE; and fameCombat is §2.8's tiebreak sort key, so crediting income to it would let a side that never fought win criterion 1 and make the mutual-passivity guard unreachable. Fixing before hand-off.

[Systems Engineer] re-fed Q8 and T-FAME-01; correcting both.
[Systems Engineer] wrote 7992 bytes to build/Economy.cpp (row 4 — Capture & Fame economy)
[Systems Engineer · self-test] row 4 (Capture & Fame economy) GATE PASS — T-FAME-01..09 (9/9 passed)
    PASS  T-FAME-01 single-pool-and-separate-combat-counter
    PASS  T-FAME-02 income-values-timing-and-configured-start
    PASS  T-FAME-03 exact-costs-refusal-never-negative
    PASS  T-FAME-04 spawn-fallback-wait-and-committed-fame
    PASS  T-FAME-05 capture-infantry-tile-held-resets-never-transfers
    PASS  T-FAME-06 income-flips-with-ownership
    PASS  T-FAME-07 kill-awards-flag-replaces-no-bonus
    PASS  T-FAME-08 no-fame-cap
    PASS  T-FAME-09 determinism
    
    9/9 passed

[Director -> Systems Engineer] spec/turn_spec.md handed over (row 5 — Turn loop & win/tiebreak). Rows 3 and 4 DECLINED the turn — row 4 takes the turn number as an argument — so every deferred turn-ownership question is concentrated here. Four invariants encode §2.8's tiebreak apparatus exactly.
[Systems Engineer] wrote 14954 bytes to build/Turn.cpp (row 5 — Turn loop & win/tiebreak)
[Systems Engineer] pass 1 authored — the cap tiebreak is a plain lexicographic comparison (both sides on zero simply ties at key 1 and falls through to objectives held), and the result tier grades by the size of the winning margin, the way nearly every strategy game reports a win.

[Systems Engineer · self-test] row 5 (Turn loop & win/tiebreak) GATE BLOCK — failing: T-TURN-01, T-TURN-01, T-TURN-05, T-TURN-06, T-TURN-07, T-TURN-10 (5/11 passed)
    FAIL  T-TURN-01 alternation-and-once-per-own-turn
    FAIL  T-TURN-01 two-independent-flags-in-either-order
    PASS  T-TURN-02 flag-death-ends-immediately
    PASS  T-TURN-03 domination-factories-only-at-turn-start
    PASS  T-TURN-04 cap-tiebreak-order-and-configured-cap
    FAIL  T-TURN-05 mutual-passivity-guard-no-fall-through
    FAIL  T-TURN-06 criterion-2-only-when-both-fought-and-tied
    FAIL  T-TURN-07 tiers-are-categorical
    PASS  T-TURN-08 repair-called-at-turn-start-with-callers-facts
    PASS  T-TURN-09 determinism
    FAIL  T-TURN-10 one-build-per-factory-per-turn
    
    5/11 passed

[Systems Engineer · self-test] BLOCK — T-TURN-01, T-TURN-01, T-TURN-05, T-TURN-06, T-TURN-07, T-TURN-10 caught it. §2.8 puts a mutual-passivity guard BEFORE the comparison precisely because a fall-through re-crowns the turtle §1.5 #1 closed, and T-TURN-06 fails downstream of the same omission; and §2.8 makes the tiers categorical so a capped grind's tally can never outrank a flag kill (§1.5 #5). Fixing before hand-off.

[Systems Engineer] re-fed §2.8's procedure — one guard, one three-key comparison, one grade; restoring the guard and making the tier categorical.
[Systems Engineer] wrote 15073 bytes to build/Turn.cpp (row 5 — Turn loop & win/tiebreak)
[Systems Engineer · self-test] row 5 (Turn loop & win/tiebreak) GATE PASS — T-TURN-01..10 (11/11 passed)
    PASS  T-TURN-01 alternation-and-once-per-own-turn
    PASS  T-TURN-01 two-independent-flags-in-either-order
    PASS  T-TURN-02 flag-death-ends-immediately
    PASS  T-TURN-03 domination-factories-only-at-turn-start
    PASS  T-TURN-04 cap-tiebreak-order-and-configured-cap
    PASS  T-TURN-05 mutual-passivity-guard-no-fall-through
    PASS  T-TURN-06 criterion-2-only-when-both-fought-and-tied
    PASS  T-TURN-07 tiers-are-categorical
    PASS  T-TURN-08 repair-called-at-turn-start-with-callers-facts
    PASS  T-TURN-09 determinism
    PASS  T-TURN-10 one-build-per-factory-per-turn
    
    11/11 passed
[Systems Engineer] wrote 44881 bytes to build/Scenario.cpp (row 7 — Scenario file & validator)
[Systems Engineer] wrote 40501 bytes to build/Ui.cpp (row 8 — UI binding contract)
[Systems Engineer] wrote 63606 bytes to build/Driver.cpp (row None — Debug-command driver)
[Director] Driver placed as a prerequisite of row 6's gate — the AI's commands are validated by the same 'execute' a typed command goes through, so T-AI-01 is structural rather than asserted. The driver reaches the scenario module, so row 7's implementation is placed with it; row 7's OWN gate runs below, on the same file, and re-authors it twice. Since row 8 landed the driver also renders the view model, so row 8's implementation is placed here too, on the same terms — its OWN gate runs below and re-authors it twice.

[Director -> Systems Engineer] spec/ai_spec.md handed over (row 6 — Opponent AI). This is the SHIPPING opponent: §2.9's difficulty is a starting-Fame handicap, so this one routine is what every tier plays against. It decides and applies nothing — one ordinary command at a time, through the player's path.
[Systems Engineer] wrote 15317 bytes to build/Ai.cpp (row 6 — Opponent AI (baseline))
[Systems Engineer] pass 1 authored — the losing-attack guard reads as 'do not attack if the counter kills you', and build ties break by the order §2.4's table prints its units.

[Systems Engineer · self-test] row 6 (Opponent AI (baseline)) GATE BLOCK — failing: T-AI-05, T-AI-06 (5/7 passed)
    (T-AI-01: 120 AI commands issued across 6 games)
    PASS  T-AI-01 legality-zero-rejected-commands
    PASS  T-AI-02 economy-phase-first-and-spends
    PASS  T-AI-03 capture-behaviour-moves-onto-undefended-objective
    PASS  T-AI-04 attack-preference-and-standoff
          (T-AI-05: 348 lethal exchanges, 348 skipped, 0 permitted)
    FAIL  T-AI-05 strictly-losing-guard-both-halves-bind
    FAIL  T-AI-06 determinism-and-Q9-tie-breaks
          (game 1: Decisive/FlagDestroyed after 5 turns, cap 6)
          (game 2: Decisive/FlagDestroyed after 3 turns, cap 5)
          (game 3: Marginal/AttritionLead after 4 turns, cap 4)
          (game 4: Marginal/AttritionLead after 4 turns, cap 4)
          (game 5: Marginal/AttritionLead after 5 turns, cap 5)
          (game 6: Draw/PassivityGuard after 3 turns, cap 3)
    PASS  GATE-AI-SMOKE self-play-games-terminate-with-a-tier
    
    5/7 passed

[Systems Engineer · self-test] BLOCK — T-AI-05, T-AI-06 caught it. §2.9 joins TWO conditions — the unit dies AND the exchange trades down — so an over-cautious guard refuses every sacrifice; and Q9 ruled the build priority is ascending §2.4 COST, which §4.7 warns in as many words is NOT the order §2.4's table prints. Fixing before hand-off.

[Systems Engineer] re-fed §2.9's guard and Q9's priority; restoring the second half of the guard and ordering builds by cost.
[Systems Engineer] wrote 14905 bytes to build/Ai.cpp (row 6 — Opponent AI (baseline))
[Systems Engineer · self-test] row 6 (Opponent AI (baseline)) GATE PASS — T-AI-01..06 + GATE-AI-SMOKE (7/7 passed)
    (T-AI-01: 120 AI commands issued across 6 games)
    PASS  T-AI-01 legality-zero-rejected-commands
    PASS  T-AI-02 economy-phase-first-and-spends
    PASS  T-AI-03 capture-behaviour-moves-onto-undefended-objective
    PASS  T-AI-04 attack-preference-and-standoff
          (T-AI-05: 348 lethal exchanges, 338 skipped, 10 permitted)
    PASS  T-AI-05 strictly-losing-guard-both-halves-bind
    PASS  T-AI-06 determinism-and-Q9-tie-breaks
          (game 1: Decisive/FlagDestroyed after 5 turns, cap 6)
          (game 2: Decisive/FlagDestroyed after 3 turns, cap 5)
          (game 3: Marginal/AttritionLead after 4 turns, cap 4)
          (game 4: Marginal/AttritionLead after 4 turns, cap 4)
          (game 5: Marginal/AttritionLead after 5 turns, cap 5)
          (game 6: Draw/PassivityGuard after 3 turns, cap 3)
    PASS  GATE-AI-SMOKE self-play-games-terminate-with-a-tier
    
    7/7 passed

[Director -> Systems Engineer] spec/scenario_spec.md handed over (row 7 — Scenario file & validator). It carries a SCOPE RULING: the two stretch maps are not authored as scenario files, not even as validator fixtures, so four of §4.7 Stub 7's fixtures have nothing to run against. The consequence is stated, not hidden — row 7 records a PARTIAL PASS and its ledger row does not flip.
[Systems Engineer] wrote 45211 bytes to build/Scenario.cpp (row 7 — Scenario file & validator)
[Systems Engineer] pass 1 authored — T-SCN-11's opposing route is minimised over the opposing seat's guidedOpening.infantry alone, the same NAMED-hex quantifier T-SCN-06 insists on.

[Systems Engineer · self-test] row 7 (Scenario file & validator) GATE BLOCK — failing: T-SCN-11 (11/12 passed)
    (data/ferrum_crossing.json loaded: ferrum_crossing, 11x9, symmetry none, turnCap 20, hash 266d3c3fb5e5141e)
    PASS  T-SCN-01 one-flag-per-side-and-it-is-a-Tank
    PASS  T-SCN-02 structural-validity
    PASS  T-SCN-03 economy-validity
    PASS  T-SCN-04 playability-flags-mutually-reachable
    PASS  T-SCN-05 odd-r-axial-round-trip-and-adjacency
    PASS  T-SCN-06 opening-capture-lane-derived-ceiling
    PASS  T-SCN-07 opening-capture-naming
    PASS  T-SCN-08 lane-costs-measured-not-inferred
    PASS  T-SCN-09 declared-symmetry-verified-refusal-branch
          (East: opposing: got 7, wanted 6)
          (East's opposing minimiser: (1,5))
          (fixture (b) PASSED; it must be refused)
          (fixture (b) reason: )
          (fixture (b) did not report 5 and 5)
          (fixture (b) minimiser: (9,3))
          (a tie was accepted)
          (East's opposing term is the guided unit's cost, not the set minimum)
    FAIL  T-SCN-11 non-contention-minimised-over-every-capturer
    PASS  GATE-SCN-PARSE malformed-input-is-refused
    PASS  GATE-SCN-HASH canonical-serialization-is-content-only
    
    NOT RUN  T-SCN-08 (a) -- The Causeway, reporting 3 and 3. Needs §2.13.6 as a
             scenario file; the Director's scope ruling authors none, not even as
             a fixture. No synthetic map may stand in: a lost fixture is reported,
             never replaced.
    NOT RUN  T-SCN-08 (b) -- Longwater March, rot180 on 13 x 8, reporting 4 and 4.
             Same reason (§2.13.5).
    NOT RUN  T-SCN-09 asserting branch -- rho asserts hex by hex, and the only
             scenario file that exists declares `none`, which asserts nothing. The
             refusal branch above IS reachable from the shipped map's own
             declaration and does run.
    NOT RUN  T-SCN-11 (c) -- The Causeway, 3 against 5 with the crossing permitted.
             Same reason (§2.13.6). Asymmetry (ii) is exercised above on the
             shipped map instead, where excluding the Bridges MOVES THE MINIMISER
             from (1,3) to (1,5) -- which is a weaker witness than a bisected map,
             because no gate here fails under the Bridge-free reading.
    NOT RUN  T-SCN-10 -- reserved and UNWRITTEN on Q26 (ruled): the enum stays at
             rot180 | none, so a horizontal mirror is undeclarable and there is
             nothing for a gate to verify. Nothing is asserted, so nothing is
             waiting -- a different state from T-MOVE-07, which IS blocked.
    
    11/12 passed

[Systems Engineer · self-test] BLOCK — T-SCN-11 caught it. Q28 ruled the opposing route ranges over EVERY CanCapture-row unit that seat deploys, because the property guarded is a RACE and a race does not care which Infantry wins it. Fixture (b) — the shipped map's own pre-fix deployment — exists to catch exactly this reading: under it (b) passes at 5 against 6 instead of failing at 5 against 5. Fixing before hand-off.

[Systems Engineer] re-fed Q28; minimising over the opposing seat's whole capturing force instead of over its marked unit.
[Systems Engineer] wrote 44881 bytes to build/Scenario.cpp (row 7 — Scenario file & validator)
[Systems Engineer · self-test] row 7 (Scenario file & validator) GATE PASS — T-SCN-01..07, 08 (c), 09 refusal, 11 (a)(b) + GATE-SCN-PARSE/HASH (12/12 passed)
    (data/ferrum_crossing.json loaded: ferrum_crossing, 11x9, symmetry none, turnCap 20, hash 266d3c3fb5e5141e)
    PASS  T-SCN-01 one-flag-per-side-and-it-is-a-Tank
    PASS  T-SCN-02 structural-validity
    PASS  T-SCN-03 economy-validity
    PASS  T-SCN-04 playability-flags-mutually-reachable
    PASS  T-SCN-05 odd-r-axial-round-trip-and-adjacency
    PASS  T-SCN-06 opening-capture-lane-derived-ceiling
    PASS  T-SCN-07 opening-capture-naming
    PASS  T-SCN-08 lane-costs-measured-not-inferred
    PASS  T-SCN-09 declared-symmetry-verified-refusal-branch
    PASS  T-SCN-11 non-contention-minimised-over-every-capturer
    PASS  GATE-SCN-PARSE malformed-input-is-refused
    PASS  GATE-SCN-HASH canonical-serialization-is-content-only
    
    NOT RUN  T-SCN-08 (a) -- The Causeway, reporting 3 and 3. Needs §2.13.6 as a
             scenario file; the Director's scope ruling authors none, not even as
             a fixture. No synthetic map may stand in: a lost fixture is reported,
             never replaced.
    NOT RUN  T-SCN-08 (b) -- Longwater March, rot180 on 13 x 8, reporting 4 and 4.
             Same reason (§2.13.5).
    NOT RUN  T-SCN-09 asserting branch -- rho asserts hex by hex, and the only
             scenario file that exists declares `none`, which asserts nothing. The
             refusal branch above IS reachable from the shipped map's own
             declaration and does run.
    NOT RUN  T-SCN-11 (c) -- The Causeway, 3 against 5 with the crossing permitted.
             Same reason (§2.13.6). Asymmetry (ii) is exercised above on the
             shipped map instead, where excluding the Bridges MOVES THE MINIMISER
             from (1,3) to (1,5) -- which is a weaker witness than a bisected map,
             because no gate here fails under the Bridge-free reading.
    NOT RUN  T-SCN-10 -- reserved and UNWRITTEN on Q26 (ruled): the enum stays at
             rot180 | none, so a horizontal mirror is undeclarable and there is
             nothing for a gate to verify. Nothing is asserted, so nothing is
             waiting -- a different state from T-MOVE-07, which IS blocked.
    
    12/12 passed

[Director -> Systems Engineer] spec/ui_spec.md handed over (row 8 — UI binding contract). It owns how a widget is FED, not what a widget looks like, which is §2.11's lane. Like row 7 it records a PARTIAL PASS: T-UI-03 and T-UI-04 are in-editor Unreal Automation, marked † in §4.11, and what they lack is the real Stratocracy widgets they assert over — the editor pass they also lacked landed at UE fed8ae9 — so the ledger row does not flip and the runner names both by name.
[Systems Engineer] wrote 39459 bytes to build/Ui.cpp (row 8 — UI binding contract)
[Systems Engineer] pass 1 authored — five readings the spec names in advance: the highlight recomputed as a hex-distance filter; partial credit toward objectivesHeld for a capture in progress; incomePerTurn read from accrueIncome, which pays 0 on turn 1; isGuidedMarked keyed on the unit's current hex; and spawnBlocked set equal to buildWaiting.

[Systems Engineer · self-test] row 8 (UI binding contract) compile FAILED
    clang++: error: linker command failed with exit code 1120 (use -v to see invocation)

[Systems Engineer · self-test] BLOCK —  caught it. Q14 refuses partial credit — a capture in progress counts for nobody until the objective flips; Q8(a) pays no income on turn 1 while ruling G makes incomePerTurn the STANDING rate, so the two differ exactly where a wrong read is invisible; isGuidedMarked is a property of the placement, not of where the unit stands now; and buildWaiting is the queued-slot fact, which cannot express a boxed-in factory with nothing queued. Fixing before hand-off.

[Systems Engineer] re-fed Q14, Q8(a) with ruling G, and rulings E and J; recomputing each DECLARED DERIVED field from the stub's words.
[Systems Engineer] wrote 40501 bytes to build/Ui.cpp (row 8 — UI binding contract)
[Systems Engineer · self-test] row 8 (UI binding contract) GATE PASS — T-UI-01, 02, 05 + GATE-CAP-PARTIAL (54/54 passed)
    === §4.11 row 8 — UI binding contract (§4.7 Stub 8) ===
    Headless half only. T-UI-03 and T-UI-04 are in-editor and do not run;
    they are named at the end. Row 8's ledger row does not flip (Q29).
    
    -- T-UI-01  forecast = resolution ------------------------------------
    PASS  T-UI-01 (a) every forecast equals a direct Combat.h computation
          (placements swept: 15872)
          (legal forecasts among them: 3160)
          (of those, forecasts in which a counter fires: 1276)
          (mismatches against resolveDamage / defenderCanCounter: 0)
    PASS  T-UI-01 (a2) the sweep actually exercised counters
    PASS  T-UI-01 (b) the resolution spends exactly the forecast's damage
          (forecast damage: 3)
          (Combat.h damage: 3)
          (defender hp after: 17)
    PASS  T-UI-01 (c) the forecast a caller reads twice does not move
    
    -- T-UI-02  highlight = the T-MOVE-01 set ----------------------------
    PASS  T-UI-02 (a) the highlight is Move.h's set, hex for hex and cost for cost
          (start hexes x unit rows compared: 128)
          (divergences: 0)
    PASS  T-UI-02 (b) an occupant shrinks the highlight, and it is still Move.h's set
          (hexes highlighted with the lane open: 13)
          (hexes highlighted with an occupant at (2,2): 10)
    PASS  T-UI-02 (c) the fixture discriminates: distance filter != Move.h's set
          (hexes within Move by hex distance: 21)
          (hexes Move.h actually reaches: 13)
    
    -- GATE-CAP-PARTIAL  a capture in progress counts for nobody ---------
    PASS  GATE-CAP-PARTIAL (a) progress short of completion leaves BOTH sides' objectivesHeld unchanged
          (side 0 objectivesHeld before: 0)
          (side 0 objectivesHeld with a capture in progress: 0)
          (side 1 objectivesHeld before: 1)
          (side 1 objectivesHeld with a capture in progress: 1)
    PASS  GATE-CAP-PARTIAL (b) the unit's captureProgress did rise in that same step
          (captureProgress before: 0)
          (captureProgress after one tick: 1)
    PASS  GATE-CAP-PARTIAL (c) completion DOES move objectivesHeld, and clears progress
          (side 0 objectivesHeld after the flip: 1)
          (objectiveTotal (the N of X of N): 2)
    
    -- snapshot shape (ungated; see the note below) ----------------------
    PASS  snapshot carries hasMoved and hasActed as two independent fields
    PASS  spending the act flag leaves the move flag spent, not replaced
    PASS  units are projected in ascending id, hexes in canonical order
    PASS  match view reads turn, cap, side to move, and no result yet
    
    -- T-UI-05  snapshot fidelity ----------------------------------------
    PASS  T-UI-05 (a) the projection is faithful under all three clauses
    PASS  T-UI-05 (a2) the check examined both kinds, not just one
          (mirrors checked: 102)
          (declared-derived checked: 9)
          (fields enumerated: 111)
    PASS  T-UI-05 (b) the contract transcribes Stub 8's field list exactly
    PASS  T-UI-05 (b2) every contract row names a module-side value or states a derivation
    PASS  T-UI-05 (c) clause (a) rejects a mirror that does not equal the module
    PASS  T-UI-05 (c) clause (b) rejects a derivation the stub does not state
    PASS  T-UI-05 (c) clause (b) rejects a guided mark the scenario does not name
    PASS  T-UI-05 (c) clause (c) rejects a snapshot whose shape left the contract
    PASS  T-UI-05 (d) incomePerTurn is the STANDING rate on turn 1, not the accrual
          (standing rate: 100)
          (accrued on turn 1: 0)
    PASS  T-UI-05 (d2) the Town rate is read from the table too, not hardcoded to factories
    PASS  T-UI-05 (e) the scenario's guided seat is marked, and only it
    PASS  T-UI-05 (e2) the mark does not move when the unit does
    PASS  T-UI-05 (e3) and the moved world is still faithful
    PASS  T-UI-05 (f) a boxed-in factory reports spawnBlocked with NOTHING queued, which buildWaiting alone cannot express
    PASS  T-UI-05 (f2) and the derivation still agrees with the check
    PASS  T-UI-05 (f3) the fixture discriminates: freeing one neighbour clears spawnBlocked
    PASS  T-UI-05 (g) buildWaiting and hasBuiltThisTurn are read from two places and both are true after a queued build
    PASS  T-UI-05 (g2) the snapshot is faithful after EVERY command of the sequence
          (commands replayed: 6)
    PASS  T-UI-05 (h) a unit can be locked and not done at once, so the block's two members are not one field
    PASS  T-UI-05 (h2) the block is outside the invariant's subject: fidelity does not read it
    
    -- GATE-BUILDLIST  the production menu's query (§2.11.5) --------------
    PASS  GATE-BUILDLIST (pre) the fixture discriminates: the table has more than one price, and this factory is NOT boxed in
    PASS  GATE-BUILDLIST (a) EVERY unit-table row is returned, in table order, each mirroring its own id and costFame
          (rows returned: 4)
    PASS  GATE-BUILDLIST (b) `affordable` is module-computed (T-UI-03) and discriminates: it agrees with the table and is neither all nor none
          (affordable rows at the cheapest price: 1)
    PASS  GATE-BUILDLIST (c) at an owned, idle, un-queued factory every row is available and carries no reason
    PASS  GATE-BUILDLIST (d) T-TURN-10's spent allowance closes the whole menu, in the words the module refuses in
    PASS  GATE-BUILDLIST (e) a factory already holding a waiting build offers no new option, in queueBuild's own words
    PASS  GATE-BUILDLIST (f-control) the boxed-in fixture really is boxed in: spawnHexesBlocked says so
    PASS  GATE-BUILDLIST (f) Q31, RULED: a boxed-in factory still offers every row -- spawnBlocked is informational and never folds into availability
    PASS  GATE-BUILDLIST (g0) a side that is not the active side is refused by T-TURN-10 first, in markBuilt's words -- the allowance outranks the ownership question because a caller must consult it first
    PASS  GATE-BUILDLIST (g) a factory the active side does not hold is refused in queueBuild's words
    PASS  GATE-BUILDLIST (h) an objective the side DOES hold but that is not a build point is refused in queueBuild's words
    PASS  GATE-BUILDLIST (i) across every state above, `available` is a property of the FACTORY and never of the row -- the cap cannot be leaking here
    PASS  GATE-BUILDLIST (j) unavailable does not imply unaffordable: a closed factory still reports what the side could pay for
    PASS  GATE-BUILDLIST (k) and unaffordable does not imply unavailable: at zero Fame every row is still offered, priced and greyable
    
    -- GATE-MATCHRESULT  who won, and by what (SEC 2.8) ------------------
    PASS  GATE-MATCHRESULT (pre) the fixture discriminates: the module's winner is NOT the side to move, so deriving one from the other would differ
          (module winner: 1)
          (sideToMove: 0)
    PASS  GATE-MATCHRESULT (a) all four fields mirror the MatchResult the turn module itself returned
          (Decisive)
          (FlagDestroyed)
    PASS  GATE-MATCHRESULT (b) the snapshot agrees on the TIER and is silent on the winner -- the projection loss this query exists to close
    PASS  GATE-MATCHRESULT (c) the draw fixture actually reached a result -- without this the mirror check below passes on an in-progress default
    PASS  GATE-MATCHRESULT (d) a draw reports SIDE_NONE and mirrors the module's own cause and key, with no side invented for it
          (Draw)
          (PassivityGuard)
    PASS  GATE-MATCHRESULT (e) a world with no turn state reports InProgress and SIDE_NONE rather than grading a match it cannot see
    
    NOT RUN  T-UI-03 -- the live standings scoreboard binds 1:1 to snapshot
             fields with no widget-side arithmetic. In-editor Unreal
             Automation over widget bindings (§4.7 Stub 8, Acceptance;
             marked † in §4.11). An in-editor pass now EXISTS; what
             these two lack are the real Stratocracy widgets they
             bind over. WRITTEN, UNBLOCKED and ASSERTING: what they
             lack is a subject, not a rule.
    NOT RUN  T-UI-04 -- the production menu binds to the buildlist derived
             from the four Stub-2 unit rows plus current fameTotal, and the
             flag never appears. Same reason, same state. The buildlist
             QUERY now EXISTS -- uiBuildOptions, ruled 2026-08-20, gated
             above as GATE-BUILDLIST -- so what this ID still lacks is the
             WIDGET that binds to it, not a shape. Row 8 does not flip.
    
    NOTE     The gap this suite filed at 7c36303 -- that no written invariant
             asserted the snapshot mirrors the rules modules at all, while
             T-UI-01..04 each assert a binding DOWNSTREAM of it -- was RULED
             on 2026-08-04. It is T-UI-05, numbered, headless and unmarked,
             and §4.5's written-ID count moved 70 -> 71. It runs above; the
             four snapshot-shape checks are now upstream of a gated ID
             rather than standing in for one.
    NOTE     GATE-CAP-PARTIAL ran on a fixture with captureTurns = 2. The
             shipped scenario ships N = 1 (§2.7), so Ferrum Crossing cannot
             reach the state this gate asserts about. N is per-scenario data
             and the fixture configures it; no map was invented.
    
    54/54 passed

[Director -> Systems Engineer] spec/save_spec.md handed over (row 10 — Save & replay format). §4.11 splits the row into three parts with three dependency sets and THIS IS PART (a) ALONE: the format spec plus the header/version machinery, which has no dependencies at all and on which T-SAVE-04 closes by itself, 'since it never applies a command'. No command is applied here and §4.10's canonical state hash is NOT defined here — that is part (b), and the stateHash in Driver.h is the driver's own debug digest (GATE-DRV-06), a different thing. Six of the row's seven IDs do not run and the runner names each with its reason. Row 10 is a PROPOSED ledger row and has none to flip.
[Systems Engineer] wrote 23685 bytes to build/Save.cpp (row 10 — Save & replay format, part (a))
[Systems Engineer] pass 1 authored — three defects the spec names in advance: loadSave parses into the CALLER'S object and validates afterwards; checkHeader compares only formatVersion and rulesCommit, two of the four fields §4.10's Version policy enumerates; and an unknown key is tolerated instead of refused.

[Systems Engineer · self-test] row 10 (Save & replay format, part (a)) GATE BLOCK — failing: T-SAVE-04, T-SAVE-04, T-SAVE-04, T-SAVE-04, T-SAVE-04, T-SAVE-04, GATE-SAVE-PARSE (18/25 passed)
    PASS  T-SAVE-04 control matching-header-loads
    FAIL  T-SAVE-04 (a) formatVersion-mismatch-refused
    FAIL  T-SAVE-04 (b) rulesCommit-mismatch-refused
    FAIL  T-SAVE-04 (c) dataHash-mismatch-refused
    FAIL  T-SAVE-04 (d) scenarioHash-mismatch-refused
    PASS  T-SAVE-04 (e) scenarioId-is-not-a-refusal-trigger
    FAIL  T-SAVE-04 (f) first-disagreement-in-table-order-is-reported
    FAIL  T-SAVE-04 (g) header-check-is-separable-from-the-parse
    FAIL  GATE-SAVE-PARSE unknown-key
          why: accepted
    PASS  GATE-SAVE-PARSE duplicate-key
    PASS  GATE-SAVE-PARSE trailing-comma
    PASS  GATE-SAVE-PARSE null-outside-result
    PASS  GATE-SAVE-PARSE non-integer-number
    PASS  GATE-SAVE-PARSE u-escape
    PASS  GATE-SAVE-PARSE non-zero-seed
    PASS  GATE-SAVE-PARSE unknown-command
    PASS  GATE-SAVE-PARSE foreign-field
    PASS  GATE-SAVE-PARSE bad-side
    PASS  GATE-SAVE-PARSE missing-key
    PASS  GATE-SAVE-PARSE trailing-content
    PASS  GATE-SAVE-PARSE raw-control-character
    PASS  GATE-SAVE-PARSE result-may-be-a-string
    PASS  GATE-SAVE-PARSE serialize-parse-serialize-is-stable
    PASS  GATE-SAVE-PARSE empty-command-log-is-legal
    PASS  GATE-SAVE-PARSE every-4.9-command-kind-round-trips
    
    NOT RUN HERE  T-SAVE-01, T-SAVE-02, T-SAVE-03 and T-SAVE-05. All four need
             the HEADLESS REPLAYER and the canonical state hash, which are part
             (b); part (a) applies no command and defines no hash, so none of
             them has a subject in THIS suite. They are not outstanding: part
             (b) has since landed and closes all four in its own runner, which
             runs beside this one. In particular T-SAVE-03 is still NOT covered
             by the empty-log case above -- the parser accepting every prefix
             as a DOCUMENT is not every prefix being a LOADABLE SAVE.
    NOT RUN  T-SAVE-06 stateHash stability across the headless and in-engine
             builds. Marked † in §4.11 and asserted jointly with T-INT-02, so
             it closes in the editor pass and in no headless build. It is NOT
             RUN here.
             Both blockers this line used to name are gone -- the canonical
             state hash was built by part (b), and the in-editor Automation
             harness landed at UE fed8ae9.
    NOT RUN  T-SAVE-07 harness compatibility (a Balance Analyst self-play log
             validates and replays as a save). Needs row 6's self-play output.
             Part (c), week 4.
    
    Row 10 is a PROPOSED ledger row (§4.11) and has none to flip. T-SAVE-04 is
    the one acceptance ID this build closes.
    
    18/25 passed

[Systems Engineer · self-test] BLOCK — GATE-SAVE-PARSE, T-SAVE-04 caught it. T-SAVE-04 states THREE things and the pass-1 module satisfies only the first: refused, refused WITH A REASON, and the caller's state UNTOUCHED. Filling the caller's object before validating is the defect the 'state untouched' clause exists for, and it is invisible to any fixture that only checks the return value. Fixing before hand-off.

[Systems Engineer] re-fed §4.10's Version policy: the refusal set is the four fields it enumerates, the parse fills a local and assigns once on success, and an unknown key is a refusal because within one formatVersion it is a typo.
[Systems Engineer] wrote 24046 bytes to build/Save.cpp (row 10 — Save & replay format, part (a))
[Systems Engineer · self-test] row 10 (Save & replay format, part (a)) GATE PASS — T-SAVE-04 + GATE-SAVE-PARSE (25/25 passed)
    PASS  T-SAVE-04 control matching-header-loads
    PASS  T-SAVE-04 (a) formatVersion-mismatch-refused
    PASS  T-SAVE-04 (b) rulesCommit-mismatch-refused
    PASS  T-SAVE-04 (c) dataHash-mismatch-refused
    PASS  T-SAVE-04 (d) scenarioHash-mismatch-refused
    PASS  T-SAVE-04 (e) scenarioId-is-not-a-refusal-trigger
    PASS  T-SAVE-04 (f) first-disagreement-in-table-order-is-reported
    PASS  T-SAVE-04 (g) header-check-is-separable-from-the-parse
    PASS  GATE-SAVE-PARSE unknown-key
    PASS  GATE-SAVE-PARSE duplicate-key
    PASS  GATE-SAVE-PARSE trailing-comma
    PASS  GATE-SAVE-PARSE null-outside-result
    PASS  GATE-SAVE-PARSE non-integer-number
    PASS  GATE-SAVE-PARSE u-escape
    PASS  GATE-SAVE-PARSE non-zero-seed
    PASS  GATE-SAVE-PARSE unknown-command
    PASS  GATE-SAVE-PARSE foreign-field
    PASS  GATE-SAVE-PARSE bad-side
    PASS  GATE-SAVE-PARSE missing-key
    PASS  GATE-SAVE-PARSE trailing-content
    PASS  GATE-SAVE-PARSE raw-control-character
    PASS  GATE-SAVE-PARSE result-may-be-a-string
    PASS  GATE-SAVE-PARSE serialize-parse-serialize-is-stable
    PASS  GATE-SAVE-PARSE empty-command-log-is-legal
    PASS  GATE-SAVE-PARSE every-4.9-command-kind-round-trips
    
    NOT RUN HERE  T-SAVE-01, T-SAVE-02, T-SAVE-03 and T-SAVE-05. All four need
             the HEADLESS REPLAYER and the canonical state hash, which are part
             (b); part (a) applies no command and defines no hash, so none of
             them has a subject in THIS suite. They are not outstanding: part
             (b) has since landed and closes all four in its own runner, which
             runs beside this one. In particular T-SAVE-03 is still NOT covered
             by the empty-log case above -- the parser accepting every prefix
             as a DOCUMENT is not every prefix being a LOADABLE SAVE.
    NOT RUN  T-SAVE-06 stateHash stability across the headless and in-engine
             builds. Marked † in §4.11 and asserted jointly with T-INT-02, so
             it closes in the editor pass and in no headless build. It is NOT
             RUN here.
             Both blockers this line used to name are gone -- the canonical
             state hash was built by part (b), and the in-editor Automation
             harness landed at UE fed8ae9.
    NOT RUN  T-SAVE-07 harness compatibility (a Balance Analyst self-play log
             validates and replays as a save). Needs row 6's self-play output.
             Part (c), week 4.
    
    Row 10 is a PROPOSED ledger row (§4.11) and has none to flip. T-SAVE-04 is
    the one acceptance ID this build closes.
    
    25/25 passed

[Director -> Systems Engineer] spec/replay_spec.md handed over (row 10 part (b) — headless replayer + canonical state hash). Part (b) RUNS T-SAVE-01/02/03/05/06 and CLOSES four of them: T-SAVE-06 is marked † in §4.11 and asserted jointly with T-INT-02, so it closes in the editor pass and nowhere headless. §4.11 put closure in part (c) because week 2's log carried only {Move, Attack}; rows 4, 5 and 6 have all since landed, so the log here is the COMPLETE §4.9 command set and a segment of it is generated by row 6's AI, which is what puts T-AI-06 inside T-SAVE-02's composition. This is a separate registry row from `save` so part (a)'s empty link set stays a checked claim. Row 10 is a PROPOSED ledger row and still has none to flip.
[Systems Engineer] wrote 24484 bytes to build/Replay.cpp (row 10 — Save & replay, part (b) — replayer + state hash)
[Systems Engineer] pass 1 authored — three defects, each a mechanical edit of the good module: replayLog applies to the caller's state IN PLACE rather than to a copy; the canonical hash walks units in STORAGE order rather than canonical hex order; and it omits the two per-unit turn flags.

[Systems Engineer · self-test] row 10 (Save & replay, part (b) — replayer + state hash) GATE BLOCK — failing: GATE-REPLAY-BYTES, GATE-REPLAY-ORDER, GATE-REPLAY-FLAGS, GATE-REPLAY-FLAGS, GATE-REPLAY-FLAGS, T-SAVE-05, T-SAVE-05, T-SAVE-05, GATE-REPLAY-FIXTURE, GATE-REPLAY-FIXTURE (26/36 passed)
    PASS  GATE-REPLAY-SETUP complete-command-set
    PASS  GATE-REPLAY-SETUP ai-generated-segment-present
    FAIL  GATE-REPLAY-BYTES canonical-serialisation-field-order
    FAIL  GATE-REPLAY-ORDER storage-order-does-not-leak
    FAIL  GATE-REPLAY-FLAGS hasMoved-changes-the-digest
    FAIL  GATE-REPLAY-FLAGS hasActed-changes-the-digest
    FAIL  GATE-REPLAY-FLAGS moved-and-acted-are-distinguishable
    PASS  GATE-REPLAY-BUILD built-this-turn-changes-the-digest
    PASS  T-SAVE-01 (a) the log replays clean
    PASS  T-SAVE-01 (b) the serialised save loads
    PASS  T-SAVE-01 (c) the loaded log replays clean
    PASS  T-SAVE-01 (d) save -> load -> identical stateHash
    PASS  T-SAVE-01 (e) the file's carried stateHash is the one it describes
    PASS  T-SAVE-01 (f) the log survives the round trip entry for entry
    PASS  T-SAVE-02 (a) both loads succeed
    PASS  T-SAVE-02 (b) both replays succeed
    PASS  T-SAVE-02 (c) the same file loaded twice -> identical hashes
    PASS  T-SAVE-02 (d) identical canonical serialisation, not only digest
    PASS  T-SAVE-02 (e) serialize is stable across a round trip
    PASS  T-SAVE-03 (a) every prefix is a loadable, replayable save
    PASS  T-SAVE-03 (b) each prefix reaches the state replaying it directly does
    PASS  T-SAVE-03 (c) the empty prefix is the initial state
    PASS  T-SAVE-05 (a) the log is refused
    PASS  T-SAVE-05 (b) the refusal names the offending index
    PASS  T-SAVE-05 (c) it names T-SAVE-05 and gives a reason
    PASS  T-SAVE-05 (d) nothing was applied
    FAIL  T-SAVE-05 (e) the pre-load state survives, by digest
    FAIL  T-SAVE-05 (f) the pre-load state survives, field for field
    FAIL  T-SAVE-05 (g) an illegal command at the LAST index refuses the file
    PASS  GATE-REPLAY-FIXTURE (a) the shipped tables and scenario load
    PASS  GATE-REPLAY-FIXTURE (b) the committed fixture is present
    PASS  GATE-REPLAY-FIXTURE (c) it parses as a §4.10 save
    PASS  GATE-REPLAY-FIXTURE (d) the scenario seeds a GameState
    PASS  GATE-REPLAY-FIXTURE (e) its command log replays clean
    FAIL  GATE-REPLAY-FIXTURE (f) the carried stateHash is the one it describes
    FAIL  GATE-REPLAY-FIXTURE (g) re-emitting reproduces the committed bytes
    
    NOT RUN  T-SAVE-06 stateHash stability across the headless and in-engine
             builds. §4.11 marks it †, it is asserted JOINTLY with T-INT-02,
             which replays IN-ENGINE -- so no headless build closes it, and
             this one does not run the ID. It is NOT RUN here. The bridge
             that landed at UE 0897cb5 replays data/parity_fixture.save
             through the vendored modules and compares its own canonical
             state hash against the one that file carries. What this build
             supplies to that comparison is its headless half -- the
             fixture and the hash it records --
             and GATE-REPLAY-FIXTURE above is what keeps them fresh, so a
             stale fixture cannot reach the in-engine side quietly.
    NOT RUN  T-SAVE-07 harness compatibility (a Balance Analyst self-play log
             validates and replays as a save). cpp_reference/selfplay.cpp is a
             combat-only 1v1 duel harness that prints a table and emits no
             §4.10 command log, so this ID has no producer at any scope here.
             Part (c), week 4.
    
    Row 10 is a PROPOSED ledger row (§4.11) and has none to flip. The four
    acceptance IDs this build closes are T-SAVE-01, T-SAVE-02, T-SAVE-03 and
    T-SAVE-05; T-SAVE-04 closed at part (a). GATE-REPLAY-* mint no acceptance
    ID, on the GATE-SAVE-PARSE / GATE-AI-SMOKE / GATE-CAP-PARTIAL precedent.
    
    26/36 passed

[Systems Engineer · self-test] BLOCK — GATE-REPLAY-BYTES, GATE-REPLAY-FIXTURE, GATE-REPLAY-FLAGS, GATE-REPLAY-ORDER, T-SAVE-05 caught it. The shape worth naming: T-SAVE-01, T-SAVE-02 and T-SAVE-03 all PASS against this module, because every clause they carry compares two runs that share the defect on both sides. Only the checks that compare against an INDEPENDENTLY re-derived serialisation, or against the state as it stood before the load, can see it. Fixing before hand-off.

[Systems Engineer] re-fed §4.10: every collection walks canonical hex order with ties broken by id, the two turn flags are hashed because a save is accepted mid-turn, and the replay applies to a copy so an illegal command at index k leaves the caller's state byte-identical.
[Systems Engineer] wrote 24377 bytes to build/Replay.cpp (row 10 — Save & replay, part (b) — replayer + state hash)
[Systems Engineer · self-test] row 10 (Save & replay, part (b) — replayer + state hash) GATE PASS — T-SAVE-01, 02, 03, 05 + GATE-REPLAY-* (36/36 passed)
    PASS  GATE-REPLAY-SETUP complete-command-set
    PASS  GATE-REPLAY-SETUP ai-generated-segment-present
    PASS  GATE-REPLAY-BYTES canonical-serialisation-field-order
    PASS  GATE-REPLAY-ORDER storage-order-does-not-leak
    PASS  GATE-REPLAY-FLAGS hasMoved-changes-the-digest
    PASS  GATE-REPLAY-FLAGS hasActed-changes-the-digest
    PASS  GATE-REPLAY-FLAGS moved-and-acted-are-distinguishable
    PASS  GATE-REPLAY-BUILD built-this-turn-changes-the-digest
    PASS  T-SAVE-01 (a) the log replays clean
    PASS  T-SAVE-01 (b) the serialised save loads
    PASS  T-SAVE-01 (c) the loaded log replays clean
    PASS  T-SAVE-01 (d) save -> load -> identical stateHash
    PASS  T-SAVE-01 (e) the file's carried stateHash is the one it describes
    PASS  T-SAVE-01 (f) the log survives the round trip entry for entry
    PASS  T-SAVE-02 (a) both loads succeed
    PASS  T-SAVE-02 (b) both replays succeed
    PASS  T-SAVE-02 (c) the same file loaded twice -> identical hashes
    PASS  T-SAVE-02 (d) identical canonical serialisation, not only digest
    PASS  T-SAVE-02 (e) serialize is stable across a round trip
    PASS  T-SAVE-03 (a) every prefix is a loadable, replayable save
    PASS  T-SAVE-03 (b) each prefix reaches the state replaying it directly does
    PASS  T-SAVE-03 (c) the empty prefix is the initial state
    PASS  T-SAVE-05 (a) the log is refused
    PASS  T-SAVE-05 (b) the refusal names the offending index
    PASS  T-SAVE-05 (c) it names T-SAVE-05 and gives a reason
    PASS  T-SAVE-05 (d) nothing was applied
    PASS  T-SAVE-05 (e) the pre-load state survives, by digest
    PASS  T-SAVE-05 (f) the pre-load state survives, field for field
    PASS  T-SAVE-05 (g) an illegal command at the LAST index refuses the file
    PASS  GATE-REPLAY-FIXTURE (a) the shipped tables and scenario load
    PASS  GATE-REPLAY-FIXTURE (b) the committed fixture is present
    PASS  GATE-REPLAY-FIXTURE (c) it parses as a §4.10 save
    PASS  GATE-REPLAY-FIXTURE (d) the scenario seeds a GameState
    PASS  GATE-REPLAY-FIXTURE (e) its command log replays clean
    PASS  GATE-REPLAY-FIXTURE (f) the carried stateHash is the one it describes
    PASS  GATE-REPLAY-FIXTURE (g) re-emitting reproduces the committed bytes
    
    NOT RUN  T-SAVE-06 stateHash stability across the headless and in-engine
             builds. §4.11 marks it †, it is asserted JOINTLY with T-INT-02,
             which replays IN-ENGINE -- so no headless build closes it, and
             this one does not run the ID. It is NOT RUN here. The bridge
             that landed at UE 0897cb5 replays data/parity_fixture.save
             through the vendored modules and compares its own canonical
             state hash against the one that file carries. What this build
             supplies to that comparison is its headless half -- the
             fixture and the hash it records --
             and GATE-REPLAY-FIXTURE above is what keeps them fresh, so a
             stale fixture cannot reach the in-engine side quietly.
    NOT RUN  T-SAVE-07 harness compatibility (a Balance Analyst self-play log
             validates and replays as a save). cpp_reference/selfplay.cpp is a
             combat-only 1v1 duel harness that prints a table and emits no
             §4.10 command log, so this ID has no producer at any scope here.
             Part (c), week 4.
    
    Row 10 is a PROPOSED ledger row (§4.11) and has none to flip. The four
    acceptance IDs this build closes are T-SAVE-01, T-SAVE-02, T-SAVE-03 and
    T-SAVE-05; T-SAVE-04 closed at part (a). GATE-REPLAY-* mint no acceptance
    ID, on the GATE-SAVE-PARSE / GATE-AI-SMOKE / GATE-CAP-PARTIAL precedent.
    
    36/36 passed

[Director -> Systems Engineer] spec/balance_spec.md handed over (row 10 part (c) — the self-play log producer). T-SAVE-07 asserts that a Balance Analyst self-play log validates and replays as a save file, one format, no dialect drift — and this repo had NO PRODUCER of such a log at any scope: cpp_reference/selfplay.cpp is a combat-only 1v1 duel harness over Combat.h that prints a table and opens no file. A THIRD registry row for one ledger row, because §4.11 gives part (c) its own dependency set — rows 4, 5 and 6, the command set, the match that runs to a result, and the AI that plays it — and folding it into `replay` would put row 6 inside part (b)'s claim. Row 10 is a PROPOSED ledger row and still has none to flip.
[Systems Engineer] wrote 4555 bytes to build/Balance.cpp (row 10 — Save & replay, part (c) — the self-play log producer)
[Systems Engineer] pass 1 authored — three defects, each a mechanical edit of the good module: Attack is tagged by the ACTING unit's hex rather than the TARGET's; Build's `unitId` carries the acting unit rather than the unit BUILT; and a command is logged when PROPOSED rather than when ACCEPTED.

[Systems Engineer · self-test] row 10 (Save & replay, part (c) — the self-play log producer) GATE BLOCK — failing: GATE-BALANCE-TRANSLATE-ATTACK-IS-TARGET-HEX, GATE-BALANCE-TRANSLATE-BUILD-NAMES-THE-UNIT-BUILT, GATE-BALANCE-RUN-ENDS-WITH-A-TIER, GATE-BALANCE-COMMAND-SET-IS-THE-AIS-FOUR, GATE-BALANCE-LOG-HOLDS-ONLY-ACCEPTED-COMMANDS, T-SAVE-07 (6/12 passed)
    PASS  GATE-BALANCE-TRANSLATE-MOVE
    FAIL  GATE-BALANCE-TRANSLATE-ATTACK-IS-TARGET-HEX
    FAIL  GATE-BALANCE-TRANSLATE-BUILD-NAMES-THE-UNIT-BUILT
    PASS  GATE-BALANCE-TRANSLATE-ENDTURN-NAMES-NEITHER
    FAIL  GATE-BALANCE-RUN-ENDS-WITH-A-TIER
          (run stopped: no unit type at index -1)
          (log: 1 commands over a 6-turn cap)
    FAIL  GATE-BALANCE-COMMAND-SET-IS-THE-AIS-FOUR
    FAIL  GATE-BALANCE-LOG-HOLDS-ONLY-ACCEPTED-COMMANDS
    PASS  GATE-BALANCE-ENTRY-TAGS-ARE-THE-LIVE-TURN-AND-SIDE
    PASS  T-SAVE-07 (a) the self-play log VALIDATES as a save file
    FAIL  T-SAVE-07 (b) it REPLAYS to the producing run's canonical state hash
          (replay stopped at 0: no unit type at index -1)
    PASS  T-SAVE-07 (c) NO DIALECT DRIFT: the round trip is byte-identical
    PASS  GATE-BALANCE-DETERMINISM-TWO-RUNS-ARE-IDENTICAL
    
    NOT RUN  T-SAVE-06 — stateHash stability across the headless and in-engine builds. It is row 10's only †, is asserted jointly with T-INT-02, and both are in-engine, which this suite is not. NOT RUN here. The blocker this line used to name is spent — the VENDORED replayer T-INT-02 needs was vendored at f5fdb69, retiring the deferring ruling.
    NOT RUN  T-SAVE-01..05 — closed by parts (a) and (b) at their own commits; this suite re-asserts none of them.
    
    6/12 passed

[Systems Engineer · self-test] BLOCK — GATE-BALANCE-COMMAND-SET-IS-THE-AIS-FOUR, GATE-BALANCE-LOG-HOLDS-ONLY-ACCEPTED-COMMANDS, GATE-BALANCE-RUN-ENDS-WITH-A-TIER, GATE-BALANCE-TRANSLATE-ATTACK-IS-TARGET-HEX, GATE-BALANCE-TRANSLATE-BUILD-NAMES-THE-UNIT-BUILT, T-SAVE-07 caught it. The shape worth naming: T-SAVE-07's clauses (a) and (c) PASS against this module. The FORMAT is agnostic to whether the rules accept a command, so a log full of refused entries still validates and still round-trips byte-identically — only clause (b), which REPLAYS the log, can see it. A suite that read 'validates' as the whole of T-SAVE-07 would have shipped this.

[Systems Engineer] re-fed §4.9 and Save.h: Attack is spelled by TARGET hex, a Build entry names the unit BUILT, and only a command the rules accepted enters the log.
[Systems Engineer] wrote 4607 bytes to build/Balance.cpp (row 10 — Save & replay, part (c) — the self-play log producer)
[Systems Engineer · self-test] row 10 (Save & replay, part (c) — the self-play log producer) GATE PASS — T-SAVE-07 + GATE-BALANCE-* (12/12 passed)
    PASS  GATE-BALANCE-TRANSLATE-MOVE
    PASS  GATE-BALANCE-TRANSLATE-ATTACK-IS-TARGET-HEX
    PASS  GATE-BALANCE-TRANSLATE-BUILD-NAMES-THE-UNIT-BUILT
    PASS  GATE-BALANCE-TRANSLATE-ENDTURN-NAMES-NEITHER
    PASS  GATE-BALANCE-RUN-ENDS-WITH-A-TIER
          (log: 35 commands over a 6-turn cap)
    PASS  GATE-BALANCE-COMMAND-SET-IS-THE-AIS-FOUR
    PASS  GATE-BALANCE-LOG-HOLDS-ONLY-ACCEPTED-COMMANDS
    PASS  GATE-BALANCE-ENTRY-TAGS-ARE-THE-LIVE-TURN-AND-SIDE
    PASS  T-SAVE-07 (a) the self-play log VALIDATES as a save file
    PASS  T-SAVE-07 (b) it REPLAYS to the producing run's canonical state hash
    PASS  T-SAVE-07 (c) NO DIALECT DRIFT: the round trip is byte-identical
    PASS  GATE-BALANCE-DETERMINISM-TWO-RUNS-ARE-IDENTICAL
    
    NOT RUN  T-SAVE-06 — stateHash stability across the headless and in-engine builds. It is row 10's only †, is asserted jointly with T-INT-02, and both are in-engine, which this suite is not. NOT RUN here. The blocker this line used to name is spent — the VENDORED replayer T-INT-02 needs was vendored at f5fdb69, retiring the deferring ruling.
    NOT RUN  T-SAVE-01..05 — closed by parts (a) and (b) at their own commits; this suite re-asserts none of them.
    
    12/12 passed

[Director -> Systems Engineer] spec/driver_spec.md handed over. §4.4 week 1 promises rows 1-3 AND 'Playable via debug commands'; the rows are green and the second half has no artifact yet.
[Systems Engineer] wrote 63606 bytes to build/Driver.cpp (row None — Debug-command driver)
[Systems Engineer] authored — the driver contains NO RULES: reach/path/move delegate to Move.h, damage and counters to Combat.h, stats to Data.h, distance and adjacency to Hex.h, capture/income/build to Economy.h, and now alternation, act flags, start-of-turn repair and the §2.8 result to Turn.h, the opponent's decisions to Ai.h, and the scenario file to Scenario.h. Where row 8 would be needed — how a widget is fed — it refuses rather than deciding.
[Systems Engineer · self-test] no row (Debug-command driver) GATE PASS — GATE-DRV-01..12 (12/12 passed)
    PASS  GATE-DRV-01 reach-equals-module-reachable
    PASS  GATE-DRV-02 move-follows-module-path
    PASS  GATE-DRV-03 forecast-equals-resolution
    PASS  GATE-DRV-04 range-band-is-combats
    PASS  GATE-DRV-05 no-second-source-of-truth
    PASS  GATE-DRV-06 refusal-changes-nothing
    PASS  GATE-DRV-07 determinism
    PASS  GATE-DRV-08 turn-ownership-is-the-turn-modules
    PASS  GATE-DRV-09 standings-and-result-are-the-turn-modules
    PASS  GATE-DRV-10 ai-turn-equals-typed-commands
    PASS  GATE-DRV-11 scenario-load-is-the-scenario-modules
    PASS  GATE-DRV-12 snapshot-is-the-ui-module
    
    12/12 passed
[Systems Engineer] playable artifact built: build/stratocracy_debug.exe (run it, then type 'help')

[Test Engineer] certify_week1 -> WEEK-1 GATE PASS — rows 1-3 (T-HEX-01..07, T-DATA-01..04+06, T-MOVE-01..06) + row 4 (T-FAME-01..09) + row 5 (T-TURN-01..10) + row 6 (T-AI-01..06 + GATE-AI-SMOKE) + row 7's SUBSET of T-SCN and row 8's SUBSET of T-UI + row 10's parts (a), (b) and (c) (T-SAVE-01, 02, 03, 04, 05, 07 + GATE-SAVE-PARSE + GATE-REPLAY-* + GATE-BALANCE-*) (see each row's not-covered list) + the debug driver (GATE-DRV-01..12) | accepted=True
[Test Engineer] acceptance record written to build/acceptance_week1.json.
[Test Engineer] NOT covered by this record: T-DATA-05 — in-editor Unreal Automation (DataTable import parity + EUnitType mirror). §4.11 marks it †; it is not headless and did not run HERE. It is green in the UE project at fed8ae9, over the data bytes of this repo's b1ea992, so it is not outstanding — only out of this gate's reach.
[Test Engineer] NOT covered by this record: T-MOVE-07 — reserved and unwritten, blocked on the Q2 movement-class ruling (§4.7 Stub 3).
[Test Engineer] NOT covered by this record: T-SCN-08 fixtures (a) The Causeway and (b) Longwater March — both need a stretch map authored as a scenario file, which the Director's scope ruling refuses (§2.13.7). Not run, and not replaced by a synthetic map.
[Test Engineer] NOT covered by this record: T-SCN-09's ASSERTING branch — rho asserts hex by hex and the only scenario file that exists declares `none`, which asserts nothing. Its REFUSAL branch did run, off the shipped map's own declaration.
[Test Engineer] NOT covered by this record: T-SCN-11 fixture (c) The Causeway — same scope ruling. Asymmetry (ii) is exercised on the shipped map instead, which is a weaker witness: no gate there fails under the Bridge-free reading.
[Test Engineer] NOT covered by this record: T-SCN-10 — reserved and UNWRITTEN on Q26 (ruled). Nothing is asserted, so nothing is waiting — a different state from T-MOVE-07, which IS blocked.
[Test Engineer] NOT covered by this record: T-UI-03 and T-UI-04 — in-editor Unreal Automation over widget bindings. §4.11 marks both †; they are not headless and did not run. They are now the WHOLE of what row 8 lacks. The harness they needed now exists (UE fed8ae9); what they still lack are the real Stratocracy widgets they assert over. Row 2 no longer shares this posture — its set is complete.
[Test Engineer] NOT covered by this record: T-SAVE-06 — stateHash stability across the headless and in-engine builds. §4.11 marks it †, and it is asserted jointly with T-INT-02. The bridge that landed at UE 0897cb5 replays data/parity_fixture.save in-engine and compares its own canonical state hash against the one that file carries. Every blocker this entry used to name is gone — the in-editor Automation harness landed at UE fed8ae9, §4.10's canonical state hash was built by part (b), and the replayer was vendored at f5fdb69, which retired the ruling that deferred it. It remains the only † of row 10's seven, and it remains uncloseable by any headless suite in this repo.
[Test Engineer] NOT covered by this record: Row 10's Balance module is NOT VENDORED into Source/StratRules/. It is named Balance and not Selfplay, for the reason Balance.h states: cpp_reference/selfplay.cpp is tracked and the build filesystem is case-insensitive, so a Selfplay.cpp beside it is the same file. ue_module/vendored_set.json names it Balance, and that declaration is what T-INT-01 reads. §4.9 enumerates the modules the sync script carries and this is not one of them. The 2026-08-05 ruling recorded here also covered Save and Replay, and for those two it has been SPENT rather than reversed: it deferred vendoring until §4.9 part 2 supplied a bridge consumer, that consumer was built, and both were vendored at f5fdb69 ahead of it. What the bridge does not consume is Selfplay — it is a headless log producer that no in-engine code calls — so for that module the ruling still describes the tree and vendoring would re-date T-INT-01's and T-INT-04's closures for no consumer.
[Test Engineer] NOT covered by this record: GATE-DRV-01..12, GATE-SCN-PARSE, GATE-SCN-HASH, GATE-SAVE-PARSE, GATE-AI-SMOKE and GATE-CAP-PARTIAL gate a tool, two file formats, a smoke path and a partial-capture reading — not a rules system apiece. They are not §4.7 stub IDs, flip no §3 ledger row, and are not GDD acceptance IDs.
[Test Engineer] Q29 refuses a ledger flip on a partial acceptance set, so row 7 stays pending because the Director's scope ruling leaves four of its fixtures without a map. Row 2's in-editor T-DATA-05 pass HAS since run, green in the UE project at fed8ae9 over this repo's b1ea992 data bytes, so its acceptance set is complete. pending because the Director's scope ruling leaves four of its fixtures without a map. Rows 1, 3, 4, 5 and 6 have no missing half and are complete at this commit. Row 10 is a PROPOSED ledger row (§4.11) and has none to flip at all — a different state from a partial pass, and not one Q29 speaks to.

Artifacts in E:\MultiAgent\stratocracy-crew\build/ : Combat.cpp, test_combat.cpp, selfplay.cpp, balance_report.md, acceptance.json, Hex.cpp, Data.cpp, Move.cpp, acceptance_week1.json, run_log.md (written on exit)

==============================================================================
VERDICT: live crew certification -- passed
VERDICT: week 1 (rows 1-8 + 10a) -- passed
==============================================================================
exit code: 0
```
