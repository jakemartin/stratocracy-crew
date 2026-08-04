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
   to these. **The DONE bit is not a snapshot field and you must not add one** —
   where per-unit presentation state lives is an open question filed with the
   GDD half and unruled. A widget-side concept in this contract would decide it.

**Do not add a field the stub does not name.** Three are known to be missing and
each is filed as an unwritten change request against the GDD, not a gap for this
build to close:

- the **per-factory "has built this turn" record** §2.11.5's `BUILD` pulse would
  need (`TurnState::builtThisTurn` holds it, and whether it reaches the snapshot
  as a field or a query is unruled);
- §2.11.2's **income rate** (`+175/turn`);
- the **DONE bit**, above.

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

`T-UI-01`, `T-UI-02` and `GATE-CAP-PARTIAL` are stated in full in §4.7 Stub 8.
**Read them there rather than from a paraphrase here.** What follows is only
what this build must not get wrong.

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

`T-UI-01`, `T-UI-02` and `GATE-CAP-PARTIAL` in full. **`T-UI-03` and `T-UI-04`
do not run** — in-editor Automation, no editor pass at this commit — and are
reported by name with that reason. They are **written, unblocked and asserting**;
what they lack is a harness, not a rule. That is a different state from
`T-SCN-10`, which is reserved and unwritten, and from `T-MOVE-07`, which is
blocked.

Two passes, as every row before this one: a pass-1 implementation carrying a
plausible misreading of *this document alone*, blocked by named IDs you predict
in advance, then a pass-2 that is green. Predict the pass-1 failure set out loud
before running it, and treat any deviation as a defect in the break rather than
a bonus.
