"""Deterministic offline pipeline — the same spec→gate→balance flow without any API.

Runs when no ANTHROPIC_API_KEY is set (or with `--offline`). It uses bundled reference
implementations to stand in for the Systems Engineer's authorship, so the whole crew
executes end-to-end on any machine with a C++ compiler and never crashes.

It also *demonstrates the gate catching a hallucination*: pass 1 deliberately submits
the over-generalized counter rule (Artillery counters at range 1), the gate blocks it on
T-COMBAT-07, then pass 2 submits the corrected rule and the gate passes.
"""
from __future__ import annotations

from . import tools


def run_offline(log) -> dict:
    log("MODE: offline deterministic pipeline (no API key) — the crew's spec→gate→"
        "balance flow with bundled authorship.\n")

    # --- Systems Engineer, pass 1: the hallucinated implementation --------------
    log("[Director -> Systems Engineer] spec/combat_spec.md handed over.")
    log(tools.write_combat_impl_fn(tools.read_reference("Combat.buggy.cpp")))
    log("[Systems Engineer] pass 1 authored — two plausible-but-wrong rules: counter = "
        "'distance <= rangeMax' (drops rangeMin), and repair heals even in enemy contact "
        "(drops the anti-fortress clause).\n")

    # --- Systems Engineer self-test: catches its own hallucination on pass 1 ----
    r1 = tools.run_test_gate_fn()
    if not r1["compiled"]:
        log("[Systems Engineer · self-test] compile FAILED — " + r1["log"])
        log("\n[stop] No usable C++ compiler on PATH. On Windows, open the "
            "'x64 Native Tools Command Prompt for VS' (so cl.exe is on PATH) and re-run; "
            "or install g++/clang++. Nothing else is wrong — the crew just can't build.")
        return {"status": "no_compiler", "gate_passed": False, "failures_caught": []}
    log("[Systems Engineer · self-test] " + r1["summary"])
    for line in r1["log"].splitlines():
        log("    " + line)
    if r1["passed"]:
        log("[note] pass 1 unexpectedly passed — the bundled 'buggy' impl should fail "
            "T-COMBAT-07; continuing anyway.\n")
    else:
        log(f"[Systems Engineer · self-test] BLOCK — {', '.join(r1['failures'])} caught the "
            "hallucinated rules. Fixing before hand-off.\n")

    # --- Systems Engineer, pass 2: corrected implementation ---------------------
    log("[Systems Engineer] re-fed invariant 7 (range band) and T-REPAIR-03 "
        "(anti-fortress); correcting both.")
    log(tools.write_combat_impl_fn(tools.read_reference("Combat.good.cpp")))
    log("[Systems Engineer] pass 2 authored (counter honors [rangeMin, rangeMax]; repair "
        "refuses when enemy-adjacent).\n")

    r2 = tools.run_test_gate_fn()
    log("[Systems Engineer · self-test] " + r2["summary"])
    for line in r2["log"].splitlines():
        log("    " + line)
    if not r2["passed"]:
        log("[stop] pass 2 did not pass self-test — see the failures above.")
        return {"status": "error", "gate_passed": False,
                "failures_caught": r1.get("failures", [])}

    # --- Test Engineer: the SOLE release authority — certifies and writes the record
    cert = tools.certify_build_fn()
    log("[Test Engineer] certify_build -> " + cert["summary"] + f" | accepted={cert['accepted']}")
    log("[Test Engineer] acceptance record written to build/acceptance.json; "
        "Balance Analyst is cleared to run.\n")

    # --- Balance Analyst --------------------------------------------------------
    b = tools.run_self_play_fn()
    log("[Balance Analyst] " + b["summary"])
    for line in b["log"].splitlines():
        log("    " + line)
    log("\n[Balance Analyst] Read-out: Tank dominates every 1v1 on plains; Artillery "
        "loses to melee attackers because the sim forces distance=1, eating counters it "
        "would avoid at standoff. Proposal: add range-2/3 duels to the sim before tuning "
        "Artillery's stats — the weakness is a methodology artifact, not (yet) a balance bug.")

    return {"status": "ok", "gate_passed": r2["passed"],
            "failures_caught": r1.get("failures", [])}


