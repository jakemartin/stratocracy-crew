"""Measured token usage and dollar cost for a live crew run (Assignment #10, D3).

Assignment #10 asks for a cost figure "calculated from the actual content generation
run, not a hypothesis". This module is the measurement: it reads what CrewAI already
accumulated during the run and prices it. Nothing here estimates, samples, or
extrapolates -- if a number is not measured, it is reported as unmeasured.

WHERE THE NUMBERS COME FROM. Every CrewAI agent owns a `TokenProcess`
(`crewai/agents/agent_builder/utilities/base_token_process.py`) that a
`TokenCalcHandler` callback feeds on every LLM response. It accumulates live, so it
can be read at any point, not only after `kickoff()` returns. `Crew.usage_metrics`
is the sum of those per-agent processes.

WHY PER-AGENT IS PER-STEP HERE. `crew/tasks.py` builds exactly three tasks and
`crew/crew.py` runs them `Process.sequential`, one per agent -- Systems Engineer,
Test Engineer, Balance Analyst. The mapping is 1:1, so per-agent totals ARE the
per-step breakdown the rubric's "most expensive pipeline step" asks for. If a fourth
task is ever added to an existing agent, that stops being true and this docstring is
the thing that is now wrong: switch to task-boundary deltas at that point.

TWO MEASUREMENT LIMITS, STATED RATHER THAN PAPERED OVER:

1. `TokenProcess` sums `prompt_tokens`, `cached_prompt_tokens`, `completion_tokens`
   and `successful_requests`. It never sums `cache_creation_tokens`, even though
   `UsageMetrics` declares the field. Cache WRITES are therefore not observable from
   here and are reported as unmeasured -- not as zero dollars. A run that writes a
   large cache costs more than this file can see, so the total is a floor.
2. `cached_prompt_tokens` is a SUBSET of `prompt_tokens` (LiteLLM reports the cached
   count inside the prompt total), so uncached input is the difference. Pricing them
   as two separate buckets would double-bill the cached half.
"""
from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path

# USD per 1M tokens. Anthropic first-party API rates.
#
# Cache multipliers follow the documented model: a 5-minute cache READ bills at ~0.1x
# the input rate, a cache WRITE at ~1.25x. They are derived here rather than typed as
# literals so a base-rate correction cannot leave the cache rates stale behind it.
_BASE = {
    "claude-sonnet-5": (2.00, 10.00),
    "claude-opus-5": (5.00, 25.00),
    "claude-haiku-4-5": (1.00, 5.00),
    "claude-sonnet-4-6": (3.00, 15.00),
}
PRICING = {
    name: {
        "input": inp,
        "output": out,
        "cache_read": round(inp * 0.10, 4),
        "cache_write": round(inp * 1.25, 4),
    }
    for name, (inp, out) in _BASE.items()
}

PER_MILLION = 1_000_000


def normalise_model(model: str) -> str:
    """`anthropic/claude-sonnet-5` -> `claude-sonnet-5`.

    CrewAI addresses models through LiteLLM, which prefixes the provider. The pricing
    table is keyed on the bare Anthropic model id.
    """
    return (model or "").split("/")[-1].strip()


def rates_for(model: str) -> dict | None:
    """Rates for `model`, or None if it is not in the table.

    None is deliberate: an unpriced model must make the report say so, not silently
    fall back to another model's rates and produce a confident wrong dollar figure.
    """
    return PRICING.get(normalise_model(model))


