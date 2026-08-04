# SPEC: UI binding contract  (Director → Systems Engineer)

Game: **Stratocracy** — UE5.8 hex turn-based strategy. Headless (§4.1): **zero
engine dependencies**, pure C++17, deterministic.

This is **§4.7 SPEC STUB 8**, §4.11 build-order **row 8** — the last unbuilt
link on the critical path. It depends on rows 5 and 7, both landed, and reads
rows 1–4 besides. Nothing here is blocked.

## What this row is, and what it is not

**This is the contract for how every widget is fed. It is not layout and not
visual design** — that is §2.11's lane. Widgets bind to a view-model snapshot
plus the §4.9 event list and **hold no rules state** (§4.1).

So the module owns **no rules and no board**. Every value in the snapshot is
produced by a module that already owns it, and every query delegates. A number
this module computes for itself is a defect even when it is the right number,
because the whole point of the row is that the screen cannot disagree with the
simulation.

## The Director's scope ruling — read this before scoping any fixture

**Row 8's acceptance set is split across two harnesses and this build is the
headless one.**

- **T-UI-01** and **T-UI-02** are headless: the queries are headless functions.
  They run here.
- **GATE-CAP-PARTIAL** is headless, **on the snapshot rather than on a widget**
  — which is why it carries no `†` and does not stand down if the editor pass
  does. It runs here.
- **T-UI-03** and **T-UI-04** are **in-editor Unreal Automation** over widget
  bindings, and are marked `†` in §4.11. **No in-editor pass exists at this
  commit**, so neither runs.

**The consequence is stated, not hidden: row 8's ledger row does NOT flip.**
Q29 requires the full acceptance set at one commit and is applied per
acceptance ID as well as per row, so a build that closes three of five records
a **partial pass** and stays `*pending*` — the posture row 2 holds on T-DATA-05
and row 7 holds on T-SCN-08/09/11. Do not write a flip, do not imply one, and
do not describe the suite as complete.

**Report both unrun IDs by name, with their reason**, the way row 7's record
reports its four fixtures. A suite that quietly omits an ID reads as a complete
pass.

## The snapshot

§4.7 Stub 8 states the field list. **Read it there and follow it exactly.** Two
things about it are load-bearing:

1. **`hasMoved` and `hasActed` are TWO INDEPENDENT per-unit fields.** This is
   the drift the GDD half of this row repaired: the stub named `hasActed` alone
   until 2026-08-04, while `T-TURN-01` has asserted two independent flags since
   row 5's rebuild at `6ccd40b`. One field cannot express a unit that has spent
   exactly one of them. Read them from `TurnState`'s two sets through
   `Turn.h::hasMoved` / `Turn.h::hasActed` and never from each other.
2. **Neither flag is §2.11.1's DONE bit.** DONE is the selection machine's own
   per-unit bit and every §2.11 surface reading *has not acted* binds to it, not
   to these. **The DONE bit is not a snapshot field** — it is derivable from
   neither flag nor from any pair of them, since Wait and RMB-in-MOVED both reach
   it without spending the act flag. It lives in the **presentation block**
   (below), which is the other half of the view model.

**The three change requests this spec filed on 2026-08-04 were RULED the same
day, and all three are now in the stub.** They are no longer gaps to leave open:

- the **per-factory group** `{hex, owner, hasBuiltThisTurn, buildWaiting,
  spawnBlocked}` — ruled C and E. `hasBuiltThisTurn` and `buildWaiting` mirror
  state the module already holds (`Turn.h::hasBuiltThisTurn`,
  `EconomyState::pending`); `spawnBlocked` is DECLARED DERIVED and is **not** the
  same fact as `buildWaiting`. The case that separates them, and the reason the
  field was ruled, is a **boxed-in factory with nothing queued**: blocked, not
  waiting. §2.11.5's footer needs exactly that state;
- §2.11.2's **income rate**, as per-side `incomePerTurn` — ruled G. DECLARED
  DERIVED, and it is the **STANDING** rate: on turn 1 it reads what the holdings
  will pay at the start of turn 2, **not** the 0 that Q8(a) pays. Do not
  implement it by calling `accrueIncome`; that function answers a different
  question and returns 0 on turn 1;
