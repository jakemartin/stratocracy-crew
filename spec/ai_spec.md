# SPEC: Opponent AI (baseline)  (Director → Systems Engineer)

Game: **Stratocracy** — UE5.8 hex turn-based strategy. Headless (§4.1): **zero
engine dependencies**, pure C++17, deterministic.

This is **§4.7 SPEC STUB 6**, §4.11 build-order **row 6**. It depends on row 5,
which has landed, and it is the critical path's next link on
`1 → 3 → 4 → 5 → 6/8`.

This is **the shipping opponent** (§2.9), not a prototype stand-in. Difficulty is
a starting-Fame handicap and never a smarter routine, so this one routine is what
every tier plays against.

## It decides; it applies nothing

The AI emits **one ordinary command at a time** and the caller applies it through
the same path a player's command takes (§2.9). That is what makes T-AI-01's
"validated like any player command" **structural** rather than asserted: the gate
drives the AI through the debug driver, so every command it issues is rendered to
the command line a human would type and handed to the same `execute`.

It holds no rules. Reachability and routes are `Move.h`; damage, counters and
repair are `Combat.h`; stats are `Data.h`; affordability and kill value are
`Economy.h`; act flags and alternation are `Turn.h`. It also owns no board — it
reads an `AiState` the caller composes, and there is **no field on it a player
could not read off the screen** (§4.7 Stub 6: the AI cheats at nothing).

## Inputs

Full game state; the §2.9 baseline routine; the default buildlist (§2.9).

## Required functions

```
strat::AiCommand strat::nextCommand(const AiState&, int side);
int  strat::expectedDamage(const AiState&, const AiUnit& atk, const AiUnit& def);
bool strat::isStrictlyLosing(const AiState&, const AiUnit& atk, const AiUnit& def);
int  strat::exchangeValueDealt(const AiState&, const AiUnit& atk, const AiUnit& def);
int  strat::exchangeValueLost(const AiState&, const AiUnit& atk);
int  strat::chooseBuild(const AiState&, int side);
bool strat::buildPriorityLess(const UnitDef&, const UnitDef&);
```

## Invariants (the merge gate)

- **T-AI-01** — legality: every AI command passes the same validation as a player
  command; **zero rejected commands** across N self-play games.
- **T-AI-02** — economy phase, first: at each held factory, if a buildlist unit is
  affordable, one is built — the AI spends and replaces losses rather than
  hoarding (§2.9).
- **T-AI-03** — capture behaviour: an idle Infantry near an uncaptured,
  **undefended** factory/town moves onto it to capture (§2.9); objectives stay
  live on both sides.
- **T-AI-04** — attack preference: the enemy flag if in reach, else the best
  expected-damage target; Artillery fires from maximum standoff (§2.9).
- **T-AI-05** — strictly-losing-attack guard: the AI never makes an attack in
  which its unit dies **and** trades down (§2.9). **Both halves bind** — dying is
  not by itself disqualifying.
- **T-AI-06** — determinism: same state → same move; every scoring tie breaks by a
  stated deterministic rule (**Q9, ruled**): position and target ties by canonical
  hex order — for a target, the hex it occupies — and build ties by the fixed
  priority **Infantry > Recon > Artillery > Tank**, which is **ascending §2.4
  cost** and **not** the order §2.4's table prints.

Plus **GATE-AI-SMOKE**, §4.11 row 6's self-play smoke: N headless AI-vs-AI games
all terminate at or before the cap with a valid result tier. It is acceptance but
carries no numbered ID in the GDD, so it is named `GATE-AI-SMOKE` rather than
`T-AI-07` — minting a `T-` ID here would move §4.5's count.

### Five stated readings

Each is a **documented choice**, not a rule. The GDD requires a determinate
answer and leaves the term undefined; none adds a rule the GDD does not have.

1. **The buildlist is data.** §2.9 calls it "mostly Infantry, an occasional Tank"
   and gives no ratio, no period and no rule. The module takes the list as a
   caller-supplied parameter and applies Q9's priority among its affordable
   members. Inventing a ratio would be a balance rule the GDD has not written.
   **The Director has since written one** — a per-type population cap, change
   request 3 below. This reading stays correct as the module's position until
   that lands: the list is still data, and the cap is data the caller supplies
   too, not a ratio the module invents.
2. **"Undefended"** (T-AI-03) = no enemy stands on the objective **and** none is
   adjacent to it. Occupancy alone would leave the word doing no work, since
   `Move.h` already refuses to enter an occupied hex.
3. **"Near"** (T-AI-03) = reachable this turn, and among reachable objectives the
   **cheapest to reach**, ties by canonical hex order. Taking the canonically
   first outright would walk a unit past a factory beside it to one across the map.
4. **"Trades down"** (T-AI-05) — §2.9 gives no metric, so: **value dealt** is the
   victim's `Economy.h` kill award prorated by the damage share of its max HP;
   **value lost** is the attacker's own kill award, unprorated, because that is
   what the enemy actually collects (§2.7). Both prices come from `killAward`.
5. **The advance goal when no flag is designated.** §2.9 says "toward the enemy
   flag"; `isFlag` is Stub 7's placement field (row 7, **built** at `9086d6a` on a
   partial pass, so the field exists and its ledger row does not flip) and Q10 is
   open on exactness,
   so with no flag on the board the goal is the **canonically first enemy unit**.

## Determinism / constraints