def cost_breakdown(usage: dict, model: str) -> dict:
    """Price one agent's measured usage. Returns token buckets and dollars.

    `usage` is a `UsageMetrics.model_dump()` (or any dict with the same keys).
    """
    prompt = int(usage.get("prompt_tokens") or 0)
    cached = int(usage.get("cached_prompt_tokens") or 0)
    completion = int(usage.get("completion_tokens") or 0)
    cache_write = int(usage.get("cache_creation_tokens") or 0)

    # DISAMBIGUATING cached_prompt_tokens. Anthropic reports three DISJOINT input
    # buckets: input_tokens (neither cached nor written), cache_creation_input_tokens
    # (written), cache_read_input_tokens (read). Measured on the 2026-08-31 legacy run,
    # CrewAI/LiteLLM filled `cached_prompt_tokens` with the cache-CREATION figure --
    # identical to `cache_creation_tokens` in all three steps, and larger than
    # `prompt_tokens`, which a genuine read cannot be. Trusting it as a read did two
    # things wrong at once: it billed real uncached input at the 0.1x read rate, and it
    # invented a cache-read column for a run that reused nothing.
    #
    # So a `cached` that merely mirrors `cache_write` is treated as no read at all. When
    # the two differ, `cached` is a real read and is a subset of `prompt` (clamped, since
    # a read larger than the prompt is not a number this can price).
    if cache_write and cached == cache_write:
        cached = 0
    cached = min(cached, prompt)
    uncached = prompt - cached

    row = {
        "uncached_input_tokens": uncached,
        "cache_read_tokens": cached,
        "cache_write_tokens": cache_write,
        "output_tokens": completion,
        "successful_requests": int(usage.get("successful_requests") or 0),
    }

    rates = rates_for(model)
    if rates is None:
        row["priced"] = False
        row["model_not_in_pricing_table"] = normalise_model(model)
        row["cost_usd"] = None
        return row

    input_cost = uncached / PER_MILLION * rates["input"]
    cache_read_cost = cached / PER_MILLION * rates["cache_read"]
    cache_write_cost = cache_write / PER_MILLION * rates["cache_write"]
    output_cost = completion / PER_MILLION * rates["output"]

    row["priced"] = True
    row["cost_usd"] = {
        "uncached_input": round(input_cost, 6),
        "cache_read": round(cache_read_cost, 6),
        "cache_write": round(cache_write_cost, 6),
        "output": round(output_cost, 6),
        "total": round(
            input_cost + cache_read_cost + cache_write_cost + output_cost, 6
        ),
    }
    return row


def _usage_for(agent) -> tuple[dict | None, str]:
    """One agent's accumulated usage, and which object it came from.

    CrewAI keeps the running total on the LLM (`BaseLLM._token_usage`) whenever the
    agent's llm is a `BaseLLM` -- which `crewai.LLM` is -- and only falls back to the
    agent's `TokenProcess` otherwise. `crewai/agent/core.py` picks between them the same
    way. Reading `_token_process` alone returns zeros for a run that really did call the
    API (measured 2026-08-31), so the LLM is tried first.

    The two sources are not equivalent: `TokenProcess` never sums `cache_creation_tokens`,
    so on the fallback path cache-WRITE spend is invisible and the total is a floor. The
    source is recorded per step so the report can say which one it got.
    """
    llm = getattr(agent, "llm", None)
    getter = getattr(llm, "get_token_usage_summary", None)
    if callable(getter):
        try:
            return getter().model_dump(), "llm"
        except Exception:  # noqa: BLE001 -- fall through to the agent-side counter
            pass

    proc = getattr(agent, "_token_process", None)
    if proc is not None:
        return proc.get_summary().model_dump(), "token_process"

    return None, "none"


def collect(crew, model: str, label: str) -> dict:
    """Read every agent's accumulated usage off `crew` and price it.

    Safe to call after `kickoff()` returns. Agents are identified by `role`, which is
    what `crew/agents.py` sets and what a reader of the report will recognise.
    """
    steps = []
    seen_llms: set[int] = set()
    for agent in getattr(crew, "agents", []) or []:
        usage, source = _usage_for(agent)
        if usage is None:
            continue

        # Guard the shared-instance trap. If two agents hand back the SAME LLM object,
        # its running total covers both and adding it twice inflates the bill -- which is
        # exactly the bug in CrewAI's own calculate_usage_metrics() for a shared llm.
        # crew/agents.py gives each agent its own instance, so this should never fire;
        # it is here so that if it ever does, the report says so instead of over-reporting.
        llm_id = id(getattr(agent, "llm", None))
        duplicate = source == "llm" and llm_id in seen_llms
        if source == "llm":
            seen_llms.add(llm_id)

        row = cost_breakdown(usage, model)
        row["step"] = getattr(agent, "role", "(unnamed agent)")
        row["usage_source"] = source
        row["raw_usage"] = usage
        if duplicate:
            row["shared_llm_double_count"] = True
        steps.append(row)

    priced = [s for s in steps if s.get("priced")]
    total_usd = round(sum(s["cost_usd"]["total"] for s in priced), 6) if priced else None

    totals = {
        "uncached_input_tokens": sum(s["uncached_input_tokens"] for s in steps),
        "cache_read_tokens": sum(s["cache_read_tokens"] for s in steps),
        "cache_write_tokens": sum(s["cache_write_tokens"] for s in steps),
        "output_tokens": sum(s["output_tokens"] for s in steps),
        "successful_requests": sum(s["successful_requests"] for s in steps),
        "cost_usd": total_usd,
        "all_steps_priced": len(priced) == len(steps) and bool(steps),
    }

    # Rank by measured dollars so "most expensive step" is read off the run, not guessed.
    ranked = sorted(priced, key=lambda s: s["cost_usd"]["total"], reverse=True)

    return {
        "label": label,
        "model": normalise_model(model),
        "measured_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "rates_usd_per_million": rates_for(model),
        "steps": steps,
        "totals": totals,
        "most_expensive_step": ranked[0]["step"] if ranked else None,
        "usage_sources": sorted({s["usage_source"] for s in steps}),
        "unmeasured": _caveats(steps),
    }


