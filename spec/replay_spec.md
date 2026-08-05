# Spec Stub: Save & replay, part (b) — headless replayer + canonical state hash

Director → Systems Engineer. GDD §4.10, §4.11 build-order row 10 part (b).
Implemented at `cpp_reference/Replay.h` / `Replay.good.cpp`, gated by
`cpp_reference/test_replay.cpp`, registered as row `replay` in `crew/tools.py`.

**Inputs.** A `GameState`, a `Save` command log (part (a)'s format), the §4.8 tables
borrowed as `RulesTables`.

**Acceptance.** `T-SAVE-01`, `T-SAVE-02`, `T-SAVE-03`, `T-SAVE-05` close here.
`T-SAVE-06` **runs nowhere yet** — §4.11 marks it `†`, it is asserted jointly with
`T-INT-02`, and no in-editor Automation harness exists. `T-SAVE-07` is part (c).
`GATE-REPLAY-*` mint no acceptance ID, on the `GATE-SAVE-PARSE` precedent.

---

## Why this is a second module and not more of `Save.h`

§4.11 gives row 10 three parts with three dependency sets, and part (a) has **no
dependencies at all**. That claim is not prose — it is encoded as the `save` row's
link set in `crew/tools.py` (`Save.cpp`, `Hex.cpp`, `test_save.cpp`). Part (b) needs
rows 1–5 plus row 6. Widening the `save` row's sources to reach the replayer would
have falsified §4.11's dependency cell **silently**. Two rows keep both claims
checked, so contradicting either fails at the gate instead of in a paragraph nobody
re-reads.

## `strat::GameState`

The GDD named this type before anything realised it: §4.9 calls it *the authoritative
`strat::GameState`*, §4.10 defines the hash over it, and **`T-UI-05`'s own invariant
text — green since `41a1452` — asserts the snapshot's mirrors against it.** No such
type existed in this repo. Declaring it here amends no invariant's text, so no
closure re-dates.

It is a **fourth composition, not a second source of truth.** `AiState` (row 6),
`UiWorld` (row 8) and the driver's `Session` are already three bundles over the same
module-owned state, and that is not duplication because **none of them owns a rule** —
which is what `GATE-DRV-05` is actually about. GameState is the one the GDD calls
authoritative; that the other three do not yet read from it is **filed as a change
request** below.

It holds the mutable state the rules modules own — board, units, economy, turn — and
**not** the §4.8 tables or the Stub-7 scenario. §4.9 names those as three separate
module-side sources, and a module takes what it does not own as an argument: the rule
that let row 4 land before row 5.

**The flag designation lives on the state, not on the unit.** `flagUnit[side]` is
`-1` when a side designates none. A per-unit `bool` cannot work, because a dead flag
is simply absent from `units` — and *"the flag died"* and *"this side never had one"*
are opposite verdicts at §2.8. `isFlagUnit()` derives the per-unit fact, exactly as
`hasMoved`/`hasActed` are derived from row 5's id sets.

## The canonical state hash — and the two groups that moved

Serialised in a fixed field order, every value an integer, every collection walked in
**canonical hex order** (`T-HEX-07`) rather than storage order, then FNV-1a 64 over
the bytes. `canonicalStateBytes` is exposed beside the digest so a check can assert
the **serialisation** and not only the hash: two digests can agree by collision, and a
digest comparison cannot say which field diverged.

    turn counter; side to move
    per side          : fameTotal, fameCombat
    objective ownership: {hex, owner}                 <- EconomyState::objectives
    per unit          : {id, side, hex, hp, isFlag, hasMoved, hasActed}
    per tile          : capture progress {hex, unitId, turnsHeld}
                                                      <- EconomyState::captures
    per factory       : build allowance {hex}         <- TurnState::builtThisTurn
    pending builds    : {factoryHex, side, defIndex}  <- EconomyState::pending

**`captureProgress` and `pendingBuilds` were written into §4.10 as per-UNIT fields and
are not.** `Economy.h` says in as many words that capture progress *"is held by the
TILE, not by the unit, and names the unit that accumulated it so progress can never
transfer (Q4, T-FAME-05)"*, and `PendingBuild` is keyed by `factoryHex`. Hashing them
per-unit would have required a projection contradicting the rule they encode. The GDD
is corrected to the grouping the modules hold.

**`buildWaiting` is not hashed**, on §4.10's own omission rule: it is exactly *"a
pending build stands at this factory"*, recomputable from the pending-build group, so
it can add no distinction and only one more way for two builds of one state to
disagree. `spawnBlocked` is omitted for the same stated reason.

**Ties are broken by id, not by storage order.** Canonical hex order is a total order
over *hexes*, not over the records keyed by them, and two records can share a hex.
Falling back on insertion order there is the leak `T-HEX-07` exists to close.

## The start-of-turn moment

`openTurn` runs `beginTurn`, then start-of-turn repair, then **income, then the
capture tick** — the order the Director ruled on 2026-08-03, so an objective whose
capture completes at the start of turn T pays its new owner from T+1. It is called on
`EndTurn` and once after `initMatch`.

**A replay that skips it diverges from the match it replays**, silently and only after
the first turn boundary — the exact class of defect `T-SAVE-01` and `T-SAVE-02` exist
to catch.

**This is the second implementation of that sequence here.** `Driver.good.cpp`'s
`openActiveTurn` is the older sibling, over `Session`. That duplicates an **order the
Director ruled**, not a rule any module owns. Filed below rather than hidden.

## All-or-nothing

`replayLog` applies to a **copy** and assigns to the caller only after the last
command succeeds. `T-SAVE-05`'s named tripwire is an implementation that applies then
validates, leaving a corrupted half-loaded state that passes every happy-path test.

---

## Readings taken here, not rules

1. **A log entry's `{turn, side}` tag must match the live turn and side.** A log that
   disagrees is a log from another match. Checked only while a match runs.
2. **`Build{factoryHex, unitId}`'s `unitId` is the TYPE built**, not an existing unit —
   part (a)'s own reading of the field, carried forward unchanged.
3. **`Attack` is tagged by target HEX**, per §4.9's `Attack{unit, targetHex}`. An AI
   command naming a target *id* is resolved to that unit's hex when it enters the log,
   so the log never stores an id the board would have to re-resolve.
4. **A boxed-in build waits and holds its slot** rather than failing the command —
   `Economy.h::resolveBuilds` decides it and this module reports nothing of its own.

## The gate's own fixture, and one thing it got wrong first

The fixture board carries **a factory per side**. §2.8's domination backstop
(`T-TURN-03`) ends a match the moment one side holds *every* factory, and row 5 checks
it at `beginTurn` — so a one-factory fixture is over before its first command, and
every later command is refused *"no match is running"*. Two factories is the smallest
board on which a turn can be played.

**The `T-SAVE-05` check was written so that it could not fail.** Its bad log was
replayed from a state in which the log's first entry had already been played, so entry
0 was refused as a repeat and nothing was applied *under any implementation* — the
check passed against the very defect it exists to catch, and the buggy variant is what
exposed it. It now starts from the initial state, so entries 0 and 1 apply before the
refusal at index 2, and it asserts the index by value.

The log carries **the complete §4.9 command set** — `Move`, `Attack`, `Build`,
`Capture`, `EndTurn` — because Q29 closes an acceptance ID only when its whole written
fixture set ran. A segment of it is **generated by row 6's AI** rather than
hand-written, which puts `T-AI-06` inside `T-SAVE-02`'s determinism composition rather
than beside it. The AI's segment moves a unit, attacks the opposing flag Tank, and
ends the turn, so the log crosses a turn boundary under both sides.

---

## Change requests — filed, not decided

1. **`AiState`, `UiWorld` and `Session` should read from `GameState`.** Four
   compositions of one state is three too many now that the GDD's authoritative one
   exists. None owns a rule, so nothing is unsound today; it is a convergence, and it
   would let `openActiveTurn` and `openTurn` become one function.
2. **`openTurn` and `openActiveTurn` duplicate a ruled ORDER.** Until (1) lands, a
   change to the income/tick order must be made twice, and nothing checks that the two
   agree.
3. **`T-SAVE-07` has no producer.** `cpp_reference/selfplay.cpp` is a combat-only 1v1
   duel harness that prints a table; it emits no §4.10 command log. Part (c) needs a
   self-play harness that writes save files, and §4.4 does not say who builds it.
