# Mid-project cost reduction — measured before/after

Assignment #10, Deliverable 3. Both figures come from live runs of this crew against the
Anthropic API on `claude-sonnet-5`, priced by `crew/usage.py` from the token counts CrewAI
recorded during each run. Neither number is estimated.

## What changed

The Systems Engineer's repair loop moved **inside the agent**.

- **Before** (`30d0fe3`): the SE held only `write_combat_impl`. It authored the module and
  stopped. A failing invariant became visible only when the Test Engineer ran the gate in
  the *next* task, narrated the failure, and handed it back across the agent boundary —
  carrying the SE's entire prior output along as task `context`.
- **After** (`b21504e`, current): the SE also holds `run_test_gate` and closes the loop
  itself, re-running the gate until it reports GATE PASS. The Test Engineer still
  certifies independently via `certify_build`, so release authority is unchanged. Only the
  *location* of the repair iterations moved.

Reproduce either one:

```bash
STRATOCRACY_CREW_ARCH=legacy      ./run_with_msvc.bat --online
STRATOCRACY_CREW_ARCH=self-verify ./run_with_msvc.bat --online
```

## Measured result

| | Before (legacy) | After (self-verify) |
|---|---|---|
| Total cost | **$0.098239** | **$0.067557** |
| API requests | 6 | 7 |
| Gate outcome | GATE PASS 17/17, accepted=True | GATE PASS 17/17, accepted=True |

**Cost change: $-0.030682 (-31.2%).**

| Token bucket | Before | After | Delta | Change |
|---|---:|---:|---:|---:|
| Uncached input | 2,064 | 859 | -1,205 | -58.4% |
| Cache read | 0 | 1,694 | +1,694 | n/a |
| Cache write | 10,801 | 9,464 | -1,337 | -12.4% |
| Output | 6,711 | 4,184 | -2,527 | -37.7% |
| **All tokens** | **19,576** | **16,201** | **-3,375** | **-17.2%** |

## Why it got cheaper

Output tokens carry the saving: 6,711 -> 4,184
(-37.7%), and output bills at 5x input on this
model. In the before configuration the repair conversation is *prose crossing an agent
boundary* — the Test Engineer writes out which invariant failed and why, and the Systems
Engineer writes out its correction in reply. In the after configuration the same repair is
a tool call and a tool result inside one agent, which nothing has to narrate.

The self-verifying SE is also the only step in either run that gets a real cache **read**
(1,694 tokens at 0.1x input): it issues 3
requests against a prompt prefix that stays put, so iterations after the first reuse it.
The before configuration's SE makes one pass and has nothing to reuse.

Note the request count went *up* (6 -> 7)
while cost went down. Requests are the wrong unit for this pipeline; tokens, and especially
output tokens, are the one that moves the bill.

## Scope of the claim

Same spec (17 invariants), same gate, same model, same machine, one run each. `STRATOCRACY_CREW_ARCH`
switches only the SE's tool set and goal text, so the architecture is the single variable —
running the literal `30d0fe3` commit instead would have swapped an 8-invariant spec for a
17-invariant one and confounded scope with architecture. One run per configuration is not a
distribution: LLM output length varies between runs, so treat the direction and the
mechanism as the finding and the exact percentage as one sample.

Per-step breakdowns: `cost_report_before_legacy.md` / `.json`,
`cost_report_after_self_verify.md` / `.json`.
