#!/usr/bin/env python3
"""Stratocracy agent crew — entrypoint (Assignment #3).

    python run.py              # live CrewAI crew if ANTHROPIC_API_KEY is set, else offline
    python run.py --offline    # force the deterministic no-API pipeline
    python run.py --online     # force the live crew (errors if no key)
    python run.py --week1      # §4.11 rows 1-8 + 10(a) + the driver, then row 9's headless half
    python run.py --integration  # §4.11 row 9 alone: T-INT-01, T-INT-04

Always produces build/run_log.md, build/Combat.cpp, and build/balance_report.md, and
never crashes: if the live crew errors (missing key, network, etc.) it falls back to the
deterministic pipeline so there is always a runnable, gate-verified result.

Exit code -- the verdict, machine-readable:

    0  every gate that ran, passed.
    1  a gate ran and FAILED.
    2  a gate could not run, so nothing was measured (no C++ compiler on PATH, no UE
       checkout for row 9). Never folded into 0: an unmeasured gate is not a passed one.
    3  an exception escaped the run itself.

Not crashing is not the same as passing, and `--offline` on a machine with no compiler is
the case that separates them.
"""
from __future__ import annotations

import os
import sys
import traceback
from pathlib import Path

# Load ANTHROPIC_API_KEY (and any other vars) from a local .env if present.
# python-dotenv ships with crewai; the try/except keeps the offline path working
# even if it isn't installed.
try:
    from dotenv import load_dotenv
    load_dotenv(Path(__file__).resolve().parent / ".env")
except Exception:
    pass

BUILD = Path(__file__).resolve().parent / "build"
BUILD.mkdir(exist_ok=True)
LOG_PATH = BUILD / "run_log.md"
_lines: list[str] = []

# The logs carry GDD notation — section marks, arrows, the cut-line dagger. When stdout
# is a console Python picks a codepage that can render them; when it is a PIPE or a file
# on Windows it falls back to cp1252 and a bare `print('→')` raises
# UnicodeEncodeError, which took the whole run down with it. Redirected output is how
# CI and `python run.py > log.txt` both work, so make the stream tolerant instead of
# rationing the characters. run_log.md is written as UTF-8 either way.
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:  # pragma: no cover — older/odd streams keep the default behaviour
    pass


def log(msg: str = "") -> None:
    print(msg)
    _lines.append(msg)


def _flush_log(header: str) -> None:
    LOG_PATH.write_text(f"# Stratocracy crew — run log\n\n_{header}_\n\n```\n"
                        + "\n".join(_lines) + "\n```\n", encoding="utf-8")


# --- The verdict, and the exit code derived from it ------------------------------- #
#
# `main` used to end in a bare `return 0`. Measured 2026-08-20: `python run.py --offline`
# on a machine with no C++ compiler printed "[stop] No usable C++ compiler on PATH",
# wrote NEITHER acceptance record, and exited 0 -- so any CI step, shell `&&`, or reader
# who trusts the exit code read "the crew certified the build" off a run that had not
# compiled a single line. A gate that cannot run must not be able to report a pass.
#
# So the exit code is DERIVED from what the stages returned, never typed. Three states,
# because two would have to lie about one of them: passed, failed, and did-not-run.

EXIT_OK, EXIT_FAILED, EXIT_NOT_RUN, EXIT_CRASHED = 0, 1, 2, 3

PASSED, FAILED, NOT_RUN = "passed", "FAILED", "did not run"

_verdicts: list[tuple[str, str, str]] = []


def _verdict_of(result: dict) -> tuple[str, str]:
    """Read a stage's own return value as (verdict, why). Two shapes are in play.

    The offline/week-1 stages return {"status", "gate_passed", ...}; the integration gate
    returns {"ran", "passed", ...} and sets ran=False when it SKIPS for want of a subject.
    Both already distinguish "could not run" from "ran and failed" -- `main` was simply
    discarding the distinction. Nothing here re-decides a verdict a stage has issued.
    """
    if "ran" in result:  # integration gate
        if not result.get("ran"):
            return NOT_RUN, result.get("summary", "skipped, nothing was checked")
        # A gate can be partly gradeable: T-INT-01 holds while T-INT-04 has no compiler
        # to run. A failure outranks a skip, but a skip never rounds down to a pass.
        if result.get("failures"):
            return FAILED, "failing: " + ", ".join(result["failures"])
        if result.get("skipped"):
            return NOT_RUN, "could not run: " + ", ".join(result["skipped"])
        return (PASSED, "") if result.get("passed") else (FAILED, "gate did not pass")
    status = result.get("status")
    if status == "no_compiler":
        return NOT_RUN, "no C++ compiler on PATH -- nothing was compiled or gated"
    if status == "no_combat":
        # Nothing in week 1 failed; week 1 was never graded, because the Combat.cpp
        # every row links was not the certified one.
        return NOT_RUN, result.get("provenance", "uncertified build/Combat.cpp")
    if status != "ok":
        return FAILED, f"stage reported status={status!r}"
    return (PASSED, "") if result.get("gate_passed") else (FAILED, "gate did not pass")


