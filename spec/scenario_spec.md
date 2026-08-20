# SPEC: Scenario file & validator  (Director → Systems Engineer)

Game: **Stratocracy** — UE5.8 hex turn-based strategy. Headless (§4.1): **zero
engine dependencies**, pure C++17, deterministic.

This is **§4.7 SPEC STUB 7**, §4.11 build-order **row 7**. Its structural half
depends on rows 1–2; its priced half — T-SCN-04, 06, 08 and 11, which all cost a
Stub-3 path — depends on row 3. All three have landed, so nothing here is
blocked. Row 7 is **not on the critical path**; row 8 depends on it.

## The Director's scope ruling — read this before scoping any fixture

**This build validates the shipped map only.** *Longwater March* (§2.13.5) and
*The Causeway* (§2.13.6) are **not** authored as scenario files, not even as
test fixtures. §2.13.7 states the condition under which the stretch set "stays on
paper," and the Director has ruled that authoring them as validator fixtures is
not licensed by this row.

§4.11's `†` note already states exactly what that costs, and it is the scope:

- **T-SCN-08** loses fixtures **(a)** *The Causeway* and **(b)** *Longwater
  March*. It keeps **(c)**, the synthetic ceiling refusal, and it keeps its
  measure-and-report behaviour on the shipped map.
- **T-SCN-09** loses its **asserting branch** entirely — `rot180` asserts hex by
  hex, and the shipped map declares `none`, which asserts nothing. Its
  **refusal** branch stands: `rot180` declared on an odd row count is a hard
  refusal, and the shipped 11 × 9 map is an odd row count, so the refusal is
  reachable by mutating the shipped map's own declaration and nothing else.
- **T-SCN-11** loses fixture **(c)** *The Causeway*. It keeps **(a)** and
  **(b)**, both shipped-map fixtures, **including the failing one**.

**The consequence is stated, not hidden: row 7's ledger row does NOT flip.**
Q29 requires the full acceptance set at one commit and this build closes a
subset, so the row records a partial pass and stays `*pending*` — the same
posture row 2 holds on T-DATA-05. Do not write a flip, do not imply one, and do
not describe the suite as complete.

**Do not invent a map to recover a lost fixture.** A synthetic file is
legitimate only where the stub already calls for one — T-SCN-08 (c) is
explicitly "a scenario whose lanes both cost 7", which names no map — and for
T-SCN-09's refusal, which needs only a declaration change on an existing grid.
Anywhere else, a lost fixture is **reported as not run**, never replaced.

## Format

One versioned JSON file per scenario. `strat::loadScenario` parses and validates
it headless. §4.7 Stub 7 states the field list and the ordering discipline —
read it there and follow it exactly. Two properties of that list are
load-bearing and easy to lose:

1. **`scenarioHash` is over a canonical serialization** — fields in the stub's
   stated order, hexes in canonical hex order. **New fields append at the tail**,
   so adding one can never reorder an existing one.
2. **Serialization order is not validation order.** A field is validated after
   whatever it references, wherever it sits in the preimage.

`symmetry` is **required**. Absent or unrecognized is a **hard load failure**,
never a silent default of `none`.

Write the shipped scenario, *Ferrum Crossing*, as `data/ferrum_crossing.json`.
Its layout, ownership, deployment and guided opening are fully specified in
§2.13.2 and §2.13.1 — read them and transcribe. **Author nothing.** If a value
you need is not in the document, stop and file a change request rather than
choosing one.

## Required functions

Name them so the caller can price a lane without loading a file, because the
gate needs that and so does the driver:

```
strat::ScenarioLoadResult strat::loadScenario(const std::string& path, ...);
strat::ScenarioLoadResult strat::validateScenario(const Scenario&, ...);
std::string               strat::scenarioHash(const Scenario&);
```

A refusal must carry a **reason**, and the reasons T-SCN-08 and T-SCN-11 produce
must carry their **measured integers** — see the print convention below.

