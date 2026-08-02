# SPEC: Data tables (units / terrain / effectiveness)  (Director → Systems Engineer)

Game: **Stratocracy** — UE5.8 hex turn-based strategy. This is the headless rules
module (§4.1): **zero engine dependencies**, pure C++, deterministic, unit-testable
without launching Unreal.

This is **§4.7 SPEC STUB 2**, §4.11 build-order row 2, and the second of the three
rows §4.4 week 1 owes. §4.7 Stub 2 defers to **§4.8** rather than duplicate a
contract that must never fork, so the schemas below are §4.8's, transcribed once.

## Principle (§4.8)

**Authored once, read twice, proven equal.** Each table is one canonical CSV in
`data/`. The headless loader parses it directly; the Unreal editor imports the same
file into a `UDataTable` whose row struct derives `FTableRowBase`. Nothing is
authored twice, so the headless sim and the engine can never disagree about a stat.

**Missing column or unparseable value = hard load failure, never a silent default.**
That is a load-time rule, not a test-time one: `loadUnits`/`loadTerrain`/`loadEffectiveness`
return `false` with a reason string, and the caller gets no table at all.

## `data/units.csv` → `strat::UnitDef` → UStruct `FUnitRow`

Exactly four rows (Infantry, Tank, Artillery, Recon; §2.4). The **flag unit is not a
row** — §2.4 defines it as "a designated Tank," so flag status is a placement-level
field in the scenario file (`isFlag`, Stub 7), gated by T-SCN-01, not a fifth type.

| Column | CSV type | Headless field | Source |
|---|---|---|---|
| `Id` (row name) | string | `id` | §2.4 |
| `HP` | int | `hpMax` | §2.4 (10/20/8/12) |
| `Move` | int | `move` | §2.4 (3/5/3/7) |
| `Atk` | int | `atk` | §2.4 (4/8/10/5) |
| `Def` | int | `def` | §2.4 (2/5/1/3) |
| `RangeMin` | int | `rangeMin` | §2.4 (Artillery 2, others 1) |
| `RangeMax` | int | `rangeMax` | §2.4 (Artillery 3, others 1) |
| `CostFame` | int | `costFame` | §2.4 (100/300/200/150) |
| `Type` | enum string | `strat::UnitType type` | addendum Part A — order fixed: Infantry, Tank, Artillery, Recon |
| `CanCapture` | bool | `canCapture` | §2.7 (Infantry only) |
| `MoveClass` | enum string | *reserved* | **blocked on Q2** |

**`MoveClass` is reserved, and the shipped CSV does not carry it.** §4.8 lists the
column with *reserved* in both the headless and the UStruct field cells because Q2 —
the movement-class ruling — is open, and §2.3's single cost column applies to every
land unit until it is ruled. Writing a value would invent the class set Q2 exists to
decide, and writing an empty required column would trip the hard-fail rule. So the
loader **neither requires nor consumes** `MoveClass`; when Q2 rules, the column is
added to the CSV and to this contract in one edit, and T-MOVE-07 is written against
it. Unknown extra columns are ignored, so adding it early breaks nothing.

`strat::UnitType` is **not redeclared here.** It lives in `Combat.h` (addendum Part
A) with its enumerator order pinned, and `Data.h` includes `Combat.h` to get it —
one declaration, so an editor-side or loader-side reorder cannot silently reindex the
effectiveness matrix.

## `data/terrain.csv` → `strat::TerrainDef` → UStruct `FTerrainRow`

Exactly seven rows (§2.3).

