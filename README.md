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

## Week 1 — GDD §4.11 rows 1-3 (added after the Assignment-3 submission)

The Combat module above is one row of the GDD's §3 provenance ledger. **§4.4 week 1
owes three more** — §4.11 rows 1-3 — and they are built here, through the same
spec → author → gate → certify pipeline:

| Row | System | Spec | Acceptance |
|---|---|---|---|
| 1 | Hex grid & math | `spec/hex_spec.md` | T-HEX-01..07 |
| 2 | Data tables (units/terrain/effectiveness) | `spec/data_spec.md` | T-DATA-01..04, 06 |
| 3 | Movement & pathfinding | `spec/move_spec.md` | T-MOVE-01..06 |

Row 3 depends on rows 1 and 2 and the gate depends on them the same way: `test_move.cpp`
links `Hex.cpp` and `Data.cpp` and takes its move costs from `data/terrain.csv` and its
Move allowances from `data/units.csv`. The tables are the GDD §4.8 contract —
**authored once, read twice**: one canonical CSV that the headless loader parses and the
editor will import, with a missing column or unparseable value a hard load failure
rather than a silent default.

**The movement gate has the same teeth as the combat one.** Pass 1 computes the
reachable set as *"every hex within `hexDistance <= move`"* — the shortcut that looks
right on a screenshot and never consults terrain. It is blocked on **T-MOVE-01,
T-MOVE-02 and T-MOVE-03**, and pass 2 (Dijkstra over terrain cost, ties broken by
canonical hex order) passes 6/6. T-MOVE-01 is the one that matters: it compares the
module's set against an **independent** shortest-path pass written in the test, never
against the module's own search, because §2.5 promises *"the real move set, not an
estimate"* and a search compared to itself cannot test that.

Two IDs are deliberately absent, and `build/acceptance_week1.json` records both rather
than letting a green run imply full coverage:

- **T-DATA-05** — the in-editor Unreal Automation half of row 2 (DataTable import
  parity + the `EUnitType` mirror). §4.11 marks it **†**; nothing headless can assert
  it, and Q29 refuses a ledger flip on a partial acceptance set, so **row 2's flip
  waits on the editor pass** even though its headless half is green.
- **T-MOVE-07** — reserved and unwritten. Recon's *"ignores some terrain cost"* is
  blocked on the Q2 movement-class ruling, and no gate is written until the rule exists.

Verified on **clang++ and MSVC**, 19/19 both times — which is the content of T-HEX-07's
"fixed across runs and compilers", not a claim about it.

### The other half of week 1 — "Playable via debug commands"

§4.4's week-1 goal has two halves, and the row flips only closed one. The second is a
**debug-command REPL** over the built modules — `spec/driver_spec.md`,
`build/stratocracy_debug.exe`:

```
> fixture river
> place 0 Artillery 0 1
> place 1 Tank 2 1
> forecast 1 2
at distance 2: Artillery deals 5
  counter: none
```

**The driver contains no rules.** Reach, path and move delegate to `Move.h`; damage and
counter eligibility to `Combat.h`; every stat to `Data.h` over `data/*.csv`; distance and
adjacency to `Hex.h`. Where an answer would need §4.11 rows 4–8 — ownership, whose turn it
is, what a scenario file looks like — it **refuses the command instead of deciding it**.
That is the whole design: a debug tool that decides anything becomes a second rules
implementation, and then the gated modules are no longer what the game does.

`forecast` and `attack` call **one** computation, so §2.6's "the forecast the player sees
is exactly what resolves" is structural rather than merely tested — **GATE-DRV-03** then
asserts it anyway.

Its suite is **GATE-DRV-01..07**, named like `GATE-DATA-HARDFAIL` rather than `T-*`
because it gates a tool, not a rules system: it is not a §4.7 stub, it flips no §3 ledger
row, and it moves no count in the GDD. Every check compares the driver's output against a
direct module call rather than a hardcoded expectation.

**What it deliberately is not:** there are no turns, no capture, no Fame, no production,
no AI and no scenario file, because rows 4–8 hold no code. It is a debug tool, not a match.

## Run it

```bash
# Offline — no API key, no install; needs only a C++ compiler. Always runs.
python run.py --offline

# Just the week-1 rows (§4.11 rows 1-3) + the debug driver, skipping the combat crew:
python run.py --week1

# Then play it — the artifact §4.4 week 1 asks for:
cd build && ./stratocracy_debug ../data     # 'help', 'fixture list', 'quit'

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
spec/hex_spec.md         §4.11 row 1 contract — coords, neighbors, canonical order
spec/data_spec.md        §4.11 row 2 contract — the §4.8 schemas, hard-fail rule
spec/move_spec.md        §4.11 row 3 contract — Dijkstra, occupancy, the tie-break
spec/driver_spec.md      the debug-command driver — no rules of its own
data/                    the canonical CSVs (§4.8): units, terrain, effectiveness
crew/agents.py           the 3 agents (role/goal/backstory + tools + Claude LLM)
crew/tasks.py            the 3 tasks, chained spec → implement → gate → balance
crew/crew.py             assembles the sequential Crew
crew/tools.py            deterministic tools: write / compile+test / self-play / week-1 gate
crew/offline.py          no-API pipeline (also demos the gate catching the hallucination)
cpp_reference/           the FIXED sources — headers + test harnesses — and good/buggy impls
run.py                   entrypoint (live if key, else offline; always produces artifacts)
build/                   generated: the authored .cpp files, acceptance records, binaries
diagram.mmd              Mermaid architecture diagram
```

**`build/` is gitignored**, so every implementation's committed home is
`cpp_reference/*.good.cpp` and the harness that gates it is `cpp_reference/test_*.cpp`.
The pipeline copies the fixed sources in and authors the implementation out on each run;
nothing under `build/` is evidence of anything on its own. Anything citing this repo as
provenance should cite the `cpp_reference/` path, which resolves in the tree.

## Outputs (in `build/` after a run)

- `Combat.cpp` — the authored, gate-passing implementation
- `Hex.cpp`, `Data.cpp`, `Move.cpp` — the week-1 modules (§4.11 rows 1-3)
- `Driver.cpp` + `stratocracy_debug` — the debug-command REPL, week 1's playable artifact
- `acceptance.json` — the Combat release record (Test Engineer only)
- `acceptance_week1.json` — the rows 1-3 release record, including what it does **not**
  cover (T-DATA-05, T-MOVE-07) so a green run cannot imply full coverage
- `run_log.md` — full spec → gate → balance transcript (incl. both caught hallucinations)
- `balance_report.md` — self-play duel table + which invariant caught the bug

## Ties back to the rest of the project

Every real run yields a commit-able system plus its passing test IDs — the evidence the GDD §3
**provenance ledger** is waiting on. Point the ledger's *Combat resolution* row at the `Combat.cpp`
commit and the gate result (`T-COMBAT-01..10 + T-REPAIR-01..07`), and the attribution becomes
demonstrated fact — the same gate now also backs the ledger's pending *Repair* and
*Type-effectiveness* rows once a live run authors them.

The week-1 rows extend that chain to **§4.4's first milestone**. *Hex grid & math* and
*Movement & pathfinding* are complete at this commit — headless, no in-editor half, so
their acceptance sets close entirely here. *Data tables* is **not**: T-DATA-05 lives in
the editor, and Q29 requires the full acceptance set at one commit before a row flips.
Reporting rows 1 and 3 as built and row 2 as still pending is the honest read of the
ledger's own rules, and it is what `acceptance_week1.json` records.
