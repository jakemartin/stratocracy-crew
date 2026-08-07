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
    """The same spec -> gate -> certify flow for GDD §4.11 rows 1-8 (§4.4 week 1,
    plus rows 4, 5, 6, 7 and 8, which week 1 does not owe: rows 4-6 and 8 are the
    critical path, and row 7 is not on it but row 8 queues behind it).

    Rows 1 and 2 are authored and gated first because row 3 depends on both (§4.11's
    Depends-on column). Row 3 then repeats the Combat demonstration with the movement
    hallucination: pass 1 highlights every hex within `hexDistance <= move`, which
    looks right and ignores terrain entirely — the failure §2.5 names in advance when
    it promises "the real move set, not an estimate".
    """
    log("\n" + "=" * 78)
    log("WEEK 1 — GDD §4.11 rows 1-8 + the debug-command driver")
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
    log(tools.write_module_impl_fn("scenario", tools.read_reference("Scenario.good.cpp")))
    log(tools.write_module_impl_fn("ui", tools.read_reference("Ui.good.cpp")))
    log(tools.write_module_impl_fn("driver", tools.read_reference("Driver.good.cpp")))
    log("[Director] Driver placed as a prerequisite of row 6's gate — the AI's commands "
        "are validated by the same 'execute' a typed command goes through, so T-AI-01 "
        "is structural rather than asserted. The driver reaches the scenario module, so "
        "row 7's implementation is placed with it; row 7's OWN gate runs below, on the "
        "same file, and re-authors it twice. Since row 8 landed the driver also renders "
        "the view model, so row 8's implementation is placed here too, on the same "
        "terms — its OWN gate runs below and re-authors it twice.\n")

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

    # --- row 7: not on the critical path, but row 8 queues behind it -------------
    log("\n[Director -> Systems Engineer] spec/scenario_spec.md handed over (row 7 — "
        "Scenario file & validator). It carries a SCOPE RULING: the two stretch maps are "
        "not authored as scenario files, not even as validator fixtures, so four of "
        "§4.7 Stub 7's fixtures have nothing to run against. The consequence is stated, "
        "not hidden — row 7 records a PARTIAL PASS and its ledger row does not flip.")
    log(tools.write_module_impl_fn("scenario", tools.read_reference("Scenario.buggy.cpp")))
    log("[Systems Engineer] pass 1 authored — T-SCN-11's opposing route is minimised "
        "over the opposing seat's guidedOpening.infantry alone, the same NAMED-hex "
        "quantifier T-SCN-06 insists on.\n")

    s1 = tools.run_row_gate_fn("scenario")
    log("[Systems Engineer · self-test] " + s1["summary"])
    for line in s1["log"].splitlines():
        log("    " + line)
    if s1["passed"]:
        log("[note] pass 1 unexpectedly passed — the bundled 'buggy' scenario module "
            "should fail T-SCN-11; continuing anyway.\n")
    else:
        log(f"\n[Systems Engineer · self-test] BLOCK — {', '.join(s1['failures'])} caught it. "
            "Q28 ruled the opposing route ranges over EVERY CanCapture-row unit that seat "
            "deploys, because the property guarded is a RACE and a race does not care "
            "which Infantry wins it. Fixture (b) — the shipped map's own pre-fix "
            "deployment — exists to catch exactly this reading: under it (b) passes at "
            "5 against 6 instead of failing at 5 against 5. Fixing before hand-off.\n")

    log("[Systems Engineer] re-fed Q28; minimising over the opposing seat's whole "
        "capturing force instead of over its marked unit.")
    log(tools.write_module_impl_fn("scenario", tools.read_reference("Scenario.good.cpp")))
    s2 = tools.run_row_gate_fn("scenario")
    log("[Systems Engineer · self-test] " + s2["summary"])
    for line in s2["log"].splitlines():
        log("    " + line)
    if not s2["passed"]:
        log("[stop] row 7 pass 2 did not pass — see the failures above.")
        return {"status": "error", "gate_passed": False,
                "failures_caught": s1.get("failures", [])}

    # --- row 8: on the critical path, and a partial pass like row 7 --------------
    log("\n[Director -> Systems Engineer] spec/ui_spec.md handed over (row 8 — UI "
        "binding contract). It owns how a widget is FED, not what a widget looks like, "
        "which is §2.11's lane. Like row 7 it records a PARTIAL PASS: T-UI-03 and "
        "T-UI-04 are in-editor Unreal Automation, marked † in §4.11, and no editor pass "
        "exists — so the ledger row does not flip and the runner names both by name.")
    log(tools.write_module_impl_fn("ui", tools.read_reference("Ui.buggy.cpp")))
    log("[Systems Engineer] pass 1 authored — five readings the spec names in advance: "
        "the highlight recomputed as a hex-distance filter; partial credit toward "
        "objectivesHeld for a capture in progress; incomePerTurn read from "
        "accrueIncome, which pays 0 on turn 1; isGuidedMarked keyed on the unit's "
        "current hex; and spawnBlocked set equal to buildWaiting.\n")

    u1 = tools.run_row_gate_fn("ui")
    log("[Systems Engineer · self-test] " + u1["summary"])
    for line in u1["log"].splitlines():
        log("    " + line)
    if u1["passed"]:
        log("[note] pass 1 unexpectedly passed — the bundled 'buggy' UI module should "
            "fail T-UI-02, T-UI-05 and GATE-CAP-PARTIAL; continuing anyway.\n")
    else:
        log(f"\n[Systems Engineer · self-test] BLOCK — {', '.join(sorted(set(u1['failures'])))} "
            "caught it. Q14 refuses partial credit — a capture in progress counts for "
            "nobody until the objective flips; Q8(a) pays no income on turn 1 while "
            "ruling G makes incomePerTurn the STANDING rate, so the two differ exactly "
            "where a wrong read is invisible; isGuidedMarked is a property of the "
            "placement, not of where the unit stands now; and buildWaiting is the "
            "queued-slot fact, which cannot express a boxed-in factory with nothing "
            "queued. Fixing before hand-off.\n")

    log("[Systems Engineer] re-fed Q14, Q8(a) with ruling G, and rulings E and J; "
        "recomputing each DECLARED DERIVED field from the stub's words.")
    log(tools.write_module_impl_fn("ui", tools.read_reference("Ui.good.cpp")))
    u2 = tools.run_row_gate_fn("ui")
    log("[Systems Engineer · self-test] " + u2["summary"])
    for line in u2["log"].splitlines():
        log("    " + line)
    if not u2["passed"]:
        log("[stop] row 8 pass 2 did not pass — see the failures above.")
        return {"status": "error", "gate_passed": False,
                "failures_caught": u1.get("failures", [])}

    # --- row 10 part (a): the save format's header machinery ---------------------
    log("\n[Director -> Systems Engineer] spec/save_spec.md handed over (row 10 — Save "
        "& replay format). §4.11 splits the row into three parts with three dependency "
        "sets and THIS IS PART (a) ALONE: the format spec plus the header/version "
        "machinery, which has no dependencies at all and on which T-SAVE-04 closes by "
        "itself, 'since it never applies a command'. No command is applied here and "
        "§4.10's canonical state hash is NOT defined here — that is part (b), and the "
        "stateHash in Driver.h is the driver's own debug digest (GATE-DRV-06), a "
        "different thing. Six of the row's seven IDs do not run and the runner names "
        "each with its reason. Row 10 is a PROPOSED ledger row and has none to flip.")
    log(tools.write_module_impl_fn("save", tools.read_reference("Save.buggy.cpp")))
    log("[Systems Engineer] pass 1 authored — three defects the spec names in advance: "
        "loadSave parses into the CALLER'S object and validates afterwards; checkHeader "
        "compares only formatVersion and rulesCommit, two of the four fields §4.10's "
        "Version policy enumerates; and an unknown key is tolerated instead of "
        "refused.\n")

    s1 = tools.run_row_gate_fn("save")
    log("[Systems Engineer · self-test] " + s1["summary"])
    for line in s1["log"].splitlines():
        log("    " + line)
    if s1["passed"]:
        log("[note] pass 1 unexpectedly passed — the bundled 'buggy' save module should "
            "fail T-SAVE-04 and GATE-SAVE-PARSE; continuing anyway.\n")
    else:
        log(f"\n[Systems Engineer · self-test] BLOCK — {', '.join(sorted(set(s1['failures'])))} "
            "caught it. T-SAVE-04 states THREE things and the pass-1 module satisfies "
            "only the first: refused, refused WITH A REASON, and the caller's state "
            "UNTOUCHED. Filling the caller's object before validating is the defect the "
            "'state untouched' clause exists for, and it is invisible to any fixture "
            "that only checks the return value. Fixing before hand-off.\n")

    log("[Systems Engineer] re-fed §4.10's Version policy: the refusal set is the four "
        "fields it enumerates, the parse fills a local and assigns once on success, and "
        "an unknown key is a refusal because within one formatVersion it is a typo.")
    log(tools.write_module_impl_fn("save", tools.read_reference("Save.good.cpp")))
    s2 = tools.run_row_gate_fn("save")
    log("[Systems Engineer · self-test] " + s2["summary"])
    for line in s2["log"].splitlines():
        log("    " + line)
    if not s2["passed"]:
        log("[stop] row 10 part (a) pass 2 did not pass — see the failures above.")
        return {"status": "error", "gate_passed": False,
                "failures_caught": s1.get("failures", [])}

    # --- row 10 part (b): the headless replayer and §4.10's canonical state hash --
    log("\n[Director -> Systems Engineer] spec/replay_spec.md handed over (row 10 part "
        "(b) — headless replayer + canonical state hash). Part (b) RUNS "
        "T-SAVE-01/02/03/05/06 and CLOSES four of them: T-SAVE-06 is marked † in "
        "§4.11, asserted jointly with T-INT-02, and no in-editor Automation harness "
        "exists. §4.11 put closure in part (c) because week 2's log carried only "
        "{Move, Attack}; rows 4, 5 and 6 have all since landed, so the log here is the "
        "COMPLETE §4.9 command set and a segment of it is generated by row 6's AI, "
        "which is what puts T-AI-06 inside T-SAVE-02's composition. This is a separate "
        "registry row from `save` so part (a)'s empty link set stays a checked claim. "
        "Row 10 is a PROPOSED ledger row and still has none to flip.")
    log(tools.write_module_impl_fn("replay", tools.read_reference("Replay.buggy.cpp")))
    log("[Systems Engineer] pass 1 authored — three defects, each a mechanical edit of "
        "the good module: replayLog applies to the caller's state IN PLACE rather than "
        "to a copy; the canonical hash walks units in STORAGE order rather than "
        "canonical hex order; and it omits the two per-unit turn flags.\n")

    p1 = tools.run_row_gate_fn("replay")
    log("[Systems Engineer · self-test] " + p1["summary"])
    for line in p1["log"].splitlines():
        log("    " + line)
    if p1["passed"]:
        log("[note] pass 1 unexpectedly passed — the bundled 'buggy' replay module "
            "should fail T-SAVE-05 and three GATE-REPLAY-* checks; continuing.\n")
    else:
        log(f"\n[Systems Engineer · self-test] BLOCK — {', '.join(sorted(set(p1['failures'])))} "
            "caught it. The shape worth naming: T-SAVE-01, T-SAVE-02 and T-SAVE-03 all "
            "PASS against this module, because every clause they carry compares two "
            "runs that share the defect on both sides. Only the checks that compare "
            "against an INDEPENDENTLY re-derived serialisation, or against the state as "
            "it stood before the load, can see it. Fixing before hand-off.\n")

    log("[Systems Engineer] re-fed §4.10: every collection walks canonical hex order "
        "with ties broken by id, the two turn flags are hashed because a save is "
        "accepted mid-turn, and the replay applies to a copy so an illegal command at "
        "index k leaves the caller's state byte-identical.")
    log(tools.write_module_impl_fn("replay", tools.read_reference("Replay.good.cpp")))
    p2 = tools.run_row_gate_fn("replay")
    log("[Systems Engineer · self-test] " + p2["summary"])
    for line in p2["log"].splitlines():
        log("    " + line)
    if not p2["passed"]:
        log("[stop] row 10 part (b) pass 2 did not pass — see the failures above.")
        return {"status": "error", "gate_passed": False,
                "failures_caught": p1.get("failures", [])}

    # --- row 10 part (c): the Balance Analyst's self-play log producer ------------
    log("\n[Director -> Systems Engineer] spec/balance_spec.md handed over (row 10 "
        "part (c) — the self-play log producer). T-SAVE-07 asserts that a Balance "
        "Analyst self-play log validates and replays as a save file, one format, no "
        "dialect drift — and this repo had NO PRODUCER of such a log at any scope: "
        "cpp_reference/selfplay.cpp is a combat-only 1v1 duel harness over Combat.h "
        "that prints a table and opens no file. A THIRD registry row for one ledger "
        "row, because §4.11 gives part (c) its own dependency set — rows 4, 5 and 6, "
        "the command set, the match that runs to a result, and the AI that plays it — "
        "and folding it into `replay` would put row 6 inside part (b)'s claim. Row 10 "
        "is a PROPOSED ledger row and still has none to flip.")
    log(tools.write_module_impl_fn("balance",
                                   tools.read_reference("Balance.buggy.cpp")))
    log("[Systems Engineer] pass 1 authored — three defects, each a mechanical edit of "
        "the good module: Attack is tagged by the ACTING unit's hex rather than the "
        "TARGET's; Build's `unitId` carries the acting unit rather than the unit BUILT; "
        "and a command is logged when PROPOSED rather than when ACCEPTED.\n")

    c1 = tools.run_row_gate_fn("balance")
    log("[Systems Engineer · self-test] " + c1["summary"])
    for line in c1["log"].splitlines():
        log("    " + line)
    if c1["passed"]:
        log("[note] pass 1 unexpectedly passed — the bundled 'buggy' self-play module "
            "should fail two translation checks, the run, the command set, the "
            "accepted-only check and T-SAVE-07 clause (b); continuing.\n")
    else:
        log(f"\n[Systems Engineer · self-test] BLOCK — "
            f"{', '.join(sorted(set(c1['failures'])))} caught it. The shape worth "
            "naming: T-SAVE-07's clauses (a) and (c) PASS against this module. The "
            "FORMAT is agnostic to whether the rules accept a command, so a log full "
            "of refused entries still validates and still round-trips byte-identically "
            "— only clause (b), which REPLAYS the log, can see it. A suite that read "
            "'validates' as the whole of T-SAVE-07 would have shipped this.\n")

    log("[Systems Engineer] re-fed §4.9 and Save.h: Attack is spelled by TARGET hex, a "
        "Build entry names the unit BUILT, and only a command the rules accepted enters "
        "the log.")
    log(tools.write_module_impl_fn("balance",
                                   tools.read_reference("Balance.good.cpp")))
    c2 = tools.run_row_gate_fn("balance")
    log("[Systems Engineer · self-test] " + c2["summary"])
    for line in c2["log"].splitlines():
        log("    " + line)
    if not c2["passed"]:
        log("[stop] row 10 part (c) pass 2 did not pass — see the failures above.")
        return {"status": "error", "gate_passed": False,
                "failures_caught": c1.get("failures", [])}

    # --- the debug-command driver: week 1's OTHER promise ------------------------
    log("\n[Director -> Systems Engineer] spec/driver_spec.md handed over. §4.4 week 1 "
        "promises rows 1-3 AND 'Playable via debug commands'; the rows are green and "
        "the second half has no artifact yet.")
    log(tools.write_module_impl_fn("driver", tools.read_reference("Driver.good.cpp")))
    log("[Systems Engineer] authored — the driver contains NO RULES: reach/path/move "
        "delegate to Move.h, damage and counters to Combat.h, stats to Data.h, "
        "distance and adjacency to Hex.h, capture/income/build to Economy.h, and now "
        "alternation, act flags, start-of-turn repair and the §2.8 result to Turn.h, "
        "the opponent's decisions to Ai.h, and the scenario file to Scenario.h. Where "
        "row 8 would be needed — how a widget is fed — it refuses rather than deciding.")
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
        "row 7 stays pending because the Director's scope ruling leaves four of its "
        "fixtures without a map. Row 2's in-editor T-DATA-05 pass HAS since run, "
        "green in the UE project at fed8ae9 over this repo's b1ea992 data bytes, so "
        "its acceptance set is complete. "
        "pending because the Director's scope ruling leaves four of its fixtures "
        "without a map. Rows 1, 3, 4, 5 and 6 have no missing half and are complete at "
        "this commit. Row 10 is a PROPOSED ledger row (§4.11) and has none to flip at "
        "all — a different state from a partial pass, and not one Q29 speaks to.")

    return {"status": "ok", "gate_passed": cert["accepted"],
            "failures_caught": m1.get("failures", [])}
