# SPEC: Combat resolution  (Director → Systems Engineer)

Game: **Stratocracy** — UE5.8 hex turn-based strategy. This is the headless rules
module (§4.1): **zero engine dependencies**, pure C++, deterministic, unit-testable
without launching Unreal.

## Data (from GDD §2.4 units, §2.3 terrain)

Unit fields: `atk, def, hp, hpMax, rangeMin, rangeMax`.

| Unit      | HP | Atk | Def | Range |
|-----------|----|-----|-----|-------|
| Infantry  | 10 | 4   | 2   | 1     |
| Tank      | 20 | 8   | 5   | 1     |
| Artillery | 8  | 10  | 1   | 2–3   |
| Recon/Air | 12 | 5   | 3   | 1     |

Terrain defense (defender's hex), percent: Plains 0, Town 10, Woods 20, Mountains 40.

## Required functions

```
int  resolveDamage(const Unit& attacker, const Unit& defender, int terrainDefPct);
bool defenderCanCounter(const Unit& defender, int distance);
```

## Formula

```
dmg = max(1, round(atk * (hp / hpMax) * (1 - terrainDefPct/100)) - def)
```

## Invariants (Test Engineer asserts each — these are the merge gate)

1. **Determinism** — same inputs → identical result; no unseeded RNG.
2. **Min-damage floor** — a hit always deals ≥ 1 (never 0 or negative).
3. **Terrain reduces damage** — higher defender terrain % → lower damage.
4. **Wounded attacker is weaker** — lower attacker hp/hpMax → lower damage.
5. **Terrain applies to the DEFENDER's hex only** — never the attacker's.
6. **Counter fires only if the attacker is within the defender's range band**
   `[rangeMin, rangeMax]` (and only if the defender survived — survival is the
   caller's check).
7. **Artillery (range 2–3) takes ZERO counter from a range-1 attacker.**  ← the
   classic hallucination gate: an agent that writes `distance <= rangeMax` (dropping
   the `rangeMin` check) passes every other test but fails this one.
8. **Artillery counters within its own band** — distance 2 or 3 → counter eligible.

## Determinism / constraints

Pure function of inputs. No I/O, no globals, no engine types. Must compile with a
plain C++17 compiler (`g++`/`clang++`) — no Unreal, no third-party libraries.

## Acceptance

`T-COMBAT-01..08` must all pass before the module is accepted (Test Engineer gate).
