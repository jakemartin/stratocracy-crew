# Stratocracy Agent Crew — Assignment #3 (Build an Agent Crew)

A 3-agent **CrewAI** crew that authors, verifies, and balance-tests a real piece of
**Stratocracy** — the headless C++ **combat-resolution** module from the GDD (§2.6 / §4.1).
The agents are the three headless roles the GDD §3 pipeline already names, so the crew and
the design document stay in lockstep.

**Which game:** Stratocracy — UE5.8 hex turn-based strategy (lineage: *Conflict*, NES 1989).
**What the crew produces:** a compiling, test-passing `Combat.cpp`, a passing test suite,
and a self-play balance report — the exact "game-ready output" the GDD promises for this crew.

---

## The crew

| Agent | Role (GDD §3) | Tool (its "act") | Verifiable output |
|-------|---------------|------------------|-------------------|
| 🛠 **Systems Engineer** | Author the headless C++ rules; self-test until they compile and pass | `write_combat_impl` + `run_test_gate` (dev self-test) | `build/Combat.cpp` (compiling, self-tested) |
| ✅ **Test Engineer** | The sole release authority — certify the build and write the acceptance record | `certify_build` → **real compile + run**, then writes `build/acceptance.json` | The acceptance record (accepted only if all 17 pass: T-COMBAT-01..10 + T-REPAIR-01..07) |
| 📊 **Balance Analyst** | Self-play, then propose one tuning/methodology change | `run_self_play` → AI-vs-AI duels (**refuses without the acceptance record**) | `build/balance_report.md` + proposal |

The **Director** is the human — represented by the input spec (`spec/combat_spec.md`, extended by
`spec/combat_spec_addendum.md` for type-effectiveness + repair), not an agent. Handoff order is fixed and sequential: **spec → implement → certify → balance**, exactly the
GDD §3 workflow. Agents coordinate through a shared `build/` workspace (they are active file
participants, not chatbots) — the file-system-integration pattern from Class 4.

**No agent is removable — each owns a distinct artifact the next one needs.** The Systems
Engineer produces `Combat.cpp`; the Test Engineer is the *only* writer of `build/acceptance.json`
(its `certify_build` runs the invariants and signs the release); and the Balance Analyst's
`run_self_play` **refuses to run without an accepted record**. Remove the Systems Engineer and
there is no code; remove the Test Engineer and there is no acceptance, so balance halts; remove
the Balance Analyst and there is no report. The Systems Engineer's own `run_test_gate` is dev-time
self-testing — it keeps the build compiling through the sequential hand-off, but it never
certifies for release. That separation (author self-checks; an independent role signs off) is why
the pipeline has no redundant agent.

See `diagram.mmd` for the architecture (roles, tools, data flow, the fail-loop).

## Why the gate is real

`run_test_gate` doesn't ask the model whether its code is correct — it **compiles the C++ with
`g++` and runs the assertions** (now **17**: T-COMBAT-01..10 + T-REPAIR-01..07). Two invariants are
the headline hallucination-catchers, and the offline run trips **both** on pass 1:

- **T-COMBAT-07** — Artillery (range 2–3) must take **zero** counter from a range-1 attacker. The
  classic hallucination over-generalizes the counter rule to `distance <= rangeMax`, dropping the
  `rangeMin` check.
- **T-REPAIR-03** — a unit in enemy contact must **not** heal on an owned objective (the
  anti-fortress lock, GDD §2.7). The parallel hallucination drops the `!enemyAdjacent` clause.

The type-effectiveness hook ships **neutral** (`effectiveness()` returns 1.0 for every pair), and
**T-COMBAT-09/10** pin it there — so an agent that "helpfully" invents balance numbers is blocked,
and the pre-existing combat numbers stay byte-identical. The offline run demonstrates it end to end:
**pass 1 blocks on T-COMBAT-07 + T-REPAIR-03, pass 2 passes 17/17.**

## Run it

```bash
# Offline — no API key, no install; needs only a C++ compiler. Always runs.
python run.py --offline

# Live crew — CrewAI + Claude author the module for real:
pip install -r requirements.txt
cp .env.example .env          # add your ANTHROPIC_API_KEY
python run.py                 # uses the live crew when a key is present
```

`run.py` never crashes a submission: if the live crew errors (no key, network, etc.) it falls
back to the deterministic pipeline, so there is always a runnable, gate-verified result.

### Windows / MSVC

The compile+test gate auto-detects the compiler: `g++`, `clang++`, `c++`, **or MSVC `cl.exe`**
(GCC/Clang use `-std=c++17 -O2 -o`; MSVC uses `cl /nologo /std:c++17 /EHsc /O2 /Fe:`). `cl.exe`
is only on `PATH` inside a Visual Studio developer shell, so on Windows with the Unreal toolchain:

```bat
:: open the "x64 Native Tools Command Prompt for VS" (Start menu), then:
cd /d E:\MultiAgent\stratocracy-crew
python run.py --offline
```

A normal PowerShell/cmd won't find `cl.exe` — use the Native Tools prompt (or add MinGW/Clang to PATH).

**Requirements:** live path — Python 3.10–3.13, `crewai[anthropic]==1.14.6`, an `ANTHROPIC_API_KEY`.
Both paths — a C++17 compiler (`g++`, `clang++`, `c++`, or MSVC `cl.exe`). Default model is the GDD
§4.6 workhorse **Sonnet 5** (`anthropic/claude-sonnet-5`); override with `STRATOCRACY_CREW_MODEL`.

## Layout

```
spec/combat_spec.md      Director's input contract (inputs, formula, invariants)
spec/combat_spec_addendum.md  contract extension: type-effectiveness (eff) + repair
crew/agents.py           the 3 agents (role/goal/backstory + tools + Claude LLM)
crew/tasks.py            the 3 tasks, chained spec → implement → gate → balance
crew/crew.py             assembles the sequential Crew
crew/tools.py            deterministic tools: write / compile+test / self-play
crew/offline.py          no-API pipeline (also demos the gate catching the hallucination)
cpp_reference/           Combat.h, test_combat.cpp, selfplay.cpp, and good/buggy impls
run.py                   entrypoint (live if key, else offline; always produces artifacts)
build/                   generated: Combat.cpp, balance_report.md, run_log.md, binaries
diagram.mmd              Mermaid architecture diagram
```

## Outputs (in `build/` after a run)

- `Combat.cpp` — the authored, gate-passing implementation
- `run_log.md` — full spec → gate → balance transcript (incl. the caught hallucination)
- `balance_report.md` — self-play duel table + which invariant caught the bug

## Ties back to the rest of the project

Every real run yields a commit-able system plus its passing test IDs — the evidence the GDD §2
**provenance ledger** is waiting on. Point the ledger's *Combat resolution* row at the `Combat.cpp`
commit and the gate result (`T-COMBAT-01..10 + T-REPAIR-01..07`), and the attribution becomes
demonstrated fact — the same gate now also backs the ledger's pending *Repair* and
*Type-effectiveness* rows once a live run authors them.
