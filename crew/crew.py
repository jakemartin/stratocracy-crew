"""Assemble the sequential crew (GDD §3 headless roles)."""
from __future__ import annotations

from crewai import Crew, Process

from .agents import build_agents
from .tasks import build_tasks


def build_crew(verbose: bool = True) -> Crew:
    # No shared LLM: build_agents() gives each role its own instance so token usage is
    # attributable per role. CrewAI accumulates usage on the LLM object, so one shared
    # instance would make every agent report the crew's whole running total -- see the
    # build_agents docstring.
    se, te, ba = build_agents()
    tasks = build_tasks(se, te, ba)
    return Crew(
        agents=[se, te, ba],
        tasks=tasks,
        process=Process.sequential,   # spec → implement → gate → balance
        verbose=verbose,
    )
