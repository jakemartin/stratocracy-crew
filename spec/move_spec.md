# SPEC: Movement & pathfinding  (Director → Systems Engineer)

Game: **Stratocracy** — UE5.8 hex turn-based strategy. This is the headless rules
module (§4.1): **zero engine dependencies**, pure C++, deterministic, unit-testable
without launching Unreal.

This is **§4.7 SPEC STUB 3**, §4.11 build-order row 3, and the third of the three
rows §4.4 week 1 owes. It **depends on rows 1 and 2** — it consumes `hexDistance`,
`neighbors` and the canonical order from `Hex.h`, and the move costs from
`data/terrain.csv` via `Data.h`. It defines no cost of its own.

Row 3 is also the row the critical path runs through: §4.11 states
**1 → 3 → 4 → 5 → 6/8**, and row 7's priced half (T-SCN-04, 06, 08, 11) cannot close
until this row does.

## What the player is promised (§2.5)

> The player selects a unit and every hex it can truly reach this turn lights up,
> terrain costs already accounted for — **the highlight is the real move set, not an
> estimate.** Clicking a lit hex sends the unit by the cheapest route. Only one unit
> fits in a hex, so lanes, chokepoints, and blocking are part of the plan. Zones of
> control are cut from the prototype.

T-MOVE-01 is the assertion of the bolded clause, and it is why the gate computes the
reachable set a second, independent way rather than re-running the module's own
search and comparing it to itself.

## Inputs

Unit `{move, moveClass}`; terrain move costs (§2.3 via Stub 2); current occupancy;
start hex. `moveClass` is **reserved on Q2** and is not read (see `data_spec.md`).

## Transition

Reachable set + cheapest paths via **Dijkstra over terrain cost** (§4.1); executing a
move relocates the unit along the chosen path (§2.5).

**Cost is charged on ENTERING a hex**, never on leaving it, so the start hex costs 0
and a path's cost is the sum of its steps' destination costs. `MoveCost == 0` is the
impassable sentinel (§4.8) and no path may enter such a hex.

## Two stated readings (neither is invented; both are the conservative branch)

1. **Occupancy — Q3 is open.** §2.5 pins one-unit-per-hex for *ending* a move and is
   silent on pathing *through* a friendly-occupied hex. The register's assumption in
   force is the conservative one: **occupied hexes block pathing entirely, friendly
   or not.** That is what ships and what T-MOVE-03 asserts. A later Q3 ruling
   loosens behaviour rather than invalidating a passing gate.
2. **The start hex is in the reachable set, at cost 0.** Occupancy excludes *other*
   units' hexes; a unit is not blocked by itself, and T-MOVE-01's "cheapest path cost
   ≤ Move" is satisfied at 0 by definition. Standing still is the null move.

## Required functions

```
struct strat::Board { MapBounds bounds; std::vector<int> terrain; std::vector<int> occupant; };
    // terrain[]  — index into the loaded TerrainDef table, one per hex, offset-indexed row*cols+col
    // occupant[] — unit id, or OCCUPANT_NONE (−1) for an empty hex

std::vector<strat::ReachEntry> strat::reachable(const Board&, const std::vector<TerrainDef>&,
                                                const Hex& start, int move);
bool strat::findPath(const Board&, const std::vector<TerrainDef>&, const Hex& start,
                     const Hex& goal, int move, std::vector<Hex>& outPath, int& outCost);
```

`reachable` returns entries **sorted in canonical hex order** (r asc, then q asc).
`findPath` returns the path **including both endpoints**, or `false` if the goal is
not reachable within `move`.

## Invariants (Test Engineer asserts each — these are the merge gate)

- **T-MOVE-01** — reachable set is **exact**: a hex is in the set ⟺ its cheapest path
  cost ≤ Move — "the real move set, not an estimate" (§2.5).
- **T-MOVE-02** — costs per §2.3: Plains 1, Woods 2, Mountains 3, Town/Bridge/Factory
  1; Water impassable to land — a land path across a Water span exists ⟺ it crosses
  on **Bridge** hexes (§2.3, "the only hex a land unit crosses Water").
- **T-MOVE-03** — occupancy: a move never ends on an occupied hex (§2.5, one unit per
  hex). Pass-through of friendly-occupied hexes is **parameterized on the Q3
  ruling**; until ruled, the gate asserts the conservative reading (occupied hexes
  block pathing entirely).
- **T-MOVE-04** — the executed path is minimal-cost; ties between equal-cost paths are
  broken by **canonical hex order**, so the route is reproducible.
- **T-MOVE-05** — no zones of control: moving adjacent to an enemy costs nothing extra
  and freezes nothing (§2.5 — ZOC is cut).
- **T-MOVE-06** — determinism: same state → identical reachable set and identical path.
- *(**T-MOVE-07 reserved**: Recon's "ignores some terrain cost" (§2.4) — blocked on
  the Q2 movement-class ruling. **No gate is written until the rule exists**, and this
  file does not write one.)*

### The tie-break, stated exactly

"Broken by canonical hex order" needs one reading to be assertable, and this is it:
among all minimal-cost paths to a hex, the chosen path is the one that is
**lexicographically smallest under canonical hex order, read start → goal**. The
search settles nodes by `(cost, path)` under that comparison, so the first settle is
provably the winner and no later relaxation can change it. This is a stronger
statement than a local "prefer the smaller predecessor" rule, and it is the one the
gate asserts.

## Determinism / constraints

Pure; **all tie-breaks canonical**; no unordered containers in any path that reaches
an output, no pointer-value comparisons, no floating point. Must compile with a plain
C++17 compiler (`g++`/`clang++`/`cl`) — no Unreal, no third-party libraries.

## Acceptance

`T-MOVE-01..06` must all pass before the module is accepted (Test Engineer gate).
`T-MOVE-07` is reserved and unwritten.