- the **DONE bit**, which went to the presentation block rather than the
  snapshot — ruled A.

**Still: do not add a field the stub does not name.** `T-UI-05` clause (c) now
enforces that mechanically rather than by instruction — a field with no contract
row fails the invariant.

**The presentation block** is the view model's second half: **two** per-unit
members, each with a named owner that is not a widget and not this module.
`done` is owned by §2.11.1's selection machine. `lockedThisTurn` is owned by the
guidance layer. **Their lifecycles differ and one must not be copied from the
other**: `done` clears when the owner's next turn begins, while `lockedThisTurn`
clears when beat 1a retires — which §2.11.6-B puts at the marked Infantry's move
completing, *inside* turn 1. This module declares the block and **does not fill
it**: `buildUiSnapshot` produces the snapshot only.

`isGuidedMarked` is a **per-unit snapshot field** and is DECLARED DERIVED
(ruled J — it is a predicate over a placement, not a copy of one, so it is not a
mirror). It is true exactly on the placement the scenario's
`guidedOpening.infantry` names for that unit's seat. **Read it from the unit's
placement, never from its current hex**: it is a property of the placement and
does not move when the unit does, and beat 1a's whole content is that the marked
unit moves.

`captureProgress` is a **per-unit** field in the stub while `Economy.h` holds
progress on the **tile**, naming the unit that accumulated it. That is not a
contradiction: progress is tile-held, resets to zero when the capturing Infantry
leaves or dies, and never transfers (Q4, T-FAME-05), so exactly one unit can
carry a non-zero value and the per-unit field expresses the tile's state without
loss. Derive it; do not store a second copy.

## Required functions

```
strat::UiSnapshot  strat::buildUiSnapshot(const UiWorld&);
std::vector<strat::ReachEntry>
                   strat::uiReachable(const UiWorld&, int unitId);
strat::UiForecast  strat::uiForecast(const UiWorld&, int attackerId,
                                     const Hex& defenderHex);
```

`UiWorld` is the caller's bundle of the module states this row reads — the
`Board`, the unit list, `EconomyState`, `TurnState` and the two loaded tables.
It is an **input**, not state this module owns.

**Add no third query.** T-UI-04 names a buildlist "derived from the four Stub-2
unit rows plus current fameTotal", but whether that reaches the UI as a snapshot
field or a query is stated nowhere, and T-UI-04 does not run in this harness. If
a shape you need is not in the document, **file a change request rather than
choosing one** — a query invented here would pre-empt a Director ruling and then
be gated in-editor as though the document had asked for it.

## Invariants (the merge gate)

`T-UI-01`, `T-UI-02`, `T-UI-05` and `GATE-CAP-PARTIAL` are stated in full in
§4.7 Stub 8. **Read them there rather than from a paraphrase here.** What
follows is only what this build must not get wrong.

**T-UI-01 — "the same call" is structural, not an assertion.** The forecast must
be produced by `Combat.h::resolveDamage` and `Combat.h::defenderCanCounter`,
verified at `5ffa8d6`, and by nothing else. A local formula that happens to
agree today is the defect this invariant exists to catch: it agrees until §2.4
moves, and then the screen lies. Gate it two ways — against a direct call to
those two functions across the whole type × terrain matrix, and against the
numbers an applied resolution actually spends, so "identical numbers" is
measured at both ends and not just at one.

**T-UI-02 — the UI queries the module and never recomputes movement.** The
highlight is *exactly* `Move.h::reachable`'s set, hex for hex and cost for cost,
in canonical order. The plausible wrong reading is a distance filter: every hex
within the unit's Move. It agrees on open ground and diverges the moment terrain
costs more than 1, is impassable, or an occupant blocks a route — and Q3's
conservative reading, in force, makes any other unit a full block. Gate the
divergence, not the agreement.

**T-UI-05 — the check must be able to DISAGREE with the projection, and the
first version was not.** Clause (b) says each DECLARED DERIVED field is
*"recomputed inside the check rather than read back out of the snapshot — so a
wrong derivation fails here and is not merely reproduced"*. Rebuilding the
snapshot and comparing it to itself is the obvious wrong reading, and there is a
second, subtler one: **calling the same derivation helper the projection called.**

