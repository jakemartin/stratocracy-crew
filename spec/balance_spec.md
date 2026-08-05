# SPEC: Self-play log producer — §4.11 row 10, part (c)

*(Director → Systems Engineer / Balance Analyst. GDD §4.10, §4.11 row 10, §4.4 week 4.)*

## What this part is for

`T-SAVE-07` — *harness compatibility: a Balance Analyst self-play log validates and
replays as a save file — one format, no dialect drift.*

Before this module the repo had **no producer of such a log at any scope**.
`cpp_reference/selfplay.cpp` is a combat-only 1v1 duel harness over `Combat.h`: it runs
every ordered pair of the four unit types to a 50-round cap, prints a table of winners
and rounds-to-kill, and opens no file. It is the Balance Analyst's *duel* instrument and
it is not a match. So `T-SAVE-07` had a subject in the GDD and none in the code, which is
what part (c) closes.

## Inputs

- an opened `strat::GameState` — the caller has run `initMatch` and `openTurn`, the same
  precondition `replayLog` has, so the producing run and the replaying run begin at the
  same moment;
- the §4.8 tables, borrowed as `RulesTables`, plus the `UnitDef`/`TerrainDef` vectors the
  AI's view copies;
- the buildlist, as defIndexes. §2.9 describes it as *"mostly Infantry, an occasional
  Tank"* and gives no ratio, so `Ai.h` takes it as caller-supplied **data**; this module
  passes it through and invents nothing;
- a command budget, which bounds the loop so a misbehaving AI cannot hang the gate.

## What it produces

A `SelfPlayResult`: the accepted command log, the final `GameState`, the `MatchResult`,
and a `SelfPlayStop` saying why the run stopped. `selfPlaySave` wraps the log in a §4.10
save whose header fields are all **supplied** — this module recomputes no header value and
hashes only the final state, which is the one thing it holds.

## Invariants

    T-SAVE-07  harness compatibility, in three clauses:
       (a) VALIDATES  — the emitted text loads under a matching header expectation
       (b) REPLAYS    — replaying the parsed log from the same opened initial state
                        reaches the canonical state hash the producing run reached, and
                        that hash is the one the file's header carries
       (c) NO DIALECT DRIFT — serialise -> parse -> serialise is byte-identical and every
                        kind in the log is one the shared spelling table names

    GATE-BALANCE-*  (mint no acceptance ID, on the GATE-SAVE-PARSE / GATE-REPLAY-*
                      precedent)
       TRANSLATE-MOVE / -ATTACK-IS-TARGET-HEX / -BUILD-NAMES-THE-UNIT-BUILT /
       -ENDTURN-NAMES-NEITHER   the four translations, checked directly
       RUN-ENDS-WITH-A-TIER     the match ends by the rules, not by the budget
       COMMAND-SET-IS-THE-AIS-FOUR   Move, Attack, Build, EndTurn all present; no Capture
       LOG-HOLDS-ONLY-ACCEPTED-COMMANDS   every entry re-applies from the initial state
       ENTRY-TAGS-ARE-THE-LIVE-TURN-AND-SIDE
       DETERMINISM-TWO-RUNS-ARE-IDENTICAL

**Determinism:** pure. No RNG, no clock, no I/O. Row 6's `nextCommand` is a pure function
of its view, and this module adds no state of its own, so two runs from one initial state
produce byte-identical logs — which is what makes `T-AI-06` compose into this suite the
way it composes into `T-SAVE-02`.

**Acceptance:** `T-SAVE-07`. `T-SAVE-06` does not run here — it is row 10's only `†`, is
asserted jointly with `T-INT-02`, and no in-editor Automation harness exists.

## Stated readings

1. **The log carries four command kinds, not five, and that is the AI's design.** `Ai.h`
   says capture is deliberately outside the AI's vocabulary: it is a turn-boundary event
   the caller runs beside income, and the AI's part of §2.9's capture behaviour is the
   **move onto the objective** (`T-AI-03`). `AiCommandKind` therefore has four members
   where §4.9 has five, so a self-play match emits `Move`, `Attack`, `Build` and
   `EndTurn` and never `Capture`. The complete §4.9 set was exercised by part (b)'s
   hand-authored log. `T-SAVE-07` asserts **format compatibility, not command coverage**,
   so those four are its whole written fixture set and Q29 is satisfied over that set.
   The gate asserts the four are present rather than leaving it unstated.
2. **Only an accepted command is logged.** A log that records what was *proposed* replays
   to a different state than the match it came from — precisely the drift this ID exists
   to refuse. The pass-1 variant logs the proposal, and the gate catches it in two places.
3. **Entry tags are read before the command is applied.** `EndTurn` advances the turn and
   the side, and the entry belongs to the turn that ended. `applyCommand` validates the
   tags against the live state, so the alternative is not merely cosmetic — it is refused.
4. **`result` is written as §2.8's tier name, or left null.** §4.10's type column reads
   *string/null*, and a run that did not end has no tier to state.
5. **A third module for one ledger row.** Each of row 10's three parts has its own
   dependency set in §4.11, encoded as its own row's link set in `crew/tools.py`. Folding
   part (c) into `Replay.h` would put row 6 inside part (b)'s claim.

## Change request — the duplicated translation

`aiViewOf` and `aiCommandToSave` also exist as **file-static copies in
`cpp_reference/test_replay.cpp`**, which is where they were written when part (b) needed
an AI-generated segment for `T-SAVE-02`'s determinism composition. They are left there
deliberately: deleting them in favour of this module's would add `Balance.cpp` to the
`replay` row's link set and put a part-(c) dependency inside part (b)'s stated
dependency claim.

The duplication is between a module and a test's private copy, and it is of a
**translation** rather than of a rule any module owns — the same shape as, and smaller
than, the `openTurn` / `openActiveTurn` change request `spec/replay_spec.md` files.
Converging them is filed here and not done here; it should be taken together with that
one, when §4.11's part (b) cell is next opened.
