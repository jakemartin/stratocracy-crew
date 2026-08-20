#!/usr/bin/env python3
"""Read the verdict of a `run.py --week1` run, and exit non-zero when it did not pass.

    python gate_ci_verdict.py              # grade the last run in build/, exit 0/1
    python gate_ci_verdict.py --self-test  # 8 fixtures proving this script can FAIL

WHY THIS EXISTS, AND IT IS NOT A STYLE PREFERENCE. `run.py --week1` USED TO EXIT 0
WHEN IT COULD NOT RUN THE GATE AT ALL. Measured 2026-08-20 on a shell with no C++
compiler on PATH: the run printed `[stop] No usable C++ compiler on PATH`, then
`INTEGRATION GATE BLOCK — failing: T-INT-04`, and returned exit code 0. A CI job of
the shape `run: python run.py --week1` was therefore green on a run that asserted
nothing -- the exact shape this project has now paid for four times in its guards.

THAT HOLE IS NOW CLOSED AT THE SOURCE, and this script is kept anyway. `run.py`
derives its exit code from the stages' own verdicts (0 passed / 1 failed / 2 could
not run / 3 crashed), so the measured run above now exits 2. Two things survive that
fix and are why this file still runs in CI: an exit code says the run did not pass
but not that it COULD have failed -- check 4 reads the pipeline's own falsifiability
controls, which no exit code carries -- and this grader reads the RECORD on disk, so
it also grades a build/ left behind by a run nobody watched.

The same run also printed `Artifacts in .../build/ : ... acceptance_week1.json ...`
while that file did not exist: `run.py` deletes the record before the run and the
only writer is `certify_week1_fn`, which the compiler stop never reached. So the
closing line names artifacts it did not produce, and the artifact's ABSENCE is
itself a verdict this script reads rather than a missing input it tolerates.

WHAT IT GRADES, and all six must hold:

  1. RECORD PRESENT     build/acceptance_week1.json exists and parses.
  2. RECORD ACCEPTED    its `accepted` field is true.
  3. NO EARLY STOP      no `[stop]` line in build/run_log.md.
  4. CONTROLS WITNESSED at least one pass-1 buggy-module BLOCK, and zero
                        `pass 1 unexpectedly passed` notes. The crew's pipeline
                        proves its own falsifiability by compiling a known-bad
                        module first; a run where no control failed is a run whose
                        green half means nothing.
  5. WEEK-1 VERDICT     `WEEK-1 GATE PASS` present and no `WEEK-1 GATE BLOCK`.
  6. INTEGRATION VERDICT `INTEGRATION GATE PASS` present, and none of BLOCK,
                        SKIPPED or INCOMPLETE. A SKIPPED integration gate says in its
                        own words that "nothing was checked and nothing is claimed" --
                        which is honest, and is not a pass. INCOMPLETE is the partial
                        form of the same thing: T-INT-01 held and T-INT-04 had no
                        compiler to run under, so the gate is not blocked and is also
                        not green. All three are graded the same way here, because the
                        check keys on the ABSENCE of the word PASS rather than on a
                        list of bad words it would have to be kept in step with.

Check 4 reads the run's own controls rather than re-running anything: this script
compiles nothing and asserts nothing about the rules. It grades a record.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
RECORD = "acceptance_week1.json"
RUN_LOG = "run_log.md"

# A verdict line CONTAINS one of these tokens; a `GATE BLOCK` carrying neither is a
# per-row pass-1 control, which is a thing we REQUIRE rather than forbid.
#
# CONTAINS AND NOT `startswith`, and that correction was earned rather than reasoned:
# the first version keyed on the line's start, and every verdict in run_log.md is
# written `[Test Engineer] INTEGRATION GATE BLOCK — ...`. Run against the real stopped
# log it therefore counted the integration BLOCK as a pass-1 CONTROL and simultaneously
# reported "no INTEGRATION GATE PASS line" -- it reached the right overall verdict by
# two wrong readings. `_FIXTURES` now carries a log with the real speaker prefix, which
# is the fixture that would have caught it.
VERDICT_TOKENS = ("WEEK-1 GATE", "INTEGRATION GATE")


def _grade(record_text: str | None, log_text: str) -> list[tuple[str, bool, str]]:
    """Return [(check name, ok, detail)]. Pure over its inputs so --self-test can drive it."""
    out: list[tuple[str, bool, str]] = []

    # 1 + 2 -- the record.
    record = None
    if record_text is None:
        out.append(("RECORD PRESENT", False,
                    f"build/{RECORD} does not exist -- the run stopped before the Test "
                    "Engineer wrote it (run.py's closing line names it either way)"))
    else:
        try:
            record = json.loads(record_text)
            out.append(("RECORD PRESENT", True, f"build/{RECORD} parsed"))
        except json.JSONDecodeError as e:
            out.append(("RECORD PRESENT", False, f"build/{RECORD} is unreadable: {e}"))

    if record is None:
        out.append(("RECORD ACCEPTED", False, "no record to read"))
    else:
        accepted = record.get("accepted")
        out.append(("RECORD ACCEPTED", accepted is True,
                    f"accepted={accepted!r}"))

    # 3 -- an early stop.
    stops = [ln.strip() for ln in log_text.splitlines() if ln.lstrip().startswith("[stop]")]
    out.append(("NO EARLY STOP", not stops,
                stops[0] if stops else "no [stop] line"))

    # 4 -- the pipeline's own falsifiability controls.
    controls = [ln.strip() for ln in log_text.splitlines()
                if "GATE BLOCK" in ln and not any(t in ln for t in VERDICT_TOKENS)]
    surprises = [ln.strip() for ln in log_text.splitlines()
                 if "pass 1 unexpectedly passed" in ln]
    ok4 = bool(controls) and not surprises
    if surprises:
        detail4 = f"{len(surprises)} control(s) did not fail: {surprises[0]}"
    elif not controls:
        detail4 = ("no pass-1 control BLOCK in the log -- nothing shows this run could "
                   "have failed")
    else:
        detail4 = f"{len(controls)} pass-1 control BLOCK(s), 0 unexpectedly-passed notes"
    out.append(("CONTROLS WITNESSED", ok4, detail4))

    # 5 + 6 -- the two verdicts.
    for name, token in (("WEEK-1 VERDICT", "WEEK-1 GATE"),
                        ("INTEGRATION VERDICT", "INTEGRATION GATE")):
        lines = [ln.strip() for ln in log_text.splitlines() if token in ln]
        passes = [ln for ln in lines if f"{token} PASS" in ln]
        bad = [ln for ln in lines if f"{token} PASS" not in ln]
        if bad:
            out.append((name, False, bad[0]))
        elif not passes:
            out.append((name, False, f"no `{token} PASS` line in the log"))
        else:
            out.append((name, True, passes[-1]))

    return out


def _read(path: Path) -> str | None:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None


def grade_build_dir() -> int:
    log_text = _read(BUILD / RUN_LOG)
    if log_text is None:
        print(f"gate verdict: build/{RUN_LOG} does not exist -- no run to grade.")
        print("Run `python run.py --week1` first. REFUSING rather than passing vacuously.")
        return 1

    results = _grade(_read(BUILD / RECORD), log_text)
    print(f"Gate verdict: {BUILD}")
    for name, ok, detail in results:
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: {detail}")
    print()
    if all(ok for _, ok, _ in results):
        print("GATE VERDICT PASS -- the run ran, could have failed, and did not")
        return 0
    print("GATE VERDICT FAIL -- " + "; ".join(n for n, ok, _ in results if not ok))
    return 1


# ---- fixtures ---------------------------------------------------------------
# Every one of the six checks gets a fixture that FAILS it, plus a clean control.
_GOOD_LOG = """
[Systems Engineer] wrote 1966 bytes to build/Hex.cpp (row 1 - Hex grid & math)
[Test Engineer] row 3 (Movement & pathing) GATE BLOCK - failing: T-MOVE-02 (6/7)
[Test Engineer] row 3 (Movement & pathing) GATE PASS (7/7)
WEEK-1 GATE PASS - rows 1-3 and rows 4-8 (T-HEX-01..07, ...)
INTEGRATION GATE PASS - T-INT-01, T-INT-04 (2/2)
"""
_GOOD_RECORD = '{"accepted": true, "scope": "GDD 4.11 rows 1-8"}'

# The real log writes every verdict behind a speaker prefix. This is the shape the
# first version of the classifier got wrong, so it is a fixture and not a comment.
_SPOKEN_LOG = "\n".join(
    "[Test Engineer] " + ln if "GATE" in ln else ln for ln in _GOOD_LOG.splitlines())

_FIXTURES: list[tuple[str, str | None, str, bool]] = [
    ("clean run", _GOOD_RECORD, _GOOD_LOG, True),
    ("clean run, verdicts behind the real speaker prefix", _GOOD_RECORD, _SPOKEN_LOG, True),
    ("spoken-prefix integration BLOCK is a verdict, not a control", _GOOD_RECORD,
     _SPOKEN_LOG.replace("INTEGRATION GATE PASS - T-INT-01, T-INT-04 (2/2)",
                         "INTEGRATION GATE BLOCK - failing: T-INT-04 (1/2 passed)"), False),
    ("record missing (the measured compiler-stop shape)", None, _GOOD_LOG, False),
    ("record unreadable", "{not json", _GOOD_LOG, False),
    ("accepted false", '{"accepted": false}', _GOOD_LOG, False),
    ("early stop line", _GOOD_RECORD,
     _GOOD_LOG + "\n[stop] No usable C++ compiler on PATH.\n", False),
    ("no control ever failed", _GOOD_RECORD,
     _GOOD_LOG.replace("GATE BLOCK - failing: T-MOVE-02 (6/7)", "GATE PASS (7/7)"), False),
    ("a control unexpectedly passed", _GOOD_RECORD,
     _GOOD_LOG + "\n[note] pass 1 unexpectedly passed - the bundled 'buggy' impl should fail\n",
     False),
    ("integration gate skipped", _GOOD_RECORD,
     _GOOD_LOG.replace("INTEGRATION GATE PASS - T-INT-01, T-INT-04 (2/2)",
                       "INTEGRATION GATE SKIPPED - no UE project at ../Stratocracy"), False),
    # The partial form: T-INT-01 PASSED on this run. A grader keyed on "did anything
    # fail" would call this clean, which is the whole reason the word is not PASS.
    ("integration gate incomplete (T-INT-04 had no compiler)", _GOOD_RECORD,
     _GOOD_LOG.replace("INTEGRATION GATE PASS - T-INT-01, T-INT-04 (2/2)",
                       "INTEGRATION GATE INCOMPLETE - could not run: T-INT-04 "
                       "(1/2 passed, 1 could not run)"), False),
    ("week-1 gate blocked", _GOOD_RECORD,
     _GOOD_LOG.replace("WEEK-1 GATE PASS - rows 1-3 and rows 4-8 (T-HEX-01..07, ...)",
                       "WEEK-1 GATE BLOCK - row 3 failing: T-MOVE-02"), False),
]


def self_test() -> int:
    bad = 0
    for name, record, log, expect_clean in _FIXTURES:
        results = _grade(record, log)
        got_clean = all(ok for _, ok, _ in results)
        ok = got_clean == expect_clean
        bad += 0 if ok else 1
        verdict = "clean" if got_clean else "FAIL(" + ",".join(
            n for n, o, _ in results if not o) + ")"
        print(f"  {'ok  ' if ok else 'BAD '} {name}: expected "
              f"{'clean' if expect_clean else 'a failure'}, got {verdict}")
    print()
    if bad:
        print(f"SELF-TEST FAILED -- {bad} of {len(_FIXTURES)} fixtures graded wrong")
        return 1
    print(f"SELF-TEST CLEAN -- {len(_FIXTURES)} fixtures, "
          "each of the six checks shown able to fail")
    return 0


if __name__ == "__main__":
    sys.exit(self_test() if "--self-test" in sys.argv[1:] else grade_build_dir())
