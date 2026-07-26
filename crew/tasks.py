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
            "counter check must honor BOTH ends of the range band."
        ),
        expected_output=(
            "A confirmation that build/Combat.cpp was written, plus a one-line note on how "
            "the counter rule honors the full [rangeMin, rangeMax] band."
        ),
        agent=systems_engineer,
    )

    gate = Task(
        description=(
            "Run run_test_gate. If it BLOCKS, report exactly which invariants failed and "
            "instruct the Systems Engineer to fix them, then run the gate again after the "
            "fix. Do not pass the build until T-COMBAT-01..08 all hold."
        ),
        expected_output=(
            "The final gate verdict (PASS/BLOCK), the test tally, and — if any fix was "
            "needed — a note on which invariant caught the bug."
        ),
        agent=test_engineer,
        context=[implement],
    )

    balance = Task(
        description=(
            "Only after the gate passes, run run_self_play and interpret the duel table. "
            "Identify the strongest unit, any degenerate matchup, and whether the melee-"
            "range assumption understates a unit. Propose ONE concrete change for the "
            "Director (a stat tweak or a sim-methodology fix)."
        ),
        expected_output=(
            "A short balance report: win tally read-out, one balance risk, and one concrete "
            "tuning or methodology proposal."
        ),
        agent=balance_analyst,
        context=[gate],
    )

    return [implement, gate, balance]
