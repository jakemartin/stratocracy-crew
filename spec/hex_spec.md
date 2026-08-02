# SPEC: Hex grid & math  (Director → Systems Engineer)

Game: **Stratocracy** — UE5.8 hex turn-based strategy. This is the headless rules
module (§4.1): **zero engine dependencies**, pure C++, deterministic, unit-testable
without launching Unreal.

This is **§4.7 SPEC STUB 1**, §4.11 build-order row 1, and the first of the three
rows §4.4 week 1 owes. It has no dependencies — Q1 pins its bounds.

## Coordinates (the §4.7 shared conventions, verbatim in force)

Axial `(q, r)`, pointy-top hexes (§2.2). Distance is the standard axial hex metric

```
distance(a, b) = (|dq| + |dr| + |dq + dr|) / 2      where dq = a.q − b.q, dr = a.r − b.r
```

pure integer math. **Two conventions coexist deliberately.** §2.13's authored maps
use odd-r offset `(col, row)` — what a human reads off an ASCII grid — while the
module uses axial, because the metric above is only clean in axial. The conversion
is part of this contract:

```
q = col − (row − (row & 1)) / 2 ,  r = row          (odd-r offset → axial)
```

Neither convention leaks into the other's layer: no authored file stores axial, and
no module code stores `(col, row)` — the offset pair exists only as the argument and
result of the two conversion functions below.

**Canonical hex order:** ascending `r`, then ascending `q`. This is the single total
order used anywhere enumeration could leak into behaviour or bytes (T-HEX-07, and
downstream T-MOVE-04/06, T-AI-06, Stub 7 serialization, the §4.10 state hash).

## Bounds (Q1)

Q1 is ruled: **bounds are per-scenario data, not a global constant.** The module
therefore takes a `MapBounds{cols, rows}` — the odd-r offset rectangle — and never
hardcodes a size. *Ferrum Crossing* ships 11×9 (§2.13.2) and is what the gate uses
as its fixture; nothing in the module knows that number.

## Required functions

All declarations live in **`namespace strat`** (see `Hex.h`).

```
bool strat::hexEqual(const Hex& a, const Hex& b);
bool strat::hexLess(const Hex& a, const Hex& b);          // canonical order: r asc, then q asc
int  strat::hexDistance(const Hex& a, const Hex& b);
bool strat::inBounds(const Hex& h, const MapBounds& b);
Hex  strat::neighborCandidate(const Hex& h, int dir);     // dir 0..5, FIXED order, unfiltered
int  strat::neighbors(const Hex& h, const MapBounds& b, Hex out[6]);   // filtered; returns count
Hex  strat::offsetToAxial(int col, int row);
void strat::axialToOffset(const Hex& h, int& col, int& row);
void strat::sortCanonical(std::vector<Hex>& hexes);
```

**The fixed enumeration order** (§4.7 requires *a* fixed, documented order and does
not name one; this is that documentation, and it is now contract):

| dir | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| name | E | NE | NW | W | SW | SE |
| `(dq, dr)` | `(+1, 0)` | `(+1, −1)` | `(0, −1)` | `(−1, 0)` | `(−1, +1)` | `(0, +1)` |

`neighborCandidate` applies the offset and nothing else — it does **not**
bounds-check, so the six candidates always exist and T-HEX-01 can assert that
filtering removes *only* out-of-bounds hexes. `neighbors` filters and preserves the
order of the survivors.

## Invariants (Test Engineer asserts each — these are the merge gate)

- **T-HEX-01** — every in-bounds hex has exactly six neighbor candidates in the
  fixed order; filtering removes only out-of-bounds hexes (§2.2 "six equal
  neighbours").
- **T-HEX-02** — distance is a metric: `d(a,a)=0`; `d(a,b)=d(b,a)`; triangle
  inequality.
- **T-HEX-03** — `d(a,b)=1` ⟺ `b ∈ neighbors(a)`.
- **T-HEX-04** — direction fairness: each of the six unit steps has distance exactly
  1 — no direction is cheap the way diagonals are on squares (§2.2).
- **T-HEX-05** — `inBounds` agrees with the Q1 dimensions; every hex reference the
  engine or a scenario hands the module is bounds-checked, never trusted.
- **T-HEX-06** — single distance definition: combat's range checks
  (T-COMBAT-06..08 @ `5ffa8d6`) consume **this** distance function — Artillery at
  distance 1 cannot counter, per the verified module, with no second metric to drift.
- **T-HEX-07** — canonical order + determinism: sorting any hex set by (r asc, q asc)
  is total, stable and platform-independent; `neighbors()` enumeration order is fixed
  across runs and compilers.

## Determinism / constraints

Pure integer functions; **no state**, no I/O, no globals, no floating point, no
engine types. Must compile with a plain C++17 compiler (`g++`/`clang++`/`cl`) — no
Unreal, no third-party libraries.

## Acceptance

`T-HEX-01..07` must all pass before the module is accepted (Test Engineer gate).