Pure function of state; **no RNG anywhere**; no clock; no I/O. Difficulty changes
only starting Fame (§2.9), never the routine — nothing in this module reads a
difficulty tier at all. Must compile with a plain C++17 compiler.

## Change requests for the Director

The first two are pre-existing consequences of rows landing in order, surfaced by
row 6 because the AI is the first caller to exercise them. Neither is worked
around silently. The third is not that class and is not a question: it is a
ruling already made, recorded here so the work has a home and a shape before
anyone writes it.

1. **§2.7's "one build per factory per turn" is enforced only as "one *pending*
   build per factory".** That was the whole of it while no module owned the turn
   (row 4's stated reading 2, written before row 5 existed): a waiting build holds
   the slot, so the slot is busy until it spawns. Row 5 now owns the turn, and
   once a build spawns the slot frees **within the same turn**, so nothing refuses
   a second build at that factory. Row 6 takes `builtThisTurn` as a
   caller-supplied board fact rather than keeping a counter of its own, and the
   driver maintains it — but a **player's** `build` command is not gated by it.
   Whether the per-turn half belongs to row 5, to row 4, or stays a caller
   obligation is a Director call.
2. **§2.1 has a unit move *and* act; row 5 has one act flag.** §2.1's loop is
   *select → move → act (attack / capture / build) → done*, and §4.9's snapshot
   carries a single per-unit `hasActed`. Row 5 models one flag, and the driver
   marks it on both `move` and `attack`, so a unit does one or the other. The AI
   therefore cannot close and strike in the same turn, which weakens §2.9's
   "if an enemy is within reach **after moving**, attack". Whether row 5 gains a
   second flag, or `hasActed` is defined to mean only the act step with movement
   tracked separately, is a Director call.
3. **Build variety — a per-type population cap. RULED 2026-08-19; this one wants
   building, not deciding.** §2.9's "mostly Infantry, an occasional Tank" has no
   representation anywhere in this module. `chooseBuild` returns the cheapest
   affordable buildlist entry, and there is no Fame level at which Tank (300) is
   affordable and Infantry (100) is not, so **with Infantry in the list the Tank
   entry is unreachable at every Fame level** and "an occasional Tank" is not an
   observable outcome. `AiState::buildlist` compounds it: `chooseBuild` reads it
   as a set, so neither order nor multiplicity expresses anything, and a caller
   told the mix is theirs to decide holds a container that cannot carry one.

   The ruling: **a side may not have more than N units of a type on the board at
   once.** At its cap a type is ineligible; `buildPriorityLess` orders whatever
   remains eligible and affordable; when nothing does, `chooseBuild` returns -1
   as it does today and the side accrues Fame until a casualty frees the cap or
   the dearer unit becomes affordable.

   It costs this module less than it looks. **The saving behaviour needs no
   code** — `nextCommand`'s economy block already reads `if (defIndex < 0)
   continue;`, so an empty eligible set is a turn that spends nothing; hoarding
   is emergent from the cap rather than a second mechanism. **Determinism is
   untouched**: a population count is state, not randomness, so no RNG, no
   clock, no cursor, and nothing carried between calls — `T-AI-06` and the
   replay fixtures stand. **Q9 is untouched**: the cap filters eligibility and
   the comparator still orders what survives, so the ruled Infantry > Recon >
   Artillery > Tank priority is neither reversed nor reinterpreted. And **the AI
   still cheats at nothing**: `AiState::units` already carries `side` and
   `defIndex`, and a unit count is the most player-visible fact on the board.

   Three things the Director left to this module, in descending order of how
   badly they bite:

   - **The cap must count `economy.pending` alongside alive units.** The economy
     block calls `chooseBuild` afresh for each held factory in canonical order
     within one turn, and a queued build sits in `economy.pending` before it
     becomes a unit. Counting only `units` puts 2 alive against a cap of 3 in
     front of three factories, each sees room, and the board lands at 5. This is
     a correctness requirement, not a choice, and it wants its own clause — a
     single-factory fixture passes the bug.
   - **Where the cap numbers live.** Buildlist multiplicity would carry them for
     free and would finally give those duplicate entries a meaning
     (`{Infantry, Infantry, Infantry, Tank}` reading as its own quota table),
     with no change to `AiState`'s shape and none to the caller's. Against it:
     every list anyone has already authored silently acquires caps equal to how
     many times someone happened to type each entry. An explicit parallel cap
     vector on `AiState`, in the idiom `buildlist` and `builtThisTurn` already
     use, says what it means and can express "uncapped". **Recommended, with the
     multiplicity reading noted as the cheaper alternative rather than hidden.**
   - **§2.9's "spends Fame and replaces losses instead of hoarding" wants an
     explicit note.** A cap makes the AI hoard. Bounded (it ends at the dearer
     unit's cost or at the first casualty) and directed (it exists to reach the
     Tank §2.9 asks for), so the reading is that the cap supplies the missing
     half of §2.9 rather than contradicting it — but §2.9 is GDD text, and that
     inference should be written down here rather than drawn by whoever
     implements the cap.

   Full investigation, including the byte-for-byte confirmation that this
   module has not moved since it was vendored and the four design options
   weighed before the ruling:
   `Stratocracy/Tools/architect/evidence/upstream-chooseBuild-buildlist-ratio.md`.

## Acceptance

`T-AI-01..06` plus `GATE-AI-SMOKE`. Row 6's ledger row flips only on the full set
at one commit (Q29).
