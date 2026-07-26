"""Assemble the sequential crew (GDD §3 headless roles)."""
from __future__ import annotations

from crewai import Crew, Process

from .agents import build_agents, build_llm
from .tasks import build_tasks


def build_crew(verbose: bool = True) -> Crew:
    llm = build_llm()
    se, te, ba = build_agents(llm)
    tasks = build_tasks(se, te, ba)
    return Crew(
        agents=[se, te, ba],
        tasks=tasks,
        process=Process.sequential,   # spec → implement → gate → balance
        verbose=verbose,
    )
