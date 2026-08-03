# SPEC: Debug-command driver  (Director → Systems Engineer)

Game: **Stratocracy** — UE5.8 hex turn-based strategy. Headless (§4.1): **zero
engine dependencies**, pure C++17, deterministic.

This is **§4.4's week-1 goal "Playable via debug commands"**, the half of week 1
that the §4.11 row flips did not close. It is **not** a §4.7 stub and **not** a
§3 ledger row: it builds no rules system, so it has no acceptance IDs in the GDD
and does not move the 69-ID count. Its own gate IDs are named `GATE-DRV-*` rather
than `T-*`, following `GATE-DATA-HARDFAIL`, so no reader mistakes them for
document-specified acceptance IDs.

## The one binding constraint

**The driver contains no rules.** Every rule decision delegates to a module that
is already gated:

| Decision | Delegated to | Gated by |
|---|---|---|
| adjacency, distance, bounds, canonical order | `Hex.h` | T-HEX-01..07 |
| unit and terrain values | `Data.h` over `data/*.csv` | T-DATA-01..04, 06 |
| what a unit can reach, and by which route | `Move.h` | T-MOVE-01..06 |
| damage, counter eligibility, repair amount | `Combat.h` | T-COMBAT-01..10, T-REPAIR-01..07 |
| capture, income, build cost and spawn, kill awards | `Economy.h` | T-FAME-01..09 |
| alternation, act flags, the start-of-turn moment, the §2.8 result | `Turn.h` | T-TURN-01..09 |
| what the opponent does with its turn | `Ai.h` | T-AI-01..06 |

If a question is not answerable by one of those seven, **the driver refuses the
command** rather than deciding it. That is what keeps a debug tool from quietly
becoming a second rules implementation — the failure §4.9's T-INT-03 exists to
prevent one layer up, and the reason `GATE-DRV-01`, `-02`, `-05`, `-08`, `-09` and `-10`
compare the driver's output against direct module calls rather than against
expected values.

## What it deliberately does NOT do

§4.11 rows 7–8 hold no code, so the driver exposes none of it and says so:

- **No scenario file** (row 7). Boards come from built-in fixtures and from
  `place`/`remove` commands. The driver defines **no file format**, so nothing
  here pre-empts Stub 7 or the §4.10 save format.
- **No UI** (row 8). Text in, text out.

**Two things it holds that no module owns yet, and labels as such.** `flag <side>
<id>` is a **debug designation** standing in for Stub 7's `isFlag` placement field
(row 7, unbuilt; Q10 open on exactness) — the human names the flag unit and the
driver never picks one, so an undesignated side simply has no flag to lose.
`turn <n>` remains the **debug setter** it was before row 5, and applies only when
no match is running; once `match` starts one, `Turn.h` owns the number and the
setter is refused.

**With no match running the board is a free sandbox**, exactly as it was before
row 5: no side owns the turn, either side may act, and a unit may act repeatedly.
That is deliberate — it is what `place`/`hp`/`remove` debugging needs — and it is
why `GATE-DRV-01..07` are unchanged by rows 5 and 6.

**The start of a turn runs the whole sequence.** `Turn.h` defines *when*; the
driver then calls, in order, `applyStartOfTurnRepair`, `accrueIncome` and
`captureTick` for the active side — the calls `spec/turn_spec.md` says the caller
must make, since the turn module accrues no income and ticks no capture itself.
`income <side>` and `capture <side>` remain as manual commands for sandbox use.

It also clears `builtThisTurn` there. That is **bookkeeping, not a rule**: it is
handed to the AI as a board fact and gates no player command — see the first
change request in `spec/ai_spec.md`, which is why the per-turn half of §2.7's
"one build per factory per turn" currently has no enforcer.

## Command set

