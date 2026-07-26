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

from .tools import write_combat_impl, run_test_gate, run_self_play

SPEC_PATH = Path(__file__).resolve().parent.parent / "spec" / "combat_spec.md"

# Workhorse model per GDD §4.6 (Sonnet 5). Override with STRATOCRACY_CREW_MODEL.
DEFAULT_MODEL = os.environ.get("STRATOCRACY_CREW_MODEL", "anthropic/claude-sonnet-5")


def build_llm() -> LLM:
    # max_tokens is required for Anthropic models in CrewAI.
    return LLM(model=DEFAULT_MODEL, max_tokens=4096, temperature=0.2)


def read_spec() -> str:
    return SPEC_PATH.read_text(encoding="utf-8")


def build_agents(llm: LLM | None = None):
    llm = llm or build_llm()

    systems_engineer = Agent(
        role="Systems Engineer",
        goal=(
            "Author the headless C++ combat module for Stratocracy strictly from the "
            "Director's spec, then write it to build/Combat.cpp with the write_combat_impl "
            "tool. If the Test Engineer reports a failing invariant, correct the code and "
            "re-write it. Implement ONLY what the spec defines — no invented rules."
        ),
        backstory=(
            "A disciplined engine programmer who treats the spec as a contract. Writes "
            "deterministic, dependency-free C++17 (no Unreal, no third-party libs) so the "
            "rules can be tested in seconds without launching the editor."
        ),
        tools=[write_combat_impl],
        llm=llm,
        allow_delegation=False,
        verbose=True,
    )

    test_engineer = Agent(
        role="Test Engineer",
        goal=(
            "Guard correctness. Run the compile+test gate with run_test_gate. If any "
            "invariant fails, state exactly which one and hand the failure back so the "
            "Systems Engineer can fix it. Only report PASS when every invariant holds."
        ),
        backstory=(
            "Owns the merge gate. Trusts the compiler and the assertions, not prose. Knows "
            "the classic hallucination — over-generalizing the counterattack rule and "
            "letting Artillery counter at range 1 — and relies on T-COMBAT-07 to catch it."
        ),
        tools=[run_test_gate],
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