def record(stage: str, result: dict) -> dict:
    """Register a stage's verdict and hand its result straight back to the caller."""
    verdict, why = _verdict_of(result)
    _verdicts.append((stage, verdict, why))
    return result


def _exit_code() -> int:
    """The verdict of the whole run. Also LOGS it, so run_log.md and the exit code cannot
    disagree -- the closing line and the number a script reads come from the same list."""
    log("")
    log("=" * 78)
    if not _verdicts:
        log("VERDICT: did not run -- no gate reported at all.")
        log("=" * 78)
        return EXIT_NOT_RUN
    for stage, verdict, why in _verdicts:
        log(f"VERDICT: {stage} -- {verdict}" + (f" ({why})" if why else ""))
    log("=" * 78)
    if any(v == FAILED for _, v, _ in _verdicts):
        return EXIT_FAILED
    # A run where something was skipped is incomplete, not green. `--week1` on a machine
    # with no UE checkout passes rows 1-8 and never sees row 9; exiting 0 would report a
    # coverage it did not have.
    if any(v == NOT_RUN for _, v, _ in _verdicts):
        return EXIT_NOT_RUN
    return EXIT_OK


def run_live() -> None:
    from crew.crew import build_crew
    from crew.tools import certify_build_fn, run_self_play_fn, IMPL
    log("MODE: live CrewAI crew (Anthropic API).\n")
    crew = build_crew(verbose=True)
    result = crew.kickoff(inputs={"game": "Stratocracy"})

    # The live console shows the agents' step-by-step tool calls, but the SAVED log
    # otherwise captured only the final narrative. Re-run the gate and self-play here and
    # write the full evidence trail — gate verdict, the agent-authored source, and the
    # duel table — so run_log.md is complete, submittable proof on its own.
    log("=== Test Engineer certification (for the record) ===")
    g = certify_build_fn()
    record("live crew certification", {"status": "ok", "gate_passed": g["accepted"]})
    log("[Test Engineer] certify_build -> " + g["summary"] + f" | accepted={g['accepted']}")
    for line in g["log"].splitlines():
        log("    " + line)

    combat = BUILD / IMPL
    if combat.exists():
        log("\n=== Agent-authored build/Combat.cpp ===")
        for line in combat.read_text(encoding="utf-8").splitlines():
            log("    " + line)

    log("\n=== Self-play (Balance Analyst input) ===")
    b = run_self_play_fn()
    for line in b["log"].splitlines():
        log("    " + line)
    (BUILD / "balance_report.md").write_text(
        "# Balance report (self-play)\n\n```\n" + b["log"] + "\n```\n", encoding="utf-8")

    log("\n=== Final crew narrative (Balance Analyst) ===\n" + str(result))

    # Cost, measured off the run that just happened (Assignment #10, D3). Read from the
    # per-agent TokenProcess CrewAI filled during kickoff, so this prices THIS run and
    # nothing else. A failure here must not lose a completed crew run, so it is caught:
    # the gate verdict and the authored source above are the expensive artefacts.
    try:
        from crew import usage as usage_mod
        from crew.agents import DEFAULT_MODEL

        label = os.environ.get("STRATOCRACY_COST_LABEL", "live crew run")
        stem = os.environ.get("STRATOCRACY_COST_STEM", "cost_report")
        report = usage_mod.collect(crew, DEFAULT_MODEL, label)
        # evidence/, not build/: build/ is gitignored and is overwritten by the next run,
        # so a cost report written there would never reach a clone and could not be the
        # measured figure the assignment asks a grader to check. Same reasoning that put
        # evidence/live_run.md where it is.
        json_path, md_path = usage_mod.write_report(
            report, Path(__file__).resolve().parent / "evidence", stem)

        log("\n=== Measured cost for this run ===")
        for line in usage_mod.render_markdown(report).splitlines():
            log("    " + line)
        log(f"    -> {json_path.name}, {md_path.name}")
    except Exception as exc:  # noqa: BLE001 -- see comment above
        log(f"\n[warn] cost measurement failed: {exc!r}")
        log("       The crew run itself stands; only the cost report is missing.")

    run_week1_stage()