def _caveats(steps: list[dict]) -> list[str]:
    """Only the limits that actually apply to THIS report.

    A standing disclaimer that does not apply is worse than none: it teaches the reader
    to skip the section that matters when it does apply.
    """
    notes = []
    if any(s["usage_source"] == "token_process" for s in steps):
        notes.append(
            "At least one step's usage came from CrewAI's TokenProcess, which never sums "
            "cache_creation (write) tokens -- that step's cache-write spend is outside "
            "this total, so the total is a floor for it."
        )
    if any(s.get("shared_llm_double_count") for s in steps):
        notes.append(
            "Two or more agents share one LLM instance, so its running total is counted "
            "against each of them. Per-step figures are NOT independent and the total is "
            "inflated. Give each agent its own LLM (crew/agents.py) to fix this."
        )
    if not any(s.get("priced") for s in steps):
        notes.append("No step was priced, so no dollar figure in this report is usable.")
    return notes


def _fmt_usd(v) -> str:
    return "unpriced" if v is None else f"${v:.6f}"


def render_markdown(report: dict) -> str:
    """The report a grader reads, from the same dict the JSON is written from."""
    t = report["totals"]
    rates = report["rates_usd_per_million"]

    out = [
        f"# Crew run cost — {report['label']}",
        "",
        f"- **Model:** `{report['model']}`",
        f"- **Measured at:** {report['measured_at']}",
        f"- **Total measured cost:** **{_fmt_usd(t['cost_usd'])}**",
        f"- **Most expensive step:** {report['most_expensive_step'] or '(none priced)'}",
        f"- **API requests:** {t['successful_requests']}",
        "",
    ]

    if rates:
        out += [
            "Rates (USD per 1M tokens): "
            f"input ${rates['input']:.2f} · output ${rates['output']:.2f} · "
            f"cache read ${rates['cache_read']:.2f} · cache write ${rates['cache_write']:.2f}",
            "",
        ]
    else:
        out += [
            "> No pricing row for this model, so token counts below are measured but "
            "unpriced.",
            "",
        ]

    out += [
        "| Step | Uncached in | Cache read | Cache write | Output | Requests | Cost |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for s in report["steps"]:
        cost = _fmt_usd(s["cost_usd"]["total"] if s.get("priced") else None)
        out.append(
            f"| {s['step']} | {s['uncached_input_tokens']:,} | {s['cache_read_tokens']:,} "
            f"| {s['cache_write_tokens']:,} | {s['output_tokens']:,} "
            f"| {s['successful_requests']} | {cost} |"
        )
    out.append(
        f"| **Total** | **{t['uncached_input_tokens']:,}** | **{t['cache_read_tokens']:,}** "
        f"| **{t['cache_write_tokens']:,}** | **{t['output_tokens']:,}** "
        f"| **{t['successful_requests']}** | **{_fmt_usd(t['cost_usd'])}** |"
    )

    if report["unmeasured"]:
        out += ["", "## Not measured", ""]
        out += [f"- {n}" for n in report["unmeasured"]]
    out.append("")
    return "\n".join(out)


def write_report(report: dict, out_dir: Path, stem: str = "cost_report") -> tuple[Path, Path]:
    """Write `<stem>.json` and `<stem>.md` into `out_dir`. Returns both paths."""
    out_dir.mkdir(parents=True, exist_ok=True)
    json_path = out_dir / f"{stem}.json"
    md_path = out_dir / f"{stem}.md"
    json_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    md_path.write_text(render_markdown(report), encoding="utf-8")
    return json_path, md_path
