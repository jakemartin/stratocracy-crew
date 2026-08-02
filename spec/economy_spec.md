# SPEC: Capture & Fame economy  (Director → Systems Engineer)

Game: **Stratocracy** — UE5.8 hex turn-based strategy. Headless (§4.1): **zero
engine dependencies**, pure C++17, deterministic.

This is **§4.7 SPEC STUB 4**, §4.11 build-order **row 4**. It depends on row 3
and is the next link on the critical path `1 → 3 → 4 → 5 → 6/8` — rows 5, 6 and 8
all queue behind it.

Four of its nine invariants encode a **ruled** open question (Q4, Q5, Q6, Q8).
Where a rule here looks arbitrary it is not: it is a Director ruling, cited.

## It owns the economy, not the turn

Row 5 owns the turn loop. This module therefore **never advances a turn** and
never decides whose turn it is. It exposes the transitions a turn loop calls —
`accrueIncome` at a turn's start, `resolveBuilds` when a build may spawn,
`captureTick` when holding is evaluated — and takes the turn number as an
**argument**. T-FAME-02's "no accrual on turn 1" is asserted by passing turn 1,
not by owning a counter.

Likewise it stores no units and no board. Occupancy, adjacency and unit identity
arrive as **caller-supplied facts**, exactly as `Combat.h::repairAmount` takes
`onOwnedObjective` and `enemyAdjacent`. That is what lets row 4 land before row 5.

## Inputs

Game state; commands `Build{factoryHex, unitId}` and `Capture{unit}`; the §2.7
income and award values; §2.4 costs via Stub 2.

## Required functions

```
void strat::initSide(EconomyState&, int side, int startingFame);
int  strat::accrueIncome(EconomyState&, const std::vector<TerrainDef>&, int side, int turnNumber);
bool strat::queueBuild(EconomyState&, const std::vector<UnitDef>&, int side,
                       const Hex& factoryHex, int defIndex, std::string& err);
std::vector<strat::SpawnResult> strat::resolveBuilds(EconomyState&, const MapBounds&,
                                                     const std::vector<Hex>& occupied);
std::vector<Hex> strat::captureTick(EconomyState&, const std::vector<CaptureOccupant>&, int side);
int  strat::killAward(const UnitDef& victim, bool victimIsFlag);
void strat::awardKill(EconomyState&, int killerSide, const UnitDef& victim, bool victimIsFlag);
```

## Invariants (the merge gate)

- **T-FAME-01** — single pool (§2.7): income, kill awards and spending all mutate
  one per-side `fameTotal`; combat awards **also** accrue a separate `fameCombat`
  counter (the §2.8 tiebreak criterion-1 sort key); **passive income never touches
  `fameCombat`**.
- **T-FAME-02** — income: each held factory pays +100/turn, each held town
  +25/turn (§2.7); accrues at the **start** of the owner's turn and is spendable
  in that same turn, with **no accrual on turn 1** — a side's turn-1 buying power
  is its starting Fame alone. §2.9's handicap moves the **player's** opening Fame
  only (350 Easy, 100 Hard) while the AI opens on 200 at every tier, so the gate
  asserts **each side's configured value and never a literal 200** (Q8, ruled).
- **T-FAME-03** — build deducts the exact §2.4 cost (Infantry 100, Recon 150,
  Artillery 200, Tank 300); refused if unaffordable; `fameTotal` is never negative.
- **T-FAME-04** — spawn on the factory hex if free, else an adjacent free hex,
  else the build **waits** (§2.7). One build per factory per turn; a waiting build
  **holds that factory's slot** until it spawns; Fame is **committed at queue
  time, never at spawn time, and is not refundable** (Q8, ruled).
- **T-FAME-05** — capture: Infantry only (§2.7, §2.4); completes after N turns of
  holding (N = 1 on the shipped scenario; per-scenario data); progress is
  **tile-held** and **resets to zero** when the capturing Infantry leaves the hex
  or dies, and **never transfers** to another unit (Q4, ruled).
- **T-FAME-06** — a captured objective's income flips to the new owner (§2.7).
- **T-FAME-07** — kill awards are exactly half the victim's §2.4 cost — Infantry
  50, Recon 75, Artillery 100, Tank 150 (Q5, ruled). A flag kill pays a flat 500
  and ends the match; the flag award **replaces** the ordinary award rather than
  stacking, so a flag Tank pays **500, not 650** (Q5). **No undamaged-strike bonus
  exists** — cut, not priced (Q6, ruled) — so the gate asserts its **absence** from
  every award.
- **T-FAME-08** — no Fame cap: `fameTotal` is unbounded; deployment is throttled
  by board space only (§2.7).
- **T-FAME-09** — determinism: same state + command → identical Fame deltas and
  identical state.

### Two stated readings

1. **Which adjacent hex a spawn takes.** §2.7 says "an adjacent free hex" and does
   not say which. T-FAME-09 requires a reproducible answer, so the spawn takes the
   **canonically smallest free neighbour** (r asc, then q asc — the §4.7 shared
   convention). This is a documented choice, not an invented rule; the GDD
   delegates it by leaving it unstated while demanding determinism.
2. **"One build per factory per turn" is enforced as one *pending* build per
   factory.** A waiting build holds the slot (§2.7), so the slot is occupied until
   it spawns; a second queue attempt at the same factory is refused. That is the
   same rule read through T-FAME-04's holding clause rather than a second rule.

## Determinism / constraints

Pure state transitions; **no RNG anywhere in the economy**; no clock; no I/O. Must
compile with a plain C++17 compiler.

## Acceptance

`T-FAME-01..09`. Row 4's ledger row flips only on the full set at one commit (Q29).
