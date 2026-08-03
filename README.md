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

## §4.11 rows 1-6 (added after the Assignment-3 submission)

The Combat module above is one row of the GDD's §3 provenance ledger. **§4.4 week 1
owes three more** — §4.11 rows 1-3 — and they are built here, through the same
spec → author → gate → certify pipeline. **Rows 4, 5 and 6 run ahead of §4.4's
milestone table**, delivered early because §4.11's critical path runs
`1 → 3 → 4 → 5 → 6/8` and each was in turn the only unblocked link on it:

| Row | System | Spec | Acceptance | §4.4 week |
|---|---|---|---|---|
| 1 | Hex grid & math | `spec/hex_spec.md` | T-HEX-01..07 | 1 |
| 2 | Data tables (units/terrain/effectiveness) | `spec/data_spec.md` | T-DATA-01..04, 06 | 1 |
| 3 | Movement & pathfinding | `spec/move_spec.md` | T-MOVE-01..06 | 1 |
| 4 | Capture & Fame economy | `spec/economy_spec.md` | T-FAME-01..09 | **3** |
| 5 | Turn loop & win/tiebreak | `spec/turn_spec.md` | T-TURN-01..09 | **3** |
| 6 | Opponent AI (baseline) | `spec/ai_spec.md` | T-AI-01..06 + smoke | **3** |

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

### Row 4 — Capture & Fame economy (week 3's row, built early)

A link on §4.11's critical path `1 → 3 → 4 → 5 → 6/8`. §4.4 schedules it in **week 3**,
not week 1 — it is ahead of the milestone table, not part of week 1's debt.
`spec/economy_spec.md`, **T-FAME-01..09, 9/9** under both compilers.

**Four of its nine invariants encode a *ruled* question**, and the gate asserts the
ruling rather than the intuition it overturned:

| Ruling | What the gate refuses |
|---|---|
| **Q8** | income on turn 1 — turn-1 buying power is starting Fame **alone** |
| **Q8** | refunding a queued build — Fame commits at queue time, not spawn time |
| **Q4** | capture progress transferring between units, or surviving an interruption |
| **Q5** | a flag kill stacking with the ordinary award — a flag Tank pays 500, not 650 |
| **Q6** | an undamaged-strike bonus — cut, not priced, so its **absence** is asserted |

The pass-1 hallucination is the one an author reaches *without* those rulings in
front of them: income accrues on turn 1 (every strategy game pays you on turn 1),
and passive income also credits `fameCombat` (Fame is one pool, so surely every
source touches every counter). It is blocked on **T-FAME-01, T-FAME-02 and
T-FAME-08** — the third because crediting income to the combat counter corrupts the
tally as well. The second bug matters more than it looks: `fameCombat` is §2.8's
tiebreak sort key, so paying it passively lets a side that never fought win
criterion 1 and makes the mutual-passivity guard unreachable.

Row 4 owns the economy, **not the turn**. It never advances a turn and never decides
whose turn it is — the turn number arrives as an argument, which is how T-FAME-02's
no-accrual-on-turn-1 gets asserted without owning a counter. That is what let row 4
land before row 5.

### Row 5 — Turn loop & win/tiebreak (the row that finally owns the turn)

`spec/turn_spec.md`, **T-TURN-01..09, 9/9** under both compilers. Rows 3 and 4
*declined* the turn, so every question they deferred — whose turn it is, which units
may still act, when a turn starts, when the match is over — is concentrated here.

**Four of its nine invariants are §2.8's resolution procedure**, which is one guard,
one three-key comparison, and one grade:

| ID | What it pins |
|---|---|
| **T-TURN-04** | the exact key order — combat Fame → objectives held → surviving HP → draw |
| **T-TURN-05** | the mutual-passivity guard: both sides at zero combat Fame is an immediate draw, with **no** fall-through |
| **T-TURN-06** | criterion 2 is reached only when both sides fought and tied — asserted over a sweep, not one fixture |
| **T-TURN-07** | tiers are **categorical**: Decisive > Marginal > Draw, whatever the tallies |

The pass-1 hallucination is the reading an author reaches *without* §2.8 in front of
them: the tiebreak is a plain lexicographic comparison, so both sides on zero simply
ties at key 1 and falls through to objectives held — and the tier grades by how big
the winning margin was, the way nearly every strategy game reports a win. Both are
the exploits §1.5 closed: the first restores the turtle win, the second lets a capped
grind's tally outrank a flag kill. It is blocked on **T-TURN-05, T-TURN-06 and
T-TURN-07**, and pass 2 passes 9/9.

