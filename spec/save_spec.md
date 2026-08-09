# SPEC: Save & replay format — part (a)  (Director → Systems Engineer)

Game: **Stratocracy** — UE5.8 hex turn-based strategy. Headless (§4.1): **zero
engine dependencies**, pure C++17, deterministic.

This is **§4.7 SPEC STUB "Save & replay"**, §4.11 build-order **row 10**, and it
was, at that build, the **last unbuilt row**. §4.11 splits the row into three parts with three
dependency sets; **this build is part (a) alone**:

> (a) *Format spec + header/version machinery* — **no deps at all; write it
> first**, and T-SAVE-04 (refusal on any header mismatch) closes on it alone,
> since it never applies a command.

## What this part is, and what it is not

**This is the file format and the machinery that refuses a file. It is not the
replayer.** Part (a) never applies a command, never touches a `Board`, and never
computes the canonical state hash.

That is what makes the dependency set empty. Every value this module compares
against is **supplied by the caller as an argument** — the same design rule that
let row 4 land before row 5: *a module takes what it does not own as an
argument.* Save does not know what the current rules commit is, what the §4.8
data set hashes to, or which scenario is loaded. It is told, and it compares.

**Explicitly out of scope, and each belongs to a named later part:**

- **Applying the command log** — part (b), the headless replayer. This module
  parses the log **structurally** and asserts nothing about whether any command
  is legal, reachable, or in turn order.
- **The canonical state hash of §4.10** — the definition is §4.10's and its
  first consumer is part (b). Here `stateHash` is an **opaque string** that is
  parsed, carried and re-emitted unexamined. It is **not** the `stateHash` in
  `Driver.h`, which is the debug driver's own digest (`GATE-DRV-06`) and a
  different thing; this module does not link the driver and does not call it.
- **Slot I/O and the `USaveGame` wrapper** — week 5, and no headless gate waits
  on it (§4.11).

## The Director's scope ruling — read this before scoping any fixture

**One acceptance ID closes here: `T-SAVE-04`.** The other six do not run, and
each has a reason that is not "we ran out of time":

- **T-SAVE-01, 02, 03, 05** need the replayer to exist — they save, load, replay
  or apply a prefix. Part (b).
- **T-SAVE-06** is the in-editor half of the parity pair, marked **`†`** in
  §4.11 and asserted jointly with `T-INT-02`, so no headless build can close it.
  Both blockers this entry used to name are gone: the canonical state
  hash was built when part (b) landed and defined it, and the in-editor Automation
  harness landed at UE `fed8ae9`.
- **T-SAVE-07** needs row 6's self-play logs. Part (c), week 4.

**The consequence is stated, not hidden: row 10's ledger row does NOT flip, and
it has no ledger row to flip.** §4.11 calls row 10 a **proposed** ledger row, so
§3's table gains nothing on this build and the nine verified rows stand. Do not
write a flip, do not imply one, and do not describe the suite as complete.
**Report every unrun ID by name with its reason**, the way row 7's and row 8's
records report theirs.

## The schema

Versioned JSON, one file, exactly the fields §4.10's file-layout table names —
**no more and no fewer.** An unknown key is a refusal, not an ignored extra.

| Field | Type | Meaning |
|---|---|---|
| `formatVersion` | int | This layout's version; unknown = refuse load |
| `rulesCommit` | string | Crew commit of the rules module that wrote the file |
| `dataHash` | string | Hash of the §4.8 CSV set in effect |
| `scenarioId` | string | The §4.7 Stub-7 scenario file |
| `scenarioHash` | string | Its hash |
| `seed` | int | Reserved; **written as 0** — no RNG ships (§2.6, pending Q12) |
| `commandLog` | array | The ordered commands of §4.9, tagged `{turn, side}` |
| `stateHash` | string | Canonical hash of the resulting state (opaque here) |
| `result` | string or null | Result tier (§2.8) if the match ended, else null |

**The command log's vocabulary is §4.9's and is not extended here.** Five
commands, with the fields §4.9 names them with:

    Move{unit, destHex}   Attack{unit, targetHex}   Build{factoryHex, unitId}
    Capture{unit}         EndTurn{}