def run_week1_stage() -> dict:
    """GDD §4.11 rows 1-8 and row 10 part (a) — the §4.4 week-1 deliverable and the rows built ahead of it.

    Deterministic on both paths: the live CrewAI crew in crew/tasks.py is written
    against the Combat spec only, so these three modules are authored from the bundled
    references and gated by the same real compile+run. Anything else would report a
    live authoring run that did not happen.
    """
    # 11 of the 12 week-1 rows link build/Combat.cpp and none of them author it, so the
    # stage is only gradeable if that file is the one the Test Engineer certified. Checked
    # here rather than in `main` so it covers every caller of the stage, and checked
    # before a single module is compiled so the reason is the first thing in the log
    # instead of a row-1 BLOCK the reader has to work backwards from.
    from crew.tools import combat_provenance_fn
    prov = combat_provenance_fn()
    if not prov["ok"]:
        log("[stop] week 1 NOT RUN -- " + prov["reason"] + ".")
        log("       11 of the 12 week-1 rows link build/Combat.cpp, so a failure in "
            "them would be about Combat and not about week 1. Run "
            "`python run.py --offline` (or --online) first to author and certify "
            "Combat, then re-run --week1.")
        return record("week 1 (rows 1-8 + 10a)",
                      {"status": "no_combat", "gate_passed": False,
                       "provenance": prov["reason"]})
    log("[Test Engineer] combat provenance OK -- " + prov["reason"] + ".")
    from crew.offline import run_week1 as _rw
    return record("week 1 (rows 1-8 + 10a)", _rw(log))


def run_integration_stage() -> dict:
    """GDD §4.11 row 9 — §4.9 Spec Stub 9's headless half: T-INT-01 and T-INT-04.

    Asserts over the UE project's vendored Source/StratRules/, not over cpp_reference/.
    §4.9's Acceptance line runs these two "on every gate run", so --week1 calls this
    too; --integration runs it alone. When no UE checkout is present it SKIPS with the
    reason stated and claims nothing — a gate that cannot see its subject must not
    report on it.
    """
    from crew.tools import run_integration_gate_fn
    log("\n" + "=" * 78)
    log("ROW 9 — §4.9 integration, headless half (T-INT-01, T-INT-04)")
    log("=" * 78 + "\n")
    r = record("row 9 integration", run_integration_gate_fn())
    log("[Test Engineer] " + r["summary"])
    for line in r["log"].splitlines():
        log("    " + line)
    return r


def run_offline(reason: str = "") -> None:
    from crew.offline import run_offline as _ro
    res = record("combat pipeline", _ro(log, reason))
    if res.get("status") != "ok":
        return  # no compiler / gate failed — offline already logged a clear reason
    from crew.tools import run_self_play_fn
    b = run_self_play_fn()
    (BUILD / "balance_report.md").write_text(
        "# Balance report (self-play)\n\n"
        f"Gate passed: {res['gate_passed']} · hallucination caught by: "
        f"{', '.join(res['failures_caught'])}\n\n```\n" + b["log"] + "\n```\n",
        encoding="utf-8")
    run_week1_stage()


# The artifact each mode is SUPPOSED to leave behind. Declared rather than globbed, so a
# module that silently stopped writing one is visible -- but the closing line is derived
# from the directory, never from this list. See `_log_artifacts`.
EXPECTED_INTEGRATION = ["stratrules_obj", "run_log.md"]
EXPECTED_WEEK1 = ["Hex.cpp", "Data.cpp", "Move.cpp", "Economy.cpp", "Turn.cpp", "Ai.cpp",
                  "Scenario.cpp", "Ui.cpp", "Save.cpp", "Driver.cpp", "stratocracy_debug",
                  "acceptance_week1.json", "run_log.md"]
EXPECTED_FULL = ["Combat.cpp", "test_combat.cpp", "selfplay.cpp", "balance_report.md",
                 "acceptance.json", "Hex.cpp", "Data.cpp", "Move.cpp",
                 "acceptance_week1.json", "run_log.md"]


def _found(name: str) -> str | None:
    """The artifact's real filename in build/, or None. Tolerant of the platform suffix:
    the driver is `stratocracy_debug` on POSIX and `stratocracy_debug.exe` on Windows."""
    if (BUILD / name).exists():
        return name
    for alt in sorted(BUILD.glob(name + ".*")):
        return alt.name
    return None