## Invariants (the merge gate)

`T-SCN-01` through `T-SCN-09` and `T-SCN-11` are stated in full in §4.7 Stub 7.
**Read them there rather than from a paraphrase here** — every one of them is
written with its rationale, and three have been re-scoped by rulings the stub
text already folds in (Q21, Q22, Q24, Q25, Q26, Q28). What follows is only what
this build must not get wrong.

**T-SCN-06 — the ceiling is derived, never a literal.** Find the capturing row
by `CanCapture` and take `2 × Move` from the loaded table. A §2.4 Move change
must re-price the gate rather than silently pass it. Cost counts **every hex
entered including the objective itself** — the same accounting as T-MOVE-01.
Quantify over the **named** hex, not over any qualifying hex.

**T-SCN-08 — measure, never infer.** The declared symmetry flag is not an input
and cannot substitute for a path cost. Report each lane cost as an integer.

**T-SCN-09 — the even row count is a precondition, not a comparison.** On odd
`H` the axial constant `W − H/2` is a half-integer, so no hex has a hex image and
the file is refused **before any comparison runs**. `rho` runs in axial, after
the T-SCN-05 conversion. Per Q25, `guidedOpening` is **not** bound.

**T-SCN-11 — the quantifier is the invariant.** The opposing route is minimised
over **every `CanCapture`-row unit that seat deploys** (Q28), not over that
seat's `guidedOpening.infantry` alone. Fixture (b) exists precisely to catch an
implementation that minimises over the guided unit alone — under the refused
reading it passes at 5 against 6 instead of failing at 5 against 5. Three further
asymmetries with T-SCN-06 are deliberate and stated in the stub: **no ceiling**,
**Bridges permitted on the opposing route**, and pricing on **terrain alone**
(Q21), occupancy excluded.

**Print convention — the relation is named at the site.** Integer order carries
no information: both relations print the larger integer first on their failing
form. A bare "X against Y" quantifies its right-hand term as the **set minimum**
over the opposing seat's `CanCapture` units. A cost from some other named hex is
a **third quantity** — print it with its hex and say what it is.

## Determinism / constraints

Pure parse plus validation; any failure refuses the **whole file** with a reason.
`scenarioHash` is platform-stable by canonical ordering. Lane costs are Stub-3
path costs and inherit its determinism (T-MOVE-04's canonical tie-break,
T-MOVE-06), so reported integers reproduce across runs and compilers. No RNG, no
clock, no I/O beyond reading the file. Must compile with a plain C++17 compiler.

**No third-party JSON library.** The headless modules vendor nothing; write the
parser, and make it refuse malformed input rather than tolerate it.

## The driver

Add the commands that reach this module and no others. Where an answer would
need row 8, **refuse rather than decide** — that discipline is the whole reason
the driver holds no rules, and it went stale once already: at `2381ca0` the
driver's own `help` still announced "no AI" one commit after the AI landed. When
you add a command, sweep the banner, the `help` body, the file-header comment and
`README.md` in one pass.

`GATE-DRV-*` IDs are **not** `T-*`. The driver is not a §4.7 stub, has no ledger
row, and flips nothing.

## Change requests for the Director

There is one, and it is a question about the scope ruling at the top of this
file rather than about anything the ruling decided correctly for row 7.

