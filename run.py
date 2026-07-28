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


def main() -> int:
    (BUILD / "acceptance.json").unlink(missing_ok=True)  # each run must re-earn acceptance
    args = set(sys.argv[1:])
    force_offline = "--offline" in args
    force_online = "--online" in args
    have_key = bool(os.environ.get("ANTHROPIC_API_KEY"))

    header = ""
    try:
        if force_online or (have_key and not force_offline):
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

    log(f"\nArtifacts in {BUILD}/ : Combat.cpp, test_combat.cpp, selfplay.cpp, "
        "balance_report.md, run_log.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
