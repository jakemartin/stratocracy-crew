# SPEC: Turn loop & win / tiebreak  (Director → Systems Engineer)

Game: **Stratocracy** — UE5.8 hex turn-based strategy. Headless (§4.1): **zero
engine dependencies**, pure C++17, deterministic.

This is **§4.7 SPEC STUB 5**, §4.11 build-order **row 5**. It depends on row 4
and on verified Combat/Repair at `5ffa8d6`, and it is the critical path's sole
next link on `1 → 3 → 4 → 5 → 6/8` — rows 6 and 8 queue behind it.

Four of its nine invariants encode §2.8's tiebreak apparatus exactly
(T-TURN-04/05/06/07), and two rest on ruled questions (Q7 turn cap, Q29 ledger
flip). Where a rule here looks arbitrary it is not: it is §2.8's procedure or a
Director ruling, cited.

## It owns the turn — and that is the whole point of it

Rows 3 and 4 **declined** the turn. Row 4 takes the turn number as an argument
and never advances it; row 3 knows nothing about whose turn it is. Every
turn-ownership question those rows deferred is concentrated here:

- who moves now, and in what order sides alternate (§2.1);
- which units may still act this turn (§4.9's per-unit `hasActed`);
- when a turn starts, which is the moment repair fires (§2.7);
- when the match is over, and with what grade (§2.8).

What it still does **not** own is the board. It stores no units, no hexes and no
Fame. Everything it needs about the board arrives as a caller-supplied
`BoardSnapshot`, exactly as `Combat.h::repairAmount` takes `onOwnedObjective`
and `enemyAdjacent` and `Economy.h` takes occupancy. Its inputs are the same
quantities §2.11.4's standings scoreboard already displays — the tiebreak adds
no new state, only an ordering over existing state (§2.8).

It also does **not** accrue income. §4.7 Stub 5's transition list is
alternation, win/loss/draw evaluation, and start-of-turn repair; income is row
4's `accrueIncome`, which the caller invokes at the start-of-turn moment this
module defines.

## Inputs

Game state; per-unit move and act flags — **TWO flags per unit, not one**
(T-TURN-01); the per-factory record of builds taken this turn (T-TURN-10); the
turn counter and cap (Q7, stored in the scenario file, Stub 7); commands incl.
`EndTurn{}`.

## Required functions

```
bool strat::initMatch(TurnState&, int firstSide, int turnCap, std::string& err);
strat::MatchResult strat::checkImmediate(TurnState&, const BoardSnapshot&);
strat::MatchResult strat::beginTurn(TurnState&, const BoardSnapshot&);
std::vector<strat::RepairApplied>
    strat::applyStartOfTurnRepair(TurnState&, const std::vector<RepairSubject>&);
bool strat::canAct(const TurnState&, int unitId, int unitSide);
bool strat::markActed(TurnState&, int unitId, int unitSide, std::string& err);
bool strat::hasActed(const TurnState&, int unitId);
bool strat::canMove(const TurnState&, int unitId, int unitSide);
bool strat::markMoved(TurnState&, int unitId, int unitSide, std::string& err);
bool strat::hasMoved(const TurnState&, int unitId);
bool strat::hasBuiltThisTurn(const TurnState&, const Hex& factory);
bool strat::canBuildAt(const TurnState&, const Hex& factory, int side);
bool strat::markBuilt(TurnState&, const Hex& factory, int side, std::string& err);
strat::MatchResult strat::endTurn(TurnState&, const BoardSnapshot&);
strat::MatchResult strat::resolveAtCap(const BoardSnapshot&);
int  strat::tierRank(ResultTier);
std::string strat::stateDigest(const TurnState&);
```

## Invariants (the merge gate)

- **T-TURN-01** — strict alternation; each unit carries **TWO INDEPENDENT
  flags** in its own turn — one for its move, one for its act — and may move at
  most once **AND** act at most once, **IN EITHER ORDER** (Director ruling,
  2026-08-03). Moving never consumes the act and acting never consumes the move.
  The per-unit sequence is §2.1's to state; this gate asserts the two flags and
  reads no ordering constraint into them. The owner takes its units in any order
  it chooses (§2.1). A unit of the inactive side may neither move nor act. The
  gate asserts: **(a)** a move-then-attack by one unit COMPLETES; **(b)** an
  attack-then-move by the same unit COMPLETES, leaving both of that unit's flags
  spent exactly as (a) leaves them — the two orders are **not** state-equivalent,
  since the two attacks are made from different hexes, so the assertion is on the
  flags and not on a state match; **(c)** a second move, or a second act, is
  REFUSED whichever of the two the unit spent first; **(d)** a refused command
  changes nothing (§4.9) — it sets neither flag and moves no unit; and **(e)**
  BOTH flags clear at the start of the owner's turn, the same moment T-TURN-10's
  per-factory build allowance renews.
- **T-TURN-02** — flag death ends the match **immediately** — Decisive win for
  the killer, loss for the owner (§2.8). Once recorded, the match is over: no
  turn begins, no unit acts, and the cap tiebreak is **never evaluated**
  (§2.8 T-CAP-01).
- **T-TURN-03** — territorial domination: controlling **every factory on the
  map** at the **start of your turn** ends the match immediately, ranked
  **Decisive** (§2.8; **factories only, towns excluded**).
- **T-TURN-04** — at the turn cap, the attrition tiebreak resolves in the exact
  §2.8 order: **combat Fame → objectives held → surviving HP → draw**. Higher
  wins at the first key that differs; the winner's grade is **Marginal**. The cap
  itself is **per-scenario data** read from `turnCap` (Q7, ruled) — the module
  holds **no literal 20**.
- **T-TURN-05** — mutual-passivity guard: both sides' `fameCombat == 0` at the
  cap → **immediate draw**, with **no fall-through** to objectives held (§2.8).
- **T-TURN-06** — criterion 2 (objectives held) is reached **only** when both
  sides fought and their `fameCombat` is equal (§2.8). Equivalently: a result
  decided by key 2 or key 3 always has equal, **nonzero** combat Fame on both
  sides.
- **T-TURN-07** — result tiers are **categorical**: Decisive > Marginal > Draw,
  regardless of Fame totals — Fame is only the sort key *inside* criterion 1
  (§2.8). A Decisive win on a small tally outranks a Marginal win on a large one.
- **T-TURN-08** — repair fires at the **start of the unit's turn** exactly when
  the verified `repairAmount` says so (owned Town/Factory, no adjacent enemy,
  +25% max HP floored, min 1, capped — T-REPAIR-01..07 @ `5ffa8d6`). This gate
  asserts the turn loop **calls it at the right moment with the right board
  facts, nothing more**: every amount equals a direct `repairAmount` call on the
  same unit and the same two board facts, it fires only for the **active** side,
  and it fires only in the start-of-turn phase. The heal values themselves are
  not re-asserted here — they are already green at `5ffa8d6`.
- **T-TURN-09** — determinism: the same command sequence from the same scenario
  → identical result tier and identical state at every step.
- **T-TURN-10** — **one build per factory per turn**, player and AI alike (§2.7;
  Q8(b), ruled): a Build naming a factory that has already taken its build this
  turn is **REFUSED**, and `fameTotal` is unchanged by the refusal — Fame is
  committed at queue time (Q8(c)) and a refused Build never queues, so the caller
  must consult this **before** it charges anything. BOTH dispositions of the
  first build count against the allowance: one that spawned immediately, and one
  that waits and holds the factory's slot (T-FAME-04). The slot and this
  allowance are **two rules, not one**. **The allowance renews at the start of
  the OWNER's turn** — the same moment the two per-unit flags clear, and *not*
  per round: a factory that changes hands mid-round arrives with a clear record
  for its new owner, which is the case the two boundaries disagree on. This ID
  lives here rather than in Stub 4 because the check needs the turn number, and
  the economy module takes the turn as an argument rather than owning it (§3).

### Two stated readings

Both are **documented choices**, not rules. The GDD delegates each by leaving it
unstated while requiring a determinate answer; each names the section it is read
off, and neither adds a rule the GDD does not have.

1. **What the turn counter counts.** Two places keep the turn number and whose
   turn it is as **separate fields**: §4.10's canonical state hash serializes
   `GameState` in a fixed field order beginning "turn counter, side to move",
   and §4.7 Stub 8's UI snapshot carries
   `match {turn, turnCap, sideToMove, resultTier or null}`. So the turn number
   is not per-side; were it per-side, the side to move would be derivable from
   it and would not need a field of its own. The reading: **a turn is one full
   I-GO-U-GO round** (§2.1), shared by both sides, and it advances when every
   side has ended its turn. §2.7 reads the same way — "both players draw income
   from **turn 2**" names one number for both sides.
2. **When the cap fires.** §2.11.4 displays the counter as `N / turnCap` and
   §2.13.2 ships `20`, so turn `turnCap` is a **playable** turn. The reading:
   the tiebreak resolves at the **end** of round `turnCap`, not at its start —
   the last side to move in that round ends its turn and the match resolves.

## Determinism / constraints

Pure state machine; **no RNG anywhere**; no clock; no I/O. `stateDigest` is an
order-independent, platform-independent digest of the turn state — §4.10's save
hash is taken from this state alongside the other modules' (row 10, unbuilt).
Must compile with a plain C++17 compiler.

## Out of scope, by design

- **Which unit is the flag.** `isFlag` is a Stub 7 scenario placement field (row
  7, **built** at `9086d6a` on a partial pass) and Q10 is open on exactness, so
  the module takes `flagAlive` per side as a snapshot fact and designates
  nothing. Row 7 landing does not change that: what this module declines is the
  designation, not the field's existence.
- **Both flags down at once.** A legal match cannot reach it — the first death
  ends the match — so the module refuses to grade that snapshot rather than
  inventing a rule for an unreachable state.
- **Income accrual, AI turns, and the scenario file.** Rows 4, 6 and 7.

## Acceptance

`T-TURN-01..10`. Row 5's ledger row flips only on the full set at one commit
(Q29), which since 2026-08-03 is read per acceptance ID as well as per row.