1. **A second authored scenario, wanted for a reason outside row 7. FILED
   2026-08-19; not decided here.** The scope ruling forbids authoring *Longwater
   March* and *The Causeway* as scenario files, weighed against row 7's own costs
   — two T-SCN-08 fixtures, T-SCN-09's asserting branch, one T-SCN-11 fixture,
   and a ledger row held at *pending*. A downstream consumer now wants one for a
   reason that ruling did not weigh, because it did not exist yet: the UE
   project's content-independence precedent asks that an invariant not rest on a
   single corpus. Two of its three axes — first side and buildlist — are
   discharged. The **scenario** axis cannot move at all, because
   `data/ferrum_crossing.json` is the only scenario that exists and the UE side
   carries it as vendored certified bytes it may not edit.

   **The consumer needs no code.** `UStratMatchSubsystem::ScenarioFile` is
   already a configured path relative to the project directory, and every
   automation clause that opens a scenario sets that string rather than compiling
   one in. What is missing is the datum.

   **The ask is a ruling, not a task:** does the content-independence need
   license authoring *Longwater March* (§2.13.5, taken first because §2.13.7
   orders *The Causeway* after it) ahead of §2.13.7's week-4 condition?
   §2.13.7's four clauses are the Director's own test and are not argued around
   here; whether week 4 is consumed by balance is not a fact either repo holds.

   **If the answer is yes, one value is missing from the GDD and is the
   Director's to state.** §2.13.5 gives an estimated match length (16–20 turns,
   "frequently reaching the cap") but no `turnCap`, and §2.8 makes the cap
   per-scenario data — 20 is *Ferrum Crossing*'s. Under this file's own "author
   nothing" rule that number cannot be chosen by whoever transcribes.

   **Everything else in §2.13.5 does transcribe, checked by script rather than by
   eye:** the ASCII grid is 13 × 8 = 104 hexes; its glyph census (Plains 86 ·
   Woods 4 · Mountains 4 · Towns 4 · Factories 6, no Water, no Bridge) is
   identical to the distribution the section states in prose; every named
   coordinate lands on the glyph it is named as; the grid is ρ-symmetric at all
   104 hexes under ρ(c, r) = (12 − c, 7 − r), not only at the nine pairs listed;
   all ten starting positions are on Plains, distinct, ρ-imaged seat to seat,
   with both home factory hexes free; and the map names no terrain kind absent
   from `data/terrain.csv`, so the tables do not move. `startingFame` follows
   §2.7's 200 per side.

   **Three consequences go with a yes, and none is free.** (a) *Longwater March*
   declares `rot180` on an even row count, so **T-SCN-09's asserting branch would
   run for the first time** — implementation surface this row deliberately does
   not have today, not a fixture addition. (b) Row 7's *pending* ledger posture
   was reasoned against the current scope; T-SCN-08 recovers fixture (b), and
   whoever lands the map must restate the posture rather than inherit it. (c)
   `sync_stratdata.py`'s `SCENARIOS` list is declared rather than globbed, so
   vendoring the file downstream is an upstream edit plus a manifest entry under
   `GATE-DATA-VENDOR`. And `scenarioHash` is produced by `strat::scenarioHash`,
   never typed.

   **One drafting trap for whoever transcribes:** §2.13.5's ASCII grid writes the
   central Woods knot as `w`. It is **Woods, not Water** — that map has no Water
   at all, and *Ferrum Crossing*'s grid trains the opposite reading.

   Full investigation, including the git-blob measurement that the vendored copy
   equals the current crew bytes and the list of what was deliberately not
   verified:
   `Stratocracy/Tools/architect/evidence/upstream-second-scenario.md`.

## Acceptance

`T-SCN-01..07` in full. `T-SCN-08` on fixture (c) plus measure-and-report on the
shipped map. `T-SCN-09` on its refusal branch. `T-SCN-11` on fixtures (a) and
(b), (b) being a **required failure**. `T-SCN-10` is reserved and unwritten on
Q26 — nothing is asserted and nothing is waiting.

**Report every ID that did not run, by name and with its reason**, the way the
week-1 record reports T-DATA-05 and T-MOVE-07. A suite that quietly omits a
fixture reads as a complete pass, and this one is deliberately not complete.

Two passes, as every row before this one: a pass-1 implementation carrying a
plausible misreading of *this document alone*, blocked by named IDs you predict
in advance, then a pass-2 that is green. Predict the pass-1 failure set out loud
before running it, and treat any deviation as a defect in the break rather than a
bonus.
