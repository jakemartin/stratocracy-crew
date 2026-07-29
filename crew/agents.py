"""The three headless roles from Stratocracy GDD §3, as CrewAI agents.

Systems Engineer  → authors the C++ combat rules from the Director's spec
Test Engineer     → runs the compile+test gate; blocks on any failing invariant
Balance Analyst   → runs self-play and proposes tuning

The Director is the human (you) — represented here by the input spec, not an agent.
"""
from __future__ import annotations

import os
from pathlib import Path

from crewai import Agent, LLM

from .tools import write_combat_impl, run_test_gate, certify_build, run_self_play

SPEC_PATH = Path(__file__).resolve().parent.parent / "spec" / "combat_spec.md"
ADDENDUM_PATH = Path(__file__).resolve().parent.parent / "spec" / "combat_spec_addendum.md"

# Workhorse model per GDD §4.6 (Sonnet 5). Override with STRATOCRACY_CREW_MODEL.
DEFAULT_MODEL = os.environ.get("STRATOCRACY_CREW_MODEL", "anthropic/claude-sonnet-5")


def build_llm() -> LLM:
    # max_tokens is required for Anthropic models in CrewAI. Sonnet 5 (and the
    # 4.6+ generation) deprecate `temperature`, so it is intentionally omitted.
    return LLM(model=DEFAULT_MODEL, max_tokens=4096)


def read_spec() -> str:
    """The Director's contract handed to the Systems Engineer: the base combat spec
    plus the addendum (type-effectiveness + repair), if present."""
    spec = SPEC_PATH.read_text(encoding="utf-8")
    if ADDENDUM_PATH.exists():
        spec += "\n\n---\n\n" + ADDENDUM_PATH.read_text(encoding="utf-8")
    return spec


def build_agents(llm: LLM | None = None):
    llm = llm or build_llm()

    systems_engineer = Agent(
        role="Systems Engineer",
        goal=(
            "Author the headless C++ combat module for Stratocracy strictly from the "
            "Director's spec and write it with write_combat_impl. Then VERIFY YOUR OWN "
            "WORK: call run_test_gate to compile and run the tests; if it reports a compile "
            "error or any failing invariant, read the message, fix build/Combat.cpp, and "
            "re-run the gate. Iterate until it reports GATE PASS (all of T-COMBAT-01..10, T-REPAIR-01..07). "
            "Only finish once the gate passes. Implement ONLY what the spec defines."
        ),
        backstory=(
            "A disciplined engine programmer who treats the spec as a contract and never "
            "hands off code that doesn't compile. Writes deterministic, dependency-free "
            "C++17 (no Unreal, no third-party libs) and leans on the real compiler + tests "
            "as the feedback loop, not on eyeballing."
        ),
        tools=[write_combat_impl, run_test_gate],
        llm=llm,
        allow_delegation=False,
        max_iter=12,
        verbose=True,
    )

    test_engineer = Agent(
        role="Test Engineer",
        goal=(
            "Own the release gate. Call certify_build to compile and run every invariant "
            "AND write the acceptance record (build/acceptance.json) that the Balance Analyst "
            "requires. Certify the build only when all of T-COMBAT-01..10, T-REPAIR-01..07 pass; if any fails, "
            "the record is marked not-accepted and the pipeline halts here — nothing runs "
            "balance on an uncertified build."
        ),
        backstory=(
            "The sole release authority. The Systems Engineer may self-test while authoring, "
            "but nothing ships downstream without this independent certification. Trusts the "
            "compiler and the assertions, not prose; knows the classic hallucination (letting "
            "Artillery counter at range 1) and relies on T-COMBAT-07 to catch it."
        ),
        tools=[certify_build],
        llm=llm,
        allow_delegation=False,
        verbose=True,
    )

    balance_analyst = Agent(
        role="Balance Analyst",
        goal=(
            "Once the gate passes, run self-play with run_self_play and interpret the "
            "results: which unit dominates, whether any matchup is degenerate, and one "
            "concrete tuning or methodology change to feed back to the Director."
        ),
        backstory=(
            "Reads AI-vs-AI duel tables for balance smells. Cares that the sim's "
            "assumptions (e.g. forcing every duel to melee range) don't misrepresent a "
            "unit such as Artillery, whose whole point is standoff."
        ),
        tools=[run_self_play],
        llm=llm,
        allow_delegation=False,
        verbose=True,
    )

    return systems_engineer, test_engineer, balance_analyst
