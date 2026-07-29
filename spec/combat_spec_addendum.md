# SPEC ADDENDUM: Type-effectiveness + Repair  (Director → Systems Engineer)

Extends `spec/combat_spec.md` for two systems folded into the GDD on 2026-07-29
(GDD §2.4 note, §2.7 Repair, §3 combat-spec `eff`). Same rules as the base spec:
**namespace `strat`, zero engine deps, pure C++17, deterministic, test-first.**
Nothing here changes the numeric output of `T-COMBAT-01..08` — both features ship in
a **neutral** state and are proven neutral by new gates. Do not invent balance values.

---

## Part A — Type-effectiveness multiplier (`eff`)

### Why
GDD Pillar 3 ("unit rock-paper-scissors") is currently **positional** (range/move), not
a counter chart — see GDD §2.4. `eff` is the hook that lets a counter table be added
*later* **iff** self-play shows the positional triangle too weak. It ships as an all-1.0
table so the triangle stays positional and the existing gate is untouched.

### Data
A 4×4 effectiveness table indexed `[attackerType][defenderType]`, every cell **= 1.0**
in this revision. Unit types (order fixed; matches GDD §2.4):
`Infantry, Tank, Artillery, Recon`.

### Header additions (Director-owned contract — add to `Combat.h`)
```cpp
enum class UnitType { Infantry, Tank, Artillery, Recon };

// Append `type` as the LAST member of struct Unit so existing 6-field aggregate
// initializers ({atk,def,hp,hpMax,rangeMin,rangeMax}) still compile (type value-
// initializes to UnitType::Infantry). New code SHOULD name the type explicitly.
//   struct Unit { int atk, def, hp, hpMax, rangeMin, rangeMax; UnitType type; };

// Damage multiplier for an attacker type striking a defender type.
// THIS REVISION: returns 1.0 for every pair. Deterministic, pure.
double effectiveness(UnitType attacker, UnitType defender);
```

### Formula change
`resolveDamage` gains one factor — the signature is **unchanged**; it reads the types
off the two `Unit`s:
```
eff = effectiveness(attacker.type, defender.type)          // 1.0 everywhere this rev
dmg = max(1, round(atk * eff * (hp/hpMax) * (1 - terrainDefPct/100)) - def)
```
`eff` multiplies the **attacker's raw output only** — it is applied once, before the
`- def` subtraction and before the min-1 floor. It never touches the defender's counter
except through that counter's own `resolveDamage` call (where the counter-attacker's
own types drive its `eff`).

### Invariants (Test Engineer asserts — merge gate)
9. **Neutral stub** — `effectiveness(x, y) == 1.0` for **all 16** ordered type pairs.
   *(An agent that "helpfully" guesses non-1.0 balance numbers fails here — this is the
   analog of the `rangeMin` gate: ship the hook neutral, do not invent values.)*
10. **Baseline preserved** — with the neutral table, `resolveDamage` is byte-identical
    to the pre-`eff` module: e.g. `resolveDamage(tank, inf, 0) == 6` and
    `resolveDamage(inf, tank, 0) == 1` still hold (`T-COMBAT-01..08` unaffected).

*(When `eff` is later populated, add directional gates: attacker-type scales damage,
defender-type affects damage ONLY via `eff`, `eff==1.0` reproduces this baseline. Out
of scope until self-play requests it.)*

---

## Part B — Repair (owned-objective healing)

### Why
GDD §2.7. Today HP only ever decreases; every unit is a one-way asset, which quietly
rewards husbanding units (turtling, against Pillar 2). Repair is the one slice of
*Conflict*'s logistics we keep. It is a **pure function of board facts the caller
supplies** — the headless module never reaches into board state itself.

### Header additions (add to `Combat.h`)
```cpp
// HP a unit recovers at the start of its turn. Pure/deterministic; 0 when ineligible.
// The caller passes the two board facts:
//   onOwnedObjective — unit ends its turn on a Town/Factory it owns
//   enemyAdjacent    — an enemy unit is in an adjacent hex
int repairAmount(const Unit& u, bool onOwnedObjective, bool enemyAdjacent);
```

### Rule
```
if (!onOwnedObjective || enemyAdjacent || u.hp >= u.hpMax) return 0;
base = floor(0.25 * u.hpMax)            // +25% of max HP
if (base < 1) base = 1                  // min-1 floor
room = u.hpMax - u.hp
return min(base, room)                  // never overheal
```
The caller adds the return value to `hp`. Reference amounts (§2.4 hpMax): Infantry 10→2,
Tank 20→5, Artillery 8→2, Recon 12→3.