def run_week1(log) -> dict:
    """The same spec -> gate -> certify flow for GDD §4.11 rows 1-6 (§4.4 week 1,
    plus rows 4, 5 and 6, which week 1 does not owe but which the critical path runs
    through).

    Rows 1 and 2 are authored and gated first because row 3 depends on both (§4.11's
    Depends-on column). Row 3 then repeats the Combat demonstration with the movement
    hallucination: pass 1 highlights every hex within `hexDistance <= move`, which
    looks right and ignores terrain entirely — the failure §2.5 names in advance when
    it promises "the real move set, not an estimate".
    """
    log("\n" + "=" * 78)
    log("WEEK 1 — GDD §4.11 rows 1-6 + the debug-command driver")
    log("=" * 78 + "\n")

    # Combat is a PREREQUISITE, not a work item (§4.11: "green at 5ffa8d6 and are
    # prerequisites"). Every week-1 target links it, so place it when the combat
    # stage has not already authored it -- otherwise `--week1` on a clean tree
    # fails to link and reports it as a compiler problem, which it is not.
    if not (tools.BUILD / tools.IMPL).exists():
        tools.ensure_workspace()
        log(tools.write_combat_impl_fn(tools.read_reference("Combat.good.cpp")))
        log("[Director] Combat placed as a prerequisite — green at 5ffa8d6, not a "
            "work item for this week (§4.11).\n")

    # --- rows 1 and 2: the two with no dependencies -----------------------------
    for key, spec_file, note in (
        ("hex",  "spec/hex_spec.md",
         "axial (q,r), the six-direction fixed order, canonical order r-asc then q-asc"),
        ("data", "spec/data_spec.md",
         "the §4.8 CSVs — hard fail on a missing column, never a silent default"),
    ):
        row = tools.WEEK1_ROWS[key]
        log(f"[Director -> Systems Engineer] {spec_file} handed over (row {row['row']}"
            f" — {row['system']}).")
        log(tools.write_module_impl_fn(key, tools.read_reference(
            row["impl"].replace(".cpp", ".good.cpp"))))
        log(f"[Systems Engineer] authored — {note}.")
        r = tools.run_row_gate_fn(key)
        if not r["compiled"]:
            log("[Systems Engineer · self-test] compile FAILED — " + r["log"])
            log("\n[stop] No usable C++ compiler on PATH. On Windows, open the "
                "'x64 Native Tools Command Prompt for VS' and re-run; or install "
                "g++/clang++. Nothing else is wrong — the crew just can't build.")
            return {"status": "no_compiler", "gate_passed": False, "failures_caught": []}
        log("[Systems Engineer · self-test] " + r["summary"])
        for line in r["log"].splitlines():
            log("    " + line)
        if not r["passed"]:
            log("[stop] row " + str(row["row"]) + " did not pass — see the failures above.")
            return {"status": "error", "gate_passed": False, "failures_caught": r["failures"]}
        log("")

    # --- row 3, pass 1: the hallucination ---------------------------------------
    log("[Director -> Systems Engineer] spec/move_spec.md handed over (row 3 — "
        "Movement & pathfinding; §4.11 says it depends on rows 1 and 2, both now green).")
    log(tools.write_module_impl_fn("move", tools.read_reference("Move.buggy.cpp")))
    log("[Systems Engineer] pass 1 authored — the reachable set is computed as "
        "'every hex within hexDistance <= move'. Terrain cost is never consulted.\n")

    m1 = tools.run_row_gate_fn("move")
    log("[Systems Engineer · self-test] " + m1["summary"])
    for line in m1["log"].splitlines():
        log("    " + line)
    if m1["passed"]:
        log("[note] pass 1 unexpectedly passed — the bundled 'buggy' movement impl "
            "should fail T-MOVE-01; continuing anyway.\n")
    else:
        log(f"\n[Systems Engineer · self-test] BLOCK — {', '.join(m1['failures'])} caught it. "
            "T-MOVE-01 compares the set against an independent shortest-path pass, so "
            "an estimate cannot pose as the real move set; T-MOVE-02 prices Woods at 2 "
            "and refuses Water; T-MOVE-03 catches that a blocked bridge no longer "
            "blocks. Fixing before hand-off.\n")

    # --- row 3, pass 2: corrected -----------------------------------------------
    log("[Systems Engineer] re-fed T-MOVE-01/02/03; replacing the estimate with "
        "Dijkstra over terrain cost, ties broken by canonical hex order.")
    log(tools.write_module_impl_fn("move", tools.read_reference("Move.good.cpp")))
    m2 = tools.run_row_gate_fn("move")
    log("[Systems Engineer · self-test] " + m2["summary"])
    for line in m2["log"].splitlines():
        log("    " + line)
    if not m2["passed"]:
        log("[stop] pass 2 did not pass self-test — see the failures above.")
        return {"status": "error", "gate_passed": False,
                "failures_caught": m1.get("failures", [])}

    # --- row 4: the next link on the critical path -------------------------------
    log("\n[Director -> Systems Engineer] spec/economy_spec.md handed over (row 4 — "
        "Capture & Fame economy). Four of its nine invariants encode a RULED question "
        "(Q4, Q5, Q6, Q8), so the gate asserts the ruling and not the intuition it "
        "overturned.")
    log(tools.write_module_impl_fn("fame", tools.read_reference("Economy.buggy.cpp")))
    log("[Systems Engineer] pass 1 authored — income accrues on turn 1 (every strategy "
        "game pays you on turn 1), and passive income also credits fameCombat (Fame is "
        "one pool, so surely every source touches every counter).\n")

    f1 = tools.run_row_gate_fn("fame")
    log("[Systems Engineer · self-test] " + f1["summary"])
    for line in f1["log"].splitlines():
        log("    " + line)
    if f1["passed"]:
        log("[note] pass 1 unexpectedly passed — the bundled 'buggy' economy should "
            "fail T-FAME-01 and T-FAME-02; continuing anyway.\n")
    else:
        log(f"\n[Systems Engineer · self-test] BLOCK — {', '.join(f1['failures'])} caught it. "
            "Q8 ruled that turn-1 buying power is starting Fame ALONE; and fameCombat is "
            "§2.8's tiebreak sort key, so crediting income to it would let a side that "
            "never fought win criterion 1 and make the mutual-passivity guard "
            "unreachable. Fixing before hand-off.\n")

    log("[Systems Engineer] re-fed Q8 and T-FAME-01; correcting both.")
    log(tools.write_module_impl_fn("fame", tools.read_reference("Economy.good.cpp")))
    f2 = tools.run_row_gate_fn("fame")
    log("[Systems Engineer · self-test] " + f2["summary"])
    for line in f2["log"].splitlines():
        log("    " + line)
    if not f2["passed"]:
        log("[stop] row 4 pass 2 did not pass — see the failures above.")
        return {"status": "error", "gate_passed": False,
                "failures_caught": f1.get("failures", [])}

    # --- row 5: the critical path's sole next link, and the first row to own a turn
    log("\n[Director -> Systems Engineer] spec/turn_spec.md handed over (row 5 — Turn "
        "loop & win/tiebreak). Rows 3 and 4 DECLINED the turn — row 4 takes the turn "
        "number as an argument — so every deferred turn-ownership question is "
        "concentrated here. Four invariants encode §2.8's tiebreak apparatus exactly.")
    log(tools.write_module_impl_fn("turn", tools.read_reference("Turn.buggy.cpp")))
    log("[Systems Engineer] pass 1 authored — the cap tiebreak is a plain lexicographic "
        "comparison (both sides on zero simply ties at key 1 and falls through to "
        "objectives held), and the result tier grades by the size of the winning "
        "margin, the way nearly every strategy game reports a win.\n")

    t1 = tools.run_row_gate_fn("turn")
    log("[Systems Engineer · self-test] " + t1["summary"])
    for line in t1["log"].splitlines():
        log("    " + line)
    if t1["passed"]:
        log("[note] pass 1 unexpectedly passed — the bundled 'buggy' turn loop should "
            "fail T-TURN-05 and T-TURN-07; continuing anyway.\n")
    else:
        log(f"\n[Systems Engineer · self-test] BLOCK — {', '.join(t1['failures'])} caught it. "
            "§2.8 puts a mutual-passivity guard BEFORE the comparison precisely because "
            "a fall-through re-crowns the turtle §1.5 #1 closed, and T-TURN-06 fails "
            "downstream of the same omission; and §2.8 makes the tiers categorical so a "
            "capped grind's tally can never outrank a flag kill (§1.5 #5). Fixing "
            "before hand-off.\n")

    log("[Systems Engineer] re-fed §2.8's procedure — one guard, one three-key "
        "comparison, one grade; restoring the guard and making the tier categorical.")
    log(tools.write_module_impl_fn("turn", tools.read_reference("Turn.good.cpp")))
    t2 = tools.run_row_gate_fn("turn")
    log("[Systems Engineer · self-test] " + t2["summary"])
    for line in t2["log"].splitlines():
        log("    " + line)
    if not t2["passed"]:
        log("[stop] row 5 pass 2 did not pass — see the failures above.")
        return {"status": "error", "gate_passed": False,
                "failures_caught": t1.get("failures", [])}

    # --- row 6: the shipping opponent -------------------------------------------
    # The driver is placed FIRST here, not gated here: row 6's own gate drives the AI
    # through it, so `execute` must exist before the row-6 suite can run. The driver's
    # gate still runs below, on the same file.
    log(tools.write_module_impl_fn("driver", tools.read_reference("Driver.good.cpp")))
    log("[Director] Driver placed as a prerequisite of row 6's gate — the AI's commands "
        "are validated by the same 'execute' a typed command goes through, so T-AI-01 "
        "is structural rather than asserted.\n")

    log("[Director -> Systems Engineer] spec/ai_spec.md handed over (row 6 — Opponent "
        "AI). This is the SHIPPING opponent: §2.9's difficulty is a starting-Fame "
        "handicap, so this one routine is what every tier plays against. It decides "
        "and applies nothing — one ordinary command at a time, through the player's path.")
    log(tools.write_module_impl_fn("ai", tools.read_reference("Ai.buggy.cpp")))
    log("[Systems Engineer] pass 1 authored — the losing-attack guard reads as 'do not "
        "attack if the counter kills you', and build ties break by the order §2.4's "
        "table prints its units.\n")

    a1 = tools.run_row_gate_fn("ai")
    log("[Systems Engineer · self-test] " + a1["summary"])
    for line in a1["log"].splitlines():
        log("    " + line)
    if a1["passed"]:
        log("[note] pass 1 unexpectedly passed — the bundled 'buggy' AI should fail "
            "T-AI-05 and T-AI-06; continuing anyway.\n")
    else:
        log(f"\n[Systems Engineer · self-test] BLOCK — {', '.join(a1['failures'])} caught it. "
            "§2.9 joins TWO conditions — the unit dies AND the exchange trades down — so "
            "an over-cautious guard refuses every sacrifice; and Q9 ruled the build "
            "priority is ascending §2.4 COST, which §4.7 warns in as many words is NOT "
            "the order §2.4's table prints. Fixing before hand-off.\n")

    log("[Systems Engineer] re-fed §2.9's guard and Q9's priority; restoring the second "
        "half of the guard and ordering builds by cost.")
    log(tools.write_module_impl_fn("ai", tools.read_reference("Ai.good.cpp")))
    a2 = tools.run_row_gate_fn("ai")
    log("[Systems Engineer · self-test] " + a2["summary"])
    for line in a2["log"].splitlines():
        log("    " + line)
    if not a2["passed"]:
        log("[stop] row 6 pass 2 did not pass — see the failures above.")
        return {"status": "error", "gate_passed": False,
                "failures_caught": a1.get("failures", [])}

    # --- the debug-command driver: week 1's OTHER promise ------------------------
    log("\n[Director -> Systems Engineer] spec/driver_spec.md handed over. §4.4 week 1 "
        "promises rows 1-3 AND 'Playable via debug commands'; the rows are green and "
        "the second half has no artifact yet.")
    log(tools.write_module_impl_fn("driver", tools.read_reference("Driver.good.cpp")))
    log("[Systems Engineer] authored — the driver contains NO RULES: reach/path/move "
        "delegate to Move.h, damage and counters to Combat.h, stats to Data.h, "
        "distance and adjacency to Hex.h, capture/income/build to Economy.h, and now "
        "alternation, act flags, start-of-turn repair and the §2.8 result to Turn.h, "
        "and the opponent's decisions to Ai.h. Where rows 7-8 would be needed — the "
        "scenario file — it refuses rather than deciding.")
    d = tools.run_row_gate_fn("driver")
    log("[Systems Engineer · self-test] " + d["summary"])
    for line in d["log"].splitlines():
        log("    " + line)
    if not d["passed"]:
        log("[stop] the driver gate did not pass — see the failures above.")
        return {"status": "error", "gate_passed": False,
                "failures_caught": m1.get("failures", [])}

    b = tools.build_driver_fn()
    log("[Systems Engineer] " + b["summary"])
    if not b["built"]:
        log(b["log"])
        return {"status": "error", "gate_passed": False,
                "failures_caught": m1.get("failures", [])}

    # --- Test Engineer: the sole release authority ------------------------------
    cert = tools.certify_week1_fn()
    log("\n[Test Engineer] certify_week1 -> " + cert["summary"] +
        f" | accepted={cert['accepted']}")
    log("[Test Engineer] acceptance record written to build/" + tools.WEEK1_ACCEPT + ".")
    for line in cert["record"]["not_covered"]:
        log("[Test Engineer] NOT covered by this record: " + line)
    log("[Test Engineer] Q29 refuses a ledger flip on a partial acceptance set, so "
        "row 2 stays pending until the in-editor T-DATA-05 pass runs. Rows 1, 3, 4, "
        "5 and 6 have no in-editor half and are complete at this commit.")

    return {"status": "ok", "gate_passed": cert["accepted"],
            "failures_caught": m1.get("failures", [])}
