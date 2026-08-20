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
"""
from __future__ import annotations

import os
import sys
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

    run_week1_stage()


def run_week1_stage() -> dict:
    """GDD §4.11 rows 1-8 and row 10 part (a) — the §4.4 week-1 deliverable and the rows built ahead of it.

    Deterministic on both paths: the live CrewAI crew in crew/tasks.py is written
    against the Combat spec only, so these three modules are authored from the bundled
    references and gated by the same real compile+run. Anything else would report a
    live authoring run that did not happen.
    """
    from crew.offline import run_week1 as _rw
    return _rw(log)


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
    r = run_integration_gate_fn()
    log("[Test Engineer] " + r["summary"])
    for line in r["log"].splitlines():
        log("    " + line)
    return r


def run_offline() -> None:
    from crew.offline import run_offline as _ro
    res = _ro(log)
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
    # Each run must re-earn both acceptance records; neither is ever inherited.
    (BUILD / "acceptance.json").unlink(missing_ok=True)
    (BUILD / "acceptance_week1.json").unlink(missing_ok=True)
    args = set(sys.argv[1:])
    force_offline = "--offline" in args
    force_online = "--online" in args
    week1_only = "--week1" in args
    integration_only = "--integration" in args
    have_key = bool(os.environ.get("ANTHROPIC_API_KEY"))

    header = ""
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
                run_offline()
                header = "offline (live crew fell back)"
        else:
            run_offline()
            header = "offline deterministic pipeline"
    finally:
        # Inside the `finally` and BEFORE the flush, so the record carries the artifact
        # report too. It used to sit after `_flush_log`, which meant the one place a
        # reader goes back to -- run_log.md -- was the one place that never had it.
        _log_artifacts(EXPECTED_INTEGRATION if integration_only else
                       EXPECTED_WEEK1 if week1_only else EXPECTED_FULL)
        _flush_log(header or "run")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