| Column | CSV type | Headless field | Source |
|---|---|---|---|
| `Id` (row name) | string | `id` | §2.3 |
| `MoveCost` | int (**0 = impassable**) | `moveCost` | §2.3 (Plains 1, Woods 2, Mountains 3, Water —, Town 1, Bridge 1, Factory 1) |
| `DefensePct` | int, **signed** | `defensePct` | §2.3 (0, 20, 40, 0, 10, **−10**, 15) |
| `PassLand` / `PassAir` / `PassSea` | bool ×3 | `passLand/passAir/passSea` | §2.3 Passable column |
| `Capturable` | bool | `capturable` | §2.3 (Town, Factory) |
| `IncomeFame` | int | `incomeFame` | §2.7 (Factory 100, Town 25, else 0) |
| `IsSpawnPoint` | bool | `isSpawnPoint` | §2.7 (Factory) |
| `IsRepairPoint` | bool | `isRepairPoint` | §2.7 Repair (Town + Factory) |

Water's `MoveCost` of `0` is the impassable sentinel and its `PassLand` is `false`;
those two say the same thing from two directions and T-DATA-02 asserts both, because
Bridge (§2.3, "the only hex a land unit crosses Water") is only meaningful if Water
is genuinely closed.

## `data/effectiveness.csv` → `strat::effectiveness`

A 4×4 matrix, **row = attacker type, columns = defender types**, values ∈
{0.5, 1.0, 1.5} (§3 spec), **shipping all-1.0** (§2.4 — the triangle stays
positional). The verified implementation (`Combat.cpp::effectiveness` @ `5ffa8d6`)
hardcodes the neutral stub; this file is the lever's *data* form, so that if
self-play ever asks for a non-1.0 cell, populating it is a CSV edit gated by the
existing directional-gate plan, not a code change.

The header row and the first column both name the four types **in the pinned order**,
and the loader asserts that rather than assuming it — an out-of-order file is a hard
load failure, not a silently transposed matrix.

T-COMBAT-09 (neutral stub, 16/16 pairs) continues to pin the shipped state at the
code layer; T-DATA-06 re-asserts it at the data layer. A non-neutral CSV with
T-COMBAT-09 still in the suite is a deliberate, visible gate change the Director must
approve — the "do not invent balance values" rule enforced by the pipeline itself.

## Required functions

```
bool strat::loadUnits(const std::string& path, std::vector<UnitDef>& out, std::string& err);
bool strat::loadTerrain(const std::string& path, std::vector<TerrainDef>& out, std::string& err);
bool strat::loadEffectiveness(const std::string& path, double out[4][4], std::string& err);
const strat::UnitDef*    strat::findUnit(const std::vector<UnitDef>& units, const std::string& id);
const strat::TerrainDef* strat::findTerrain(const std::vector<TerrainDef>& terrain, const std::string& id);
```

## Invariants (Test Engineer asserts each — these are the merge gate)

- **T-DATA-01** — loaded unit values equal the §2.4 table exactly (4 rows, all columns).
- **T-DATA-02** — loaded terrain values equal the §2.3 table exactly (7 rows),
  including Bridge's **negative** defense and Water impassable-to-land.
- **T-DATA-03** — exactly one unit row has `CanCapture == true` (Infantry, §2.7).
- **T-DATA-04** — sanity: all costs > 0; `RangeMin <= RangeMax`; `HP > 0`.
- **T-DATA-05** — *(editor, Unreal Automation)* every imported DataTable row equals
  the CSV field-for-field, and `EUnitType` mirrors `strat::UnitType` exactly.
  **This half is not headless and does not run in this gate** — it is §4.11's
  **†**-marked ID for row 2, and its absence is what holds row 2's ledger flip under
  Q29 even when everything below passes.
- **T-DATA-06** — `effectiveness.csv` is 4×4, indexed in the pinned type order, every
  cell ∈ {0.5, 1.0, 1.5}; the **shipped** file is all-1.0.

## Determinism / constraints

Pure parse; **missing/malformed field = hard fail, no defaults**. No engine types.
Must compile with a plain C++17 compiler (`g++`/`clang++`/`cl`) — no Unreal, no
third-party libraries, no CSV library.

## Acceptance

`T-DATA-01..04, 06` headless (this gate). `T-DATA-05` in the editor pass.