```
help                                  list commands
map                                   render terrain + unit positions
units                                 id, side, type, hex, hp for every unit
fixture <name>                        load a built-in board (fixture list = names)
place <side> <Type> <col> <row>       add a unit (side 0|1; Type per data/units.csv)
remove <id>                           delete a unit
hp <id> <value>                       set current HP (debug: set up a wounded case)
dist <c1> <r1> <c2> <r2>              hexDistance between two hexes
reach <id>                            the T-MOVE-01 reachable set, with costs
path <id> <col> <row>                 the cheapest route, without moving
move <id> <col> <row>                 execute the move along that route
forecast <atkId> <defId>              predicted damage + whether a counter fires
attack <atkId> <defId>                resolve it, apply damage and any counter
repair <id> <owned 0|1>               apply repairAmount; ownership is an argument
fame                                  fameTotal and fameCombat per side
objectives                            every objective, its owner, income, progress
turn <n>                              set the sandbox turn number (no match only)
income <side>                         accrue start-of-turn income for a side
build <side> <Type> <col> <row>       queue a build at a held factory, then spawn
capture <side>                        evaluate holding for a side
match <firstSide> <turnCap>           start a match; the turn loop takes over
endturn                               end the active side's turn
standings                             the §2.11.4 scoreboard rows + the leader
result                                the recorded tier, cause and winner
flag <side> <id>                      designate a side's flag unit (debug)
ai                                    the AI plays the active side's whole turn
ai buildlist <Type>...                set the §2.9 buildlist the AI builds from
quit                                  exit
```

`forecast` is §2.11.3's attack forecast at the text layer: it must predict
**exactly** what `attack` then applies (`GATE-DRV-03`), because §2.6 promises
"the forecast the player sees is exactly what resolves, with no hidden roll".

Coordinates are **odd-r offset `(col, row)`** at the command surface — what a
human reads off the rendered map — and are converted by `Hex.h`'s
`offsetToAxial` on the way in. No axial coordinate is ever typed or printed.
This is the §4.7 convention: no authored or human-facing layer stores axial.

## Invariants (the merge gate)

- **GATE-DRV-01** — `reach` equals `Move.h::reachable` called directly: same
  hexes, same costs, same canonical order.
- **GATE-DRV-02** — `move` relocates the unit to exactly `findPath`'s endpoint by
  exactly its path, and updates occupancy; a refused move changes nothing.
- **GATE-DRV-03** — **forecast equals resolution.** The damage `forecast`
  predicts is the damage `attack` applies, and its counter prediction matches
  what fires — for every legal attacker/defender pair on the fixture.
- **GATE-DRV-04** — range: `attack` is refused unless `hexDistance` is inside the
  attacker's `[rangeMin, rangeMax]`, and the counter fires only when
  `defenderCanCounter` says so at that same distance.
- **GATE-DRV-05** — no second source of truth: the terrain defense the driver
  applies equals `Data.h`'s `defensePct` for that hex, and every distance it
  reports equals `hexDistance`.
- **GATE-DRV-06** — refusal safety: an illegal or malformed command leaves the
  session state hash unchanged and returns a reason; no partial application.
- **GATE-DRV-07** — determinism: the same command sequence from the same fixture
  produces byte-identical output.
- **GATE-DRV-08** — turn ownership is `Turn.h`'s: with no match running either
  side moves freely and a unit may move twice; once a match runs, every refusal
  the driver issues agrees with `canAct`/`markActed`, `activeSide` is read rather
  than counted, and a refused action leaves the state hash unchanged.
- **GATE-DRV-09** — `standings`' leader line is `resolveAtCap` on the live board
  and the recorded result is `resolveAtCap` on the board the match ended on, so
  what is displayed during a match and what decides it cannot disagree; a
  designated flag's death ends the match through `checkImmediate`, with no
  tiebreak key read.
- **GATE-DRV-10** — the `ai` command adds nothing of its own: replaying by hand the
  command lines it printed reaches a byte-identical state hash, so the AI can reach
  no state a typed command cannot. With no match running it refuses and changes
  nothing.

## Determinism / constraints

Pure over its inputs; no RNG; no clock; no filesystem access except the
`data/*.csv` load at startup. Must compile with a plain C++17 compiler.

## Acceptance

`GATE-DRV-01..10`. This suite gates a debug tool, not a rules system, and no §3
ledger row flips on it.