That is not hypothetical here. On the first pass the check shared the
projection's helpers, and the pass-1 variant's wrong `incomePerTurn` and wrong
`isGuidedMarked` **both passed the invariant** — projection and check were wrong
together, so clause (b) compared a value to itself. The three derivations are
therefore **written out a second time inside the check**, from the stub's words.
The duplication is the mechanism, not an oversight; do not "clean it up".

Clause (c) needs two directions, because each catches a different defect: a
field in the snapshot with no contract row (one that entered without a contract)
and a contract row nothing emits (one the document claims and the snapshot does
not carry). Neither is visible to (a) or (b), which only compare fields both
sides already agree exist.

Assert it the way the stub says — **rebuild the snapshot after each command of a
fixed command sequence** — and pin the two rulings that a self-consistency check
cannot see on its own: `incomePerTurn` **= the standing rate while turn 1 pays
0**, and the guided mark **surviving its unit's move**.

**GATE-CAP-PARTIAL — it is a differential read of two fields, and both halves
must move.** Raising a unit's `captureProgress` short of completion leaves
**both sides'** `objectivesHeld` unchanged. Assert that the progress field
**did** rise in the same step; an implementation that changes nothing at all
passes a one-sided check while breaking the game. This is §2.8's **T-CAP-05**,
which aliases onto no `T-TURN-` ID; it adds no field and no numbered ID (the
`GATE-AI-SMOKE` precedent, row 6), so §4.5's written-ID count does not move.

Q14 is ruled: a partially captured objective **counts for nobody until the
objective flips**. Partial credit would need a fractional-count rule and would
invert T-CAP-05.

**The check needs a scenario with N ≥ 2 and the shipped one has N = 1.** Capture
completes after N turns of holding, N is per-scenario data, and *Ferrum
Crossing* ships N = 1 (§2.7), so on shipped data a capture never has a partial
state to be read. Configure the fixture's `captureTurns` and **say so in the
run** — this is a property of the shipped scenario, not a licence to invent a
map, and the state the gate asserts about is one the shipped scenario cannot
reach.

## Determinism / constraints

Pure projection plus two delegating queries. No RNG, no clock, no I/O beyond the
tables the caller already loaded. Widgets are pure functions of snapshot +
events (§4.9 T-INT-05). The snapshot enumerates hexes and units in **canonical
order** so two runs on the same state produce the same bytes. Must compile with
a plain C++17 compiler; verify under **clang++ and MSVC** both — `g++` is not
installed on this machine.

## The driver

Add the commands that reach this module and no others, and **refuse rather than
decide** wherever an answer would need a row that holds no code. That discipline
went stale once already: at `2381ca0` the driver's `help` still announced "no
AI" one commit after the AI landed. When you add a command, sweep the banner,
the `help` body, the file-header comment and `README.md` in one pass.

`GATE-DRV-*` IDs are **not** `T-*`. The driver is not a §4.7 stub, has no ledger
row, and flips nothing.

## Acceptance

`T-UI-01`, `T-UI-02`, `T-UI-05` and `GATE-CAP-PARTIAL` in full. **`T-UI-03` and
`T-UI-04` do not run** — in-editor Automation, no editor pass at this commit —
and are reported by name with that reason. They are **written, unblocked and
asserting**; what they lack is a harness, not a rule. That is a different state
from `T-SCN-10`, which is reserved and unwritten, and from `T-MOVE-07`, which is
blocked.

**The row still does not flip** (Q29, applied per acceptance ID as well as per
row): `T-UI-03` and `T-UI-04` remain unclosed, so this row records a partial
pass and stays `*pending*` — row 2's posture on `T-DATA-05` and row 7's on
`T-SCN-08/09/11`. What changed is that the editor pass is now the *whole* of
what the row lacks; before `T-UI-05` was implemented it was not.

Two passes, as every row before this one: a pass-1 implementation carrying a
plausible misreading of *this document alone*, blocked by named IDs you predict
in advance, then a pass-2 that is green. Predict the pass-1 failure set out loud
before running it, and treat any deviation as a defect in the break rather than
a bonus.
