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

# Which repair architecture the Systems Engineer uses. Two real configurations from this
# repo's own history, kept switchable so the cost difference between them is measurable
# on demand rather than asserted (Assignment #10, D3 before/after).
#
#   legacy       -- 30d0fe3. The SE authors only. It holds write_combat_impl and nothing
#                   else, so a failing invariant has to cross an agent boundary: the Test
#                   Engineer runs the gate, narrates the failure, and hands it back. Every
#                   hop re-establishes context in a different agent.
#   self-verify  -- b21504e (current). The SE also holds run_test_gate and closes the loop
#                   itself, iterating until GATE PASS. The Test Engineer still certifies
#                   independently, so the release authority is unchanged -- what moved is
#                   only WHERE the repair iterations happen.
#
# The spec, the gate, the invariant count and the model are identical either way. That is
# the point: it isolates the architecture as the single variable. Running the literal
# 30d0fe3 commit instead would also swap an 8-invariant spec for a 17-invariant one and
# confound the scope change with the architecture change.
ARCHITECTURE = os.environ.get("STRATOCRACY_CREW_ARCH", "self-verify").strip().lower()
_VALID_ARCH = ("self-verify", "legacy")
if ARCHITECTURE not in _VALID_ARCH:
    raise ValueError(
        f"STRATOCRACY_CREW_ARCH={ARCHITECTURE!r} is not one of {_VALID_ARCH}. "
        "A typo must not silently fall back to the default -- that would label a "
        "self-verify run as legacy in the cost comparison."
    )


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
    """Build the three roles.

    ONE LLM INSTANCE PER AGENT when `llm` is not supplied. CrewAI accumulates token
    usage on the LLM object (`BaseLLM._token_usage`, read back by
    `get_token_usage_summary()`), not on the agent -- so a single LLM shared by three
    agents makes per-agent cost unattributable, and it makes CrewAI's own
    `Crew.calculate_usage_metrics()` add THE SAME running total once per agent, which
    reports roughly 3x the real spend. Separate instances fix both.
    Measured 2026-08-31: a shared instance reported 0 tokens against every agent's
    `_token_process` while the run really did call the API, which is what surfaced this.

    Passing an explicit `llm` keeps the old shared-instance behaviour for any caller
    that wants it, and accepts the attribution loss that comes with it.
    """
    shared = llm is not None
    llm = llm or build_llm()

    if ARCHITECTURE == "self-verify":
        se_goal = (
            "Author the headless C++ combat module for Stratocracy strictly from the "
            "Director's spec and write it with write_combat_impl. Then VERIFY YOUR OWN "
            "WORK: call run_test_gate to compile and run the tests; if it reports a compile "
            "error or any failing invariant, read the message, fix build/Combat.cpp, and "
            "re-run the gate. Iterate until it reports GATE PASS (all of T-COMBAT-01..10, T-REPAIR-01..07). "
            "Only finish once the gate passes. Implement ONLY what the spec defines."
        )
        se_backstory = (
            "A disciplined engine programmer who treats the spec as a contract and never "
            "hands off code that doesn't compile. Writes deterministic, dependency-free "
            "C++17 (no Unreal, no third-party libs) and leans on the real compiler + tests "
            "as the feedback loop, not on eyeballing."
        )
        se_tools = [write_combat_impl, run_test_gate]
    else:  # legacy -- 30d0fe3's cross-agent repair loop, verbatim
        se_goal = (
            "Author the headless C++ combat module for Stratocracy strictly from the "
            "Director's spec, then write it to build/Combat.cpp with the write_combat_impl "
            "tool. If the Test Engineer reports a failing invariant, correct the code and "
            "re-write it. Implement ONLY what the spec defines — no invented rules."
        )
        se_backstory = (
            "A disciplined engine programmer who treats the spec as a contract. Writes "
            "deterministic, dependency-free C++17 (no Unreal, no third-party libs) so the "
            "rules can be tested in seconds without launching the editor."
        )
        se_tools = [write_combat_impl]

    systems_engineer = Agent(
        role="Systems Engineer",
        goal=se_goal,
        backstory=se_backstory,
        tools=se_tools,
        llm=llm if shared else build_llm(),
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
        # Legacy also hands the gate itself to the Test Engineer (30d0fe3), because in that
        # architecture the SE cannot run it -- a failing invariant only becomes visible
        # here, and the repair narration crosses the task boundary carrying the SE's whole
        # output as `context`. certify_build stays in BOTH configurations: it writes
        # build/acceptance.json, which the Balance Analyst and the week-1 stage require, so
        # dropping it in legacy would change what the pipeline produces and not just what
        # it costs.
        tools=[certify_build] if ARCHITECTURE == "self-verify" else [run_test_gate, certify_build],
        llm=llm if shared else build_llm(),
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
        llm=llm if shared else build_llm(),
        allow_delegation=False,
        verbose=True,
    )

    return systems_engineer, test_engineer, balance_analyst
