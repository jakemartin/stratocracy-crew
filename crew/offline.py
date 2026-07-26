"""Deterministic offline pipeline — the same spec→gate→balance flow without any API.

Runs when no ANTHROPIC_API_KEY is set (or with `--offline`). It uses bundled reference
implementations to stand in for the Systems Engineer's authorship, so the whole crew
executes end-to-end on any machine with a C++ compiler and never crashes.

It also *demonstrates the gate catching a hallucination*: pass 1 deliberately submits
the over-generalized counter rule (Artillery counters at range 1), the gate blocks it on
T-COMBAT-07, then pass 2 submits the corrected rule and the gate passes.
"""
from __future__ import annotations

from . import tools


def run_offline(log) -> dict:
    log("MODE: offline deterministic pipeline (no API key) — the crew's spec→gate→"
        "balance flow with bundled authorship.\n")

    # --- Systems Engineer, pass 1: the hallucinated implementation --------------
    log("[Director -> Systems Engineer] spec/combat_spec.md handed over.")
    log(tools.write_combat_impl_fn(tools.read_reference("Combat.buggy.cpp")))
    log("[Systems Engineer] pass 1 authored (counter rule = 'distance <= rangeMax').\n")

    # --- Test Engineer: gate blocks pass 1 --------------------------------------
    r1 = tools.run_test_gate_fn()
    if not r1["compiled"]:
        log("[Test Engineer] compile FAILED — " + r1["log"])
        log("\n[stop] No usable C++ compiler on PATH. On Windows, open the "
            "'x64 Native Tools Command Prompt for VS' (so cl.exe is on PATH) and re-run; "
            "or install g++/clang++. Nothing else is wrong — the crew just can't build.")
        return {"status": "no_compiler", "gate_passed": False, "failures_caught": []}
    log("[Test Engineer] " + r1["summary"])
    for line in r1["log"].splitlines():
        log("    " + line)
    if r1["passed"]:
        log("[note] pass 1 unexpectedly passed — the bundled 'buggy' impl should fail "
            "T-COMBAT-07; continuing anyway.\n")
    else:
        log(f"[Test Engineer] MERGE BLOCKED — {', '.join(r1['failures'])} caught the "
            "hallucinated counter rule. Handing back to Systems Engineer.\n")

    # --- Systems Engineer, pass 2: corrected implementation ---------------------
    log("[Systems Engineer] re-fed invariant 7; correcting the range-band check.")
    log(tools.write_combat_impl_fn(tools.read_reference("Combat.good.cpp")))
    log("[Systems Engineer] pass 2 authored (counter honors [rangeMin, rangeMax]).\n")

    r2 = tools.run_test_gate_fn()
    log("[Test Engineer] " + r2["summary"])
    for line in r2["log"].splitlines():
        log("    " + line)
    if not r2["passed"]:
        log("[stop] pass 2 did not pass the gate — see the failures above.")
        return {"status": "error", "gate_passed": False,
                "failures_caught": r1.get("failures", [])}
    log("[Test Engineer] MERGE APPROVED.\n")

    # --- Balance Analyst --------------------------------------------------------
    b = tools.run_self_play_fn()
    log("[Balance Analyst] " + b["summary"])
    for line in b["log"].splitlines():
        log("    " + line)
    log("\n[Balance Analyst] Read-out: Tank dominates every 1v1 on plains; Artillery "
        "loses to melee attackers because the sim forces distance=1, eating counters it "
        "would avoid at standoff. Proposal: add range-2/3 duels to the sim before tuning "
        "Artillery's stats — the weakness is a methodology artifact, not (yet) a balance bug.")

    return {"status": "ok", "gate_passed": r2["passed"],
            "failures_caught": r1.get("failures", [])}