def _log_artifacts(expected: list[str]) -> None:
    """Report what this run ACTUALLY left in build/, and name anything it did not.

    THIS USED TO BE A TYPED STRING AND IT LIED. Measured 2026-08-20 on a run that stopped
    at "no C++ compiler on PATH": the closing line still announced `acceptance_week1.json`,
    which `main` deletes up front and only `certify_week1_fn` writes -- so the run named an
    artifact it had just failed to produce, to a reader with no reason to doubt it. A list
    of names is not evidence that the names exist; the directory is.

    `run_log.md` is written moments after this by `_flush_log` and is reported as produced
    on that basis -- the one name here that is a promise rather than an observation, and it
    is said out loud rather than quietly counted as present.

    WHAT "PRESENT" DOES AND DOES NOT MEAN. build/ is not emptied between runs, so a file
    listed here may be one an EARLIER run left. The line says what is in the directory,
    which is what it is worth. The two acceptance records are the exception and the
    stronger reading: `main` deletes both before the run, so their presence does mean this
    run earned them -- which is exactly why their ABSENCE was worth catching.
    """
    found, missing = [], []
    for name in expected:
        if name == "run_log.md":
            found.append(name + " (written on exit)")
            continue
        real = _found(name)
        (found if real else missing).append(real or name)
    log(f"\nArtifacts in {BUILD}/ : " + (", ".join(found) if found else "(none)"))
    if missing:
        log("NOT PRODUCED by this run, and nothing here claims otherwise: "
            + ", ".join(missing))


def main() -> int:
    args = set(sys.argv[1:])
    force_offline = "--offline" in args
    force_online = "--online" in args
    week1_only = "--week1" in args
    integration_only = "--integration" in args

    # A run re-earns the records IT CAN PRODUCE, and leaves the others alone. This used
    # to delete both unconditionally, which meant `--week1` destroyed the combat
    # certification -- the very record the provenance check above needs to read, written
    # by a `--offline` run that had done nothing wrong. Deleting another run's evidence
    # is not the same discipline as refusing to inherit your own.
    if not (week1_only or integration_only):
        (BUILD / "acceptance.json").unlink(missing_ok=True)
    if not integration_only:
        (BUILD / "acceptance_week1.json").unlink(missing_ok=True)
    have_key = bool(os.environ.get("ANTHROPIC_API_KEY"))

    header = ""
    crashed = False
    try:
        if integration_only:
            run_integration_stage()
            header = "integration only (§4.11 row 9, headless half)"
        elif week1_only:
            run_week1_stage()
            run_integration_stage()
            header = "week 1 only (§4.11 rows 1-8 + 10(a)) + row 9's headless half"
        elif force_online or (have_key and not force_offline):
            try:
                run_live()
                header = "live CrewAI crew"
            except Exception as e:  # graceful degradation — never crash the submission
                log(f"\n[warn] live crew failed ({type(e).__name__}: {e}). "
                    "Falling back to the deterministic pipeline.\n")
                run_offline(f"live crew failed: {type(e).__name__}")
                header = "offline (live crew fell back)"
        else:
            # The two ways this branch is reached are different facts and are reported as
            # different facts. `--offline` with a key present is not "no API key".
            run_offline("--offline requested" if force_offline
                        else "no ANTHROPIC_API_KEY")
            header = "offline deterministic pipeline"
    except Exception:
        # The stage raised. Previously this escaped `main` and Python exited 1 with a
        # traceback on stderr and NOTHING in run_log.md -- the record went quiet on the
        # one run that most needed explaining. Put the traceback in the log, and give the
        # crash its own code so it is not confused with a gate that ran and failed.
        crashed = True
        header = (header or "run") + " (crashed)"
        log("")
        log("[crash] the run raised before finishing:")
        for ln in traceback.format_exc().rstrip().splitlines():
            log("    " + ln)
    finally:
        # Inside the `finally` and BEFORE the flush, so the record carries the artifact
        # report too. It used to sit after `_flush_log`, which meant the one place a
        # reader goes back to -- run_log.md -- was the one place that never had it.
        _log_artifacts(EXPECTED_INTEGRATION if integration_only else
                       EXPECTED_WEEK1 if week1_only else EXPECTED_FULL)
        code = EXIT_CRASHED if crashed else _exit_code()
        log(f"exit code: {code}")
        _flush_log(header or "run")

    return code


if __name__ == "__main__":
    raise SystemExit(main())
