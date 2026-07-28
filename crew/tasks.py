"""Tasks wire the fixed handoff order from GDD §3: spec → implementation → gate → balance.

CrewAI passes each task's output to the next via `context`, so the Test Engineer sees
what the Systems Engineer built and the Balance Analyst sees the gate result.
"""
from __future__ import annotations

from crewai import Task

from .agents import read_spec


def build_tasks(systems_engineer, test_engineer, balance_analyst):
    spec = read_spec()

    implement = Task(
        description=(
            "Implement the Stratocracy combat module from this spec:\n\n"
            f"{spec}\n\n"
            "Write the full contents of Combat.cpp (implementing resolveDamage and "
            "defenderCanCounter exactly as specified) and save it with write_combat_impl. "
            "Do not restate the spec; produce code. Pay attention to invariant 7: the "
            "counter check must honor BOTH ends of the range band.\n\n"
            "Then compile-and-test your own work: call run_test_gate. If it reports a "
            "COMPILE error or any FAIL, read the message, fix build/Combat.cpp (save again "
            "with write_combat_impl), and re-run run_test_gate. Repeat until it reports "
            "GATE PASS with all of T-COMBAT-01..08 passing. Do not finish until it passes.\n"
            "Common pitfall: the declarations live in `namespace strat` (see Combat.h) — "
            "define your functions inside `namespace strat { ... }` (or fully-qualify) and "
            "operate on `strat::Unit`, not a bare `Unit`."
        ),
        expected_output=(
            "Confirmation that build/Combat.cpp compiles and the gate reports 8/8 passing, "
            "plus a one-line note on how the counter rule honors the full [rangeMin, rangeMax] band."
        ),
        agent=systems_engineer,
    )

    gate = Task(
        description=(
            "Certify the build for release: call certify_build. It runs every invariant "
            "(T-COMBAT-01..08) and writes build/acceptance.json — the record the Balance "
            "Analyst requires before it will run. Certify only if all pass; if any fails, "
            "the record is marked not-accepted and the crew halts here. Report the verdict."
        ),
        expected_output=(
            "The certification verdict (accepted true/false), the test tally, and — if the "
            "Systems Engineer's self-repair left anything failing — which invariant blocked."
        ),
        agent=test_engineer,
        context=[implement],
    )

    balance = Task(
        description=(
            "Run run_self_play and interpret the duel table. It REQUIRES the Test Engineer's "
            "acceptance record (build/acceptance.json) and refuses without it, so this only "
            "works once the build is certified. Identify the strongest unit, any degenerate "
            "matchup, and whether the melee-range assumption understates a unit. Propose ONE "
            "concrete change for the Director (a stat tweak or a sim-methodology fix)."
        ),
        expected_output=(
            "A short balance report: win tally read-out, one balance risk, and one concrete "
            "tuning or methodology proposal."
        ),
        agent=balance_analyst,
        context=[gate],
    )

    return [implement, gate, balance]
