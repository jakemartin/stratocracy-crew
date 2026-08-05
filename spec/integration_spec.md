# SPEC: Presentation bridge & integration — headless half  (Director → Systems Engineer)

Game: **Stratocracy** — UE5.8 hex turn-based strategy. Headless (§4.1): **zero
engine dependencies**, pure C++17, deterministic.

This is **§4.9 SPEC STUB 9**, §4.11 build-order **row 9**. Unlike rows 1–8 it
builds no rules and adds no C++: its subject is the *vendoring step* between the
crew repo and the UE project, and its two headless invariants assert that the
step preserved what §3's ledger says it preserved.

## What this row is, and what it is not

**§4.9's premise is that integration adds Unreal *around* the rules module and
never adds Unreal *to* it.** The module's whole value is that it has no engine
dependencies, so the moment a copy of it lives inside a UE project there are two
copies, and the ledger's evidence chain is only as good as the claim that they
are the same bytes.

That is the entire job of this row's headless half. It does **not** build the
bridge — the load mapping, the command/event surface, the actors and widgets of
§4.9 part 2 — and it asserts nothing about them.

## The Director's scope ruling — read this before scoping anything

**Row 9's acceptance set is split across two harnesses and this build is the
headless one.** §4.9's own Acceptance line makes the split:

> Acceptance: T-INT-01, 04 on every gate run; T-INT-02, 03, 05 in the editor pass.

- **T-INT-01** and **T-INT-04** need no editor and no engine. They run here, on
  every gate run, and §4.9 says of T-INT-04 that *"the gate run itself is the
  assert"*.
- **T-INT-02** (replay parity), **T-INT-03** (rejection safety) and **T-INT-05**
  (presentation statelessness) are the editor pass. **No in-editor Automation
  harness exists**, so they did not run.

**THE LEDGER ROW THEREFORE DOES NOT FLIP.** Q29 requires the full acceptance set
at one commit, per acceptance ID as well as per row. This is row 2's posture on
T-DATA-05, row 7's on its stretch-map fixtures and row 8's on T-UI-03/04 — a
partial pass, recorded as one.

**T-INT-03 is in the editor pass, and the `†` marks are not in conflict with that
— do not re-derive this wrongly.** §4.11's row-9 cell marks only **T-INT-02 and
T-INT-05**, which can look like a disagreement with the Acceptance line above.
It is not one, and the master says so in as many words: *"**T-INT-03 stays
unmarked on the rule, not on cost:** §4.9 does place it in the editor pass, but
what it asserts … is the bridge behaviour §4.9 contracts …, and a marked ID may
not guard a rules invariant."* The dagger tracks **cut-line membership** — what
stands down if the calendar slips — **not headless-versus-editor**. §4.5's clause
that the build-order table is authoritative governs which side of the *cut line*
an ID sits on, which is a different question.

So T-INT-03 does not run here, for the same reason T-INT-02 and T-INT-05 do not.
Its subject is besides the canonical state hash, which §4.10 owns and **row 10
has not built**.

## What gets vendored — derived, not chosen

§4.9 requires `Source/StratRules/` to hold *"no engine headers, no UObject, no
third-party includes — pure C++17 in `namespace strat`"*, and a UBT module cannot
contain a second `main()`. That excludes every `test_*.cpp`, `driver_main.cpp`
and `selfplay.cpp`. The `*.buggy.cpp` files are the deliberately-wrong pass-1
fixtures the gate blocks on and are not shippable code.

What remains is the **ten modules, header and implementation** — Combat, Hex,
Data, Move, Economy, Turn, Ai, Scenario, Ui, Driver — **twenty files**. `Driver`
is in the set and is not optional: `Ai.good.cpp` links against it, which is why
§4.11 row 6's gate carries `Driver.cpp` too.

**Names are unchanged.** `cpp_reference/Ui.good.cpp` vendors as
`Source/StratRules/Ui.good.cpp`. UBT globs `*.cpp`, so the suffix costs nothing,
and it makes T-INT-01 a path-for-path identity with no rename map for a later
reader to get wrong.

## The vendor step

`sync_stratrules.py`, in this repo. **It reads the sources from the git object
store, not from the working tree** — `git show <commit>:cpp_reference/<f>`. That
is load-bearing, not tidiness: it makes source identity true *by construction* at
the moment the script finishes, so the only ways T-INT-01 can later fail are the
ones it exists to catch — a hand edit to a vendored file, an added or deleted
file, or crew moving on without a re-sync. Vendoring the working tree would let a
dirty edit be recorded under a clean commit hash, which is precisely the failure
T-INT-01 would then be unable to see.

It writes two further files, and **neither is excluded from the check any more.**
`StratRules.Build.cs` is now tracked in this repo at `ue_module/` and is vendored
from the object store like the sources, so it hash-matches a blob at
`rulesCommit`. `StratRules.manifest.json` records `rulesCommit` itself, and is
rebuilt from `ue_module/manifest_fields.json` plus the re-derived hashes.

**Why the manifest is recomputed rather than hash-matched.** It cannot be stored
in this repo at the commit it names: a file's bytes cannot contain the sha of the
tree that holds them. That is a fixed point, not a gap — recomputation is the
strongest check available on it, and it is written out in `crew/tools.py` rather
than imported from the vendor script, so it asserts something about the generator
instead of restating it.

## Invariants