### Invariants (Test Engineer asserts — merge gate)
- **T-REPAIR-01 full-HP-no-heal** — `hp == hpMax` → `0`.
- **T-REPAIR-02 basic-heal** — Tank `{hp:10,hpMax:20}`, owned, not adjacent → `5`.
- **T-REPAIR-03 anti-fortress** — same Tank but `enemyAdjacent == true` → `0`.
  *(The gate. An agent that heals whenever `onOwnedObjective` — dropping the
  `!enemyAdjacent` clause — passes 01/02 but fails this. Direct analog of T-COMBAT-07.)*
- **T-REPAIR-04 must-own** — `onOwnedObjective == false` → `0`.
- **T-REPAIR-05 no-overheal** — Tank `{hp:18,hpMax:20}`, owned, not adjacent → `2`
  (capped at missing HP, not `5`).
- **T-REPAIR-06 min-1-floor** — a small unit `{hp:1,hpMax:3}`, owned, not adjacent → `1`
  (`floor(0.75)=0` → floored up to 1).
- **T-REPAIR-07 determinism** — same inputs → identical result; no RNG, no I/O.

---

## Test-gate additions (Test Engineer — write these BEFORE implementation)
Append to `test_combat.cpp` (same `check(name, cond)` harness; non-zero exit on any fail):
```cpp
// --- Part A: type-effectiveness (neutral this revision) ---
const UnitType TYPES[4] = {UnitType::Infantry, UnitType::Tank,
                           UnitType::Artillery, UnitType::Recon};
bool allNeutral = true;
for (auto a : TYPES) for (auto d : TYPES) if (effectiveness(a, d) != 1.0) allNeutral = false;
check("T-COMBAT-09 eff-neutral-stub", allNeutral);
// tank vs infantry on plains: round(8*1.0*1.0*1.0)-2 = 6, unchanged by eff
check("T-COMBAT-10 eff-baseline-preserved", resolveDamage(tank, inf, 0) == 6);

// --- Part B: repair ---  (tank {8,5,20,20,1,1,UnitType::Tank}; small {1,1,1,3,1,1,...})
const Unit tankHurt{8,5,10,20,1,1, UnitType::Tank};
const Unit tankNick{8,5,18,20,1,1, UnitType::Tank};
const Unit tankFull{8,5,20,20,1,1, UnitType::Tank};
const Unit small   {1,1, 1, 3,1,1, UnitType::Infantry};
check("T-REPAIR-01 full-hp-no-heal", repairAmount(tankFull, true,  false) == 0);
check("T-REPAIR-02 basic-heal",      repairAmount(tankHurt, true,  false) == 5);
check("T-REPAIR-03 anti-fortress",   repairAmount(tankHurt, true,  true ) == 0);
check("T-REPAIR-04 must-own",        repairAmount(tankHurt, false, false) == 0);
check("T-REPAIR-05 no-overheal",     repairAmount(tankNick, true,  false) == 2);
check("T-REPAIR-06 min-1-floor",     repairAmount(small,    true,  false) == 1);
check("T-REPAIR-07 determinism",
      repairAmount(tankHurt, true, false) == repairAmount(tankHurt, true, false));
```

## Acceptance
`T-COMBAT-01..10` **and** `T-REPAIR-01..07` must all pass before the extended module is
accepted (Test Engineer gate, same certification path as the base spec).

## Integration checklist (crew wiring — not yet applied)
So the crew actually authors + gates these, the next build needs:
1. **`Combat.h`** — add `enum class UnitType`, the `type` field (last), and the two
   declarations above.
2. **`test_combat.cpp`** — add the asserts above (test-first).
3. **`crew/tools.py`** — bump the acceptance record string `"tests"` from
   `"T-COMBAT-01..08"` to `"T-COMBAT-01..10, T-REPAIR-01..07"` (line ~128); the
   compile+run gate itself is generic and needs no other change.
4. **`crew/tasks.py` / agent prompts** — hand the Systems Engineer *both*
   `spec/combat_spec.md` and this addendum.
5. *(Optional, for the offline demo)* add `cpp_reference/Combat.good.cpp` bodies +
   a `Combat.buggy.cpp` that drops the `!enemyAdjacent` clause, so the offline path can
   demo the anti-fortress gate the way it demos T-COMBAT-07.
6. GDD §3 ledger already carries the pending **Repair** and **Type-effectiveness** rows
   (added 2026-07-29) — they flip to ✓ + commit/test IDs once this gate is green.