**The cap is data, not a number in the module.** Q7 ruled it per-scenario, held in
Stub 7's `turnCap`; `initMatch` **refuses** a cap it cannot use rather than
substituting a default, which is what makes "no literal 20 lives here" enforced
instead of promised.

**T-TURN-08 asserts one thing only** — that the loop calls the already-verified
`repairAmount` at the right moment with the right board facts. Every expectation in
it is a direct call into `Combat.h`; the heal values are green at `5ffa8d6` under
T-REPAIR-01..07 and are not re-asserted.

Like row 4, it owns no board: units, hexes and Fame stay with their modules and
arrive as a caller-supplied `BoardSnapshot` — the same quantities §2.11.4's
scoreboard already displays, because §2.8's tiebreak adds no new state, only an
ordering over existing state.

### Row 6 — Opponent AI (the shipping opponent)

`spec/ai_spec.md`, **T-AI-01..06 + GATE-AI-SMOKE, 7/7** under both compilers. §4.4
puts it in week 3, at the end of the vertical slice. This is not a placeholder: §2.9
makes difficulty a **starting-Fame handicap and never a smarter routine**, so this one
routine is what every tier plays against.

**It decides and applies nothing.** The AI emits one ordinary command at a time and the
caller applies it — in the gate, through the debug driver's `execute`, the same door a
typed command uses. That is what makes T-AI-01's *"every AI command passes the same
validation as a player command"* structural rather than asserted, and it is why no
second rules composition exists in the test file to disagree with the modules.

The pass-1 hallucination is two readings an author reaches from §2.9 alone:

| Bug | Why it looks right | Caught by |
|---|---|---|
| the losing-attack guard skips any attack where the counter kills the unit | "skip a strictly-losing attack (the unit would die and trade down)" reads as one condition | **T-AI-05** |
| build ties break by the order §2.4's table **prints** its units | that is the order they are read in, and the order the `UnitType` enum pins | **T-AI-06** |

§2.9 joins **two** conditions, so dying is not by itself disqualifying — and Q9 ruled
the build priority is **ascending §2.4 cost** (Infantry > Recon > Artillery > Tank),
which §4.7 warns in as many words is not the printed order.

T-AI-05 is asserted as a **sweep over the whole shipped stat table** rather than on one
fixture: of 348 exchanges in which the counter kills the attacker, the guard skips 338
and permits 10, and every permitted one is checked not to trade down. The buggy build
permits **zero**, which is exactly the collapse into "never die" that the sweep exists
to catch — and no hand-picked fixture would have proved the difference so cheaply.

The self-play smoke runs six different openings; all six terminate at or before their
cap with a valid tier — two flag kills, two attrition leads, two passivity draws.

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
adjacency to `Hex.h`; capture, income and build to `Economy.h`; alternation, act flags,
the start-of-turn moment and the §2.8 result to `Turn.h`; the opponent's decisions to
`Ai.h`. Where an answer would need §4.11 rows 7–8 — what a scenario file looks like —
it **refuses the command instead of deciding it**. That is the whole design: a debug
tool that decides anything becomes a second rules implementation, and then the gated
modules are no longer what the game does.

`forecast` and `attack` call **one** computation, so §2.6's "the forecast the player sees
is exactly what resolves" is structural rather than merely tested — **GATE-DRV-03** then
asserts it anyway.

Its suite is **GATE-DRV-01..10**, named like `GATE-DATA-HARDFAIL` rather than `T-*`
because it gates a tool, not a rules system: it is not a §4.7 stub, it flips no §3 ledger
row, and it moves no count in the GDD. Every check compares the driver's output against a
direct module call rather than a hardcoded expectation.

Row 4 is reachable from the same surface — `objectives`, `fame`, `turn <n>`,
`income <side>`, `build <side> <Type> <col> <row>`, `capture <side>`, and a kill award
paid through `attack`:

```
> capture 0            side 0 captured (0,0)
> turn 1 / income 0    accrued 0 on turn 1 (no accrual on turn 1 — Q8)
> turn 2 / income 0    accrued 100 -> fameTotal 300
> build 0 Infantry 0 0 100 Fame committed at queue time, not refundable (Q8)
                       spawned #2 at (1,0)      <- factory hex occupied: the
                                                   adjacent-free fallback, T-FAME-04
```