Each entry is an object carrying `turn`, `side`, `kind`, and exactly the fields
its own `kind` names — a `Move` entry carrying `targetHex` is a refusal, not a
tolerated extra. Hexes are authored `[col, row]` odd-r and converted at parse
time, the T-SCN-05 posture: **no parsed state holds (col, row)**.

## Stated readings

Each is a **DOCUMENTED CHOICE**, not a rule. §4.10 requires a determinate answer
and leaves the term undefined; none of these adds a rule the GDD does not have.
This is the posture `Scenario.h` records its eleven readings in.

1. **`formatVersion` is 1.** §4.10 versions the file and names no number; this
   build defines the format, so this is its first version. (Reading 1 of
   `Scenario.h`, for the same reason.)
2. **The refusal set is exactly the four fields §4.10 names**, and `scenarioId`
   is **not** one of them. §4.10's Version policy enumerates *"mismatched
   `formatVersion`, `rulesCommit`, `dataHash`, or `scenarioHash`"* and
   T-SAVE-04's parenthetical repeats the same four as *"version/rules/data/
   scenario hash"*. `scenarioId` is carried and type-checked; making it a fifth
   refusal trigger would be **a rule the GDD does not have**, and the hash
   already distinguishes any two scenario files.
3. **`null` is accepted as a value, for `result` alone.** §4.10's own type
   column reads *"string/null"*, so the schema requires it. Everywhere else
   `null` is a refusal, which is where `Scenario.h`'s reading 10 leaves it — the
   two parsers differ **here and only here**, and because the two schemas
   differ, not because this one is laxer.
4. **`stateHash` is opaque at this part.** It is required, must be a string, and
   is otherwise unexamined. Part (b) defines what it must contain. A part-(a)
   check that asserted anything about its *value* would be asserting §4.10's
   canonical-hash definition without building it.
5. **`seed` must be present and must be 0.** §4.10 says *"Reserved; written as
   0"*. A non-zero seed is a refusal under `GATE-SAVE-PARSE`, not under
   T-SAVE-04 — it is a schema violation, not a header mismatch.
6. **"State untouched" is asserted on the caller's out-parameter.** T-SAVE-04
   says a refused load leaves state untouched. With no replayer there is no game
   state to leave alone, so the assertable subject is the `Save&` the caller
   passed: on **every** refusal it is bit-for-bit what it was before the call.
   This is the strongest form available at part (a) and it is the one that
   catches the real defect — a loader that fills the caller's object and
   validates afterwards.
7. **Parse refusal and header refusal are different verdicts.** A malformed file
   fails `GATE-SAVE-PARSE`; a well-formed file whose header disagrees fails
   `T-SAVE-04`. Each refusal names which, so a test cannot pass for the wrong
   reason.
8. **This module owns no digest.** It compares strings it is given. `dataHash`
   and `scenarioHash` are computed by the modules that own that data (row 2's
   tables, row 7's `scenarioHash`), and part (a) depending on either to
   *recompute* them is exactly the dependency §4.11 says this part does not
   have.

## The gate

    T-SAVE-04       refusal: any header mismatch (version/rules/data/scenario
                    hash) -> load refused WITH A REASON, and the caller's state
                    is untouched. Four mismatch fixtures, one per field, plus a
                    matching-header control that must LOAD.
    GATE-SAVE-PARSE the strict parser refuses malformed input with a reason:
                    unknown key, missing key, duplicate key, trailing comma,
                    null outside `result`, non-integer number, \u escape, raw
                    control character, trailing content, bad command kind,
                    a command carrying another kind's field, non-zero seed.

`GATE-SAVE-PARSE` is **not** a §4.7 stub ID and mints **no acceptance ID** — the
`GATE-SCN-PARSE` / `GATE-AI-SMOKE` / `GATE-CAP-PARTIAL` precedent. It gates a
file format, not a rules system, so it moves **no §4.5 count** and flips no
ledger row.

## Determinism

Pure. No RNG, no clock, no I/O beyond reading the one file. `parseSave` and
`serializeSave` are total functions of their inputs, and `serializeSave` emits
fields in the §4.10 table's order so that a round trip is byte-stable.