```
  T-INT-01  source identity. EVERY file in Source/StratRules/ is accounted for at
            the recorded rulesCommit -- nothing is exempt -- by two mechanisms:

              hash-match      the 20 sources against cpp_reference/<same name>,
                              and StratRules.Build.cs against
                              ue_module/StratRules.Build.cs
              recomputation   StratRules.manifest.json is rebuilt from
                              ue_module/manifest_fields.json plus the
                              independently re-derived hashes, and compared
                              byte-for-byte

            The accounted SET is exactly those 22 names, so a missing file and an
            unexpected file are both findings; and rulesCommit resolves in this
            repo.

            THE CHECK DOES NOT TRUST THE MANIFEST'S OWN HASHES, and does not
            import the vendor script's file list. It takes only rulesCommit from
            the manifest and re-derives both the expected set (`git ls-tree`) and
            every expected hash (`git show`) from the commit itself. A check that
            compared the manifest to the disk would pass on any edit that changed
            both, and a check that shared the vendor script's list would be blind
            to a module the script silently dropped.

  T-INT-04  no engine deps. Every vendored implementation compiles STANDALONE —
            outside UBT, under the single compiler the gate detects (the first of
            g++, clang++, c++, cl on PATH). Compilation is to an OBJECT file: the
            module has no main() by construction, so object compilation is what
            "compiles standalone" can mean here. It compiles the VENDORED copy,
            not cpp_reference/, which is the only way it can witness an engine
            header that vendoring introduced. §4.9: any one compiler clean
            satisfies this; it does not require all four.
```

Determinism: both invariants are pure functions of the two trees. Neither writes
to the UE project.

Acceptance: **T-INT-01, T-INT-04** headless, on every gate run
(`python run.py --integration`, and at the end of `--week1`). T-INT-02, T-INT-03,
T-INT-05 in the editor pass, which does not exist.

## The gate must be shown to fail — it was

A gate that has never failed has never been shown to be a gate. Five known-bad
inputs, each restored afterwards:

| Known-bad input | Result |
|---|---|
| a comment appended to a vendored source | T-INT-01 FAIL (hash mismatch); T-INT-04 PASS |
| the same, **plus** the manifest's own hash updated to match | T-INT-01 **still** FAIL |
| a vendored header deleted | T-INT-01 FAIL (missing); T-INT-04 FAIL — `Ai.good.cpp` genuinely stops compiling |
| an extra `Sneaky.good.cpp` added | T-INT-01 FAIL (unexpected); T-INT-04 compiled **11**, not 10 |
| `#include "CoreMinimal.h"` injected | T-INT-04 FAIL; T-INT-01 FAIL, since the bytes changed |

The second row is the one that matters: it is what distinguishes this check from
a manifest-vs-disk comparison, which every one of these edits would pass.

**The widening was shown to be a widening**, by running the three new known-bad
inputs against the check as it stood BEFORE the change and again after. A gate
that passes after an extension has not been shown to have gained anything; a
gate that passed the same input *before* it has:

| Known-bad input | Before widening | After |
|---|---|---|
| a comment appended to `StratRules.Build.cs` | T-INT-01 **PASS** — undetected | T-INT-01 FAIL |
| the manifest's `note` altered | T-INT-01 **PASS** — undetected | T-INT-01 FAIL |
| the manifest's `generator` altered | T-INT-01 **PASS** — undetected | T-INT-01 FAIL |
| an extra `Sneaky.good.cpp` added (control) | T-INT-01 FAIL | T-INT-01 FAIL |

Two further controls on the new mechanisms, both as designed: editing
`ue_module/StratRules.Build.cs` **in the working tree** does not move the verdict,
because the check reads the blob at `rulesCommit`; and tampering a vendored source
*together with* its recorded hash in the manifest still FAILs, which is the
manifest-hash control carried over to the widened check.

## When the UE project is absent

The gate **SKIPS with the reason stated and claims nothing** — it does not pass.
A gate that cannot see its subject must not report on it, and a vacuous pass here
would put a green T-INT-01 beside a vendoring that never happened.

## What this row does NOT do, stated so it is not inferred

- **The module IS wired into a build target, and that closes nothing.** Until
  2026-08-05 `Stratocracy.uproject` did not list it and no target depended on it,
  so UBT never compiled it. It now lists it, and `StratocracyEditor` links
  `UnrealEditor-StratRules.dll`. **In-engine compilation is what the editor pass
  gates, and T-INT-04 deliberately asserts the standalone compile instead**, so a
  green UBT build moves no acceptance ID and is not evidence for one. The first
  UBT attempt **failed**: both targets set `BuildSettingsVersion.V7`, and V2
  onward raises `ShadowVariableWarningLevel` to `Error`, so `Driver.good.cpp`'s
  shadowed local `r` stopped the build as `error C4456`. The other nine modules
  compiled clean. The fix is one line in `ue_module/StratRules.Build.cs`
  downgrading that warning for this module only — chosen over editing the
  certified source, which would have re-dated T-INT-04's closure as well as
  T-INT-01's. **UBT compiles these sources as C++20** with MSVC strict
  conformance (`BuildSettingsVersion` V4 onward), while the standalone gate
  compiles them as C++17: the same bytes, two language standards. That is filed
  as a change request against §4.9's "pure C++17" wording and is **not** a
  finding about the sources, which compile clean under both.
- **No bridge exists.** No load mapping, no command submission, no event list, no
  actor and no widget. §4.9 part 2 is unbuilt.
- **The canonical state hash HAS SINCE BEEN BUILT**, as §4.11 **row 10**'s part
  (b) — `cpp_reference/Replay.h`, where §4.10 defines it. T-INT-02 and T-INT-03
  therefore have a subject; what they still lack is the in-editor Automation
  harness, which is now the whole of what they wait on. The `stateHash` in
  `Driver.h` is the driver's own debug digest (`GATE-DRV-06`) and is a
  different thing from both.
- **No acceptance ID is minted**, and none of §4.5's counts moves on the build
  alone.