Row 5 turns the same surface into a match — `match <firstSide> <turnCap>`, `endturn`,
`standings`, `result`, `flag <side> <id>`:

```
> match 0 20           side 0 moves first, cap 20 turns (scenario data, Q7)
                       turn 1/20 — side 0 to move
> move 2 3 1           refused: side 1 is not the active side (side 0 is)
> move 1 1 1           #1 moved ...
> move 1 1 0           refused: unit 1 has already acted this turn
> turn 9               refused: a match is running — the turn loop owns the number
> standings            Destroyed / Objectives / Unit HP + the current leader
> attack 1 2           ... destroyed — side 0 earns 500 Fame (flat flag award, Q5)
                       match over: Decisive (FlagDestroyed) — side 0 wins
```

**With no match running the board stays a free sandbox** — either side may act and a
unit may act twice — which is what `place`/`hp`/`remove` debugging needs, and why
GATE-DRV-01..07 are unchanged by row 5. `turn <n>` is still the **debug setter** it
always was, and applies only in that sandbox; once `match` runs, `Turn.h` owns the
number. `flag <side> <id>` is a **debug designation** standing in for Stub 7's
`isFlag` (row 7 unbuilt, Q10 open) — the human names the flag unit and the driver
never picks one.

Row 6 puts an opponent behind it. `ai` plays the active side's whole turn, printing
every command it issues and applying each through the **same `execute` a typed command
goes through** — which is what makes T-AI-01's "validated like any player command"
structural. `GATE-DRV-10` replays those printed lines by hand and asserts the state
hash matches, so the AI can reach no state a human could not type.

```
> ai
  ai> move 1 0 0        #1 moved, cost 1: (0,1)(0,0)
  ai> move 3 4 1        #3 moved, cost 4: (1,2)(1,1)(2,1)(3,1)(4,1)
  ai: end of turn
```

**What it deliberately is not:** there is no scenario file and no UI, because rows 7–8
hold no code. It is a debug tool with a real turn loop and a real opponent, not the game.

## Run it

```bash
# Offline — no API key, no install; needs only a C++ compiler. Always runs.
python run.py --offline

# Just the built rows (§4.11 rows 1-6) + the debug driver, skipping the combat crew:
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
spec/economy_spec.md     §4.11 row 4 contract — income, build, capture, awards
spec/turn_spec.md        §4.11 row 5 contract — alternation, repair moment, §2.8 result
spec/ai_spec.md          §4.11 row 6 contract — the shipping opponent, §2.9's routine
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
- `Hex.cpp`, `Data.cpp`, `Move.cpp`, `Economy.cpp`, `Turn.cpp`, `Ai.cpp` — the rules
  modules (§4.11 rows 1-6)
- `Driver.cpp` + `stratocracy_debug` — the debug-command REPL, week 1's playable artifact
- `acceptance.json` — the Combat release record (Test Engineer only)
- `acceptance_week1.json` — the rows 1-6 release record, including what it does **not**
  cover (T-DATA-05, T-MOVE-07) so a green run cannot imply full coverage
- `run_log.md` — full spec → gate → balance transcript (incl. both caught hallucinations)
- `balance_report.md` — self-play duel table + which invariant caught the bug

## Ties back to the rest of the project

Every real run yields a commit-able system plus its passing test IDs — the evidence the GDD §3
**provenance ledger** is waiting on. Point the ledger's *Combat resolution* row at the `Combat.cpp`
commit and the gate result (`T-COMBAT-01..10 + T-REPAIR-01..07`), and the attribution becomes
demonstrated fact — the same gate now also backs the ledger's pending *Repair* and
*Type-effectiveness* rows once a live run authors them.

The week-1 rows extend that chain to **§4.4's first milestone**. *Hex grid & math*,
*Movement & pathfinding*, *Capture & Fame economy*, *Turn loop & win/tiebreak* and
*Opponent AI* are complete at this commit — headless, no in-editor half, so their
acceptance sets close entirely here. *Data tables* is **not**: T-DATA-05 lives in the
editor, and Q29 requires the full acceptance set at one commit before a row flips.
Reporting rows 1, 3, 4, 5 and 6 as built and row 2 as still pending is the honest read
of the ledger's own rules, and it is what `acceptance_week1.json` records.
