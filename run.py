#!/usr/bin/env python3
"""Stratocracy agent crew — entrypoint (Assignment #3).

    python run.py            # live CrewAI crew if ANTHROPIC_API_KEY is set, else offline
    python run.py --offline  # force the deterministic no-API pipeline
    python run.py --online   # force the live crew (errors if no key)

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
    """GDD §4.11 rows 1-7 — the §4.4 week-1 deliverable and the rows built ahead of it.

    Deterministic on both paths: the live CrewAI crew in crew/tasks.py is written
    against the Combat spec only, so these three modules are authored from the bundled
    references and gated by the same real compile+run. Anything else would report a
    live authoring run that did not happen.
    """
    from crew.offline import run_week1 as _rw
    return _rw(log)


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


def main() -> int:
    # Each run must re-earn both acceptance records; neither is ever inherited.
    (BUILD / "acceptance.json").unlink(missing_ok=True)
    (BUILD / "acceptance_week1.json").unlink(missing_ok=True)
    args = set(sys.argv[1:])
    force_offline = "--offline" in args
    force_online = "--online" in args
    week1_only = "--week1" in args
    have_key = bool(os.environ.get("ANTHROPIC_API_KEY"))

    header = ""
    try:
        if week1_only:
            run_week1_stage()
            header = "week 1 only (§4.11 rows 1-7)"
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
        _flush_log(header or "run")

    if week1_only:
        log(f"\nArtifacts in {BUILD}/ : Hex.cpp, Data.cpp, Move.cpp, Economy.cpp, "
            "Turn.cpp, Ai.cpp, Scenario.cpp, Driver.cpp, stratocracy_debug, "
            "acceptance_week1.json, run_log.md")
    else:
        log(f"\nArtifacts in {BUILD}/ : Combat.cpp, test_combat.cpp, selfplay.cpp, "
            "balance_report.md, acceptance.json, Hex.cpp, Data.cpp, Move.cpp, "
            "acceptance_week1.json, run_log.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
