"""Deterministic tools the crew uses to act on the shared workspace.

These are the real "Act" side of Sense-Think-Act: they touch the filesystem and
invoke a C++ compiler. The agents' LLM reasoning decides *what* C++ to write; these
tools *do* the writing, compiling and running and hand back real results (compiler
errors, test PASS/FAIL, self-play tables). The Test Engineer's gate is therefore a
real `g++` compile + run — not the model's opinion of its own code.

The same functions back both execution paths (see run.py):
  * the live CrewAI crew (agents call the @tool wrappers), and
  * the offline deterministic pipeline (calls the plain functions directly).
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REF = ROOT / "cpp_reference"
BUILD = ROOT / "build"
DATA = ROOT / "data"         # the canonical CSVs (GDD §4.8) — authored once, read twice

HEADER = "Combat.h"
TEST = "test_combat.cpp"
SELFPLAY = "selfplay.cpp"
IMPL = "Combat.cpp"          # the file the Systems Engineer authors
ACCEPT = "acceptance.json"   # the release record — ONLY the Test Engineer writes this

# --------------------------------------------------------------------------- #
# Week 1 — GDD §4.11 rows 1-3, the three rows §4.4 week 1 owes, plus rows 4, 5, 6, 7
# and 8. Rows 4-6 and 8 are on the critical path `1 -> 3 -> 4 -> 5 -> 6/8`; row 7 is
# NOT, and is built here because row 8 queues behind it and its dependencies (rows 1-3)
# all landed.
#
# Same shape as Combat: Director-owned headers and test harnesses are FIXED (copied
# from cpp_reference/), the implementation is authored into build/, and the gate is a
# real compile + run. Each row gets its own runner so a failure is attributable to one
# ledger row rather than to "week 1".
# --------------------------------------------------------------------------- #
WEEK1_FIXED = ("Hex.h", "Data.h", "Move.h", "Economy.h", "Turn.h", "Ai.h", "Scenario.h",
               "Ui.h", "Save.h", "Replay.h", "Balance.h", "Driver.h",
               "test_hex.cpp", "test_data.cpp", "test_move.cpp", "test_economy.cpp",
               "test_turn.cpp", "test_ai.cpp", "test_scenario.cpp", "test_ui.cpp",
               "test_save.cpp", "test_replay.cpp", "test_balance.cpp",
               "test_driver.cpp", "driver_main.cpp")

WEEK1_ROWS = {
    "hex": {
        "row": 1, "system": "Hex grid & math", "spec": "spec/hex_spec.md",
        "impl": "Hex.cpp", "sources": ["Hex.cpp", "Combat.cpp", "test_hex.cpp"],
        "stem": "test_hex_runner", "tests": "T-HEX-01..07",
    },
    "data": {
        "row": 2, "system": "Data tables (units/terrain)", "spec": "spec/data_spec.md",
        "impl": "Data.cpp", "sources": ["Data.cpp", "Combat.cpp", "test_data.cpp"],
        "stem": "test_data_runner", "tests": "T-DATA-01..04, 06",
    },
    "move": {
        "row": 3, "system": "Movement & pathfinding", "spec": "spec/move_spec.md",
        "impl": "Move.cpp",
        "sources": ["Move.cpp", "Hex.cpp", "Data.cpp", "Combat.cpp", "test_move.cpp"],
        "stem": "test_move_runner", "tests": "T-MOVE-01..06",
    },
    "fame": {
        "row": 4, "system": "Capture & Fame economy", "spec": "spec/economy_spec.md",
        "impl": "Economy.cpp",
        "sources": ["Economy.cpp", "Hex.cpp", "Data.cpp", "Combat.cpp", "test_economy.cpp"],
        "stem": "test_economy_runner", "tests": "T-FAME-01..09",
    },
    "turn": {
        "row": 5, "system": "Turn loop & win/tiebreak", "spec": "spec/turn_spec.md",
        "impl": "Turn.cpp",
        "sources": ["Turn.cpp", "Economy.cpp", "Hex.cpp", "Data.cpp", "Combat.cpp",
                    "test_turn.cpp"],
        "stem": "test_turn_runner", "tests": "T-TURN-01..10",
    },
    "ai": {
        "row": 6, "system": "Opponent AI (baseline)", "spec": "spec/ai_spec.md",
        "impl": "Ai.cpp",
        # Driver.cpp is linked here (row 6's gate drives the AI through `execute`), and
        # Driver.h has included Ui.h since row 8 landed — so Ui.cpp links here too.
        "sources": ["Ai.cpp", "Driver.cpp", "Ui.cpp", "Scenario.cpp", "Turn.cpp",
                    "Economy.cpp", "Move.cpp", "Hex.cpp", "Data.cpp", "Combat.cpp",
                    "test_ai.cpp"],
        "stem": "test_ai_runner", "tests": "T-AI-01..06 + GATE-AI-SMOKE",
    },
    "scenario": {
        "row": 7, "system": "Scenario file & validator", "spec": "spec/scenario_spec.md",
        "impl": "Scenario.cpp",
        "sources": ["Scenario.cpp", "Move.cpp", "Hex.cpp", "Data.cpp", "Economy.cpp",
                    "Combat.cpp", "test_scenario.cpp"],
        "stem": "test_scenario_runner",
        # The acceptance set 4.7 Stub 7 writes is T-SCN-01..09 and T-SCN-11. This
        # suite closes a SUBSET of it: the Director's scope ruling authors no scenario
        # file for the two stretch maps, so T-SCN-08 (a)/(b), T-SCN-09's asserting
        # branch and T-SCN-11 (c) have no fixture. The runner names each one.
        "tests": "T-SCN-01..07, 08 (c), 09 refusal, 11 (a)(b) + GATE-SCN-PARSE/HASH",
    },
    "ui": {
        "row": 8, "system": "UI binding contract", "spec": "spec/ui_spec.md",
        "impl": "Ui.cpp",
        # Ui.h includes Scenario.h — isGuidedMarked reads the guidedOpening seat — so
        # the scenario module links here even though row 8 asserts nothing about it.
        "sources": ["Ui.cpp", "Scenario.cpp", "Turn.cpp", "Economy.cpp", "Move.cpp",
                    "Hex.cpp", "Data.cpp", "Combat.cpp", "test_ui.cpp"],
        "stem": "test_ui_runner",
        # The acceptance set §4.7 Stub 8 writes is T-UI-01..05. This suite closes a
        # SUBSET of it: T-UI-03 and T-UI-04 are in-editor Unreal Automation, marked †
        # in §4.11. An in-editor Automation harness now EXISTS (UE fed8ae9) but runs
        # T-DATA-05 only; these two still lack the real Stratocracy widgets they assert
        # over. The runner names both before its tally.
        "tests": "T-UI-01, 02, 05 + GATE-CAP-PARTIAL",
    },
    "save": {
        "row": 10, "system": "Save & replay format, part (a)", "spec": "spec/save_spec.md",
        "impl": "Save.cpp",
        # THE LINK SET IS THE CLAIM. §4.11 says row 10 part (a) has "no deps at all",
        # and this is where that is checked rather than asserted: Hex.cpp for the odd-r
        # conversion and nothing else. No Scenario.cpp (the scenarioHash is COMPARED,
        # never recomputed), no Turn/Economy (no command is applied), no Driver.cpp.
        # Adding a source here without a reason quietly falsifies §4.11's dependency
        # cell for this row.
        "sources": ["Save.cpp", "Hex.cpp", "test_save.cpp"],
        "stem": "test_save_runner",
        # The acceptance set §4.7's Save stub writes is T-SAVE-01..07. This suite closes
        # a SUBSET: T-SAVE-04, which closes on part (a) alone. T-SAVE-01/02/03/05 need
        # the replayer and close in the `replay` row below; T-SAVE-06 is in-editor (†);
        # T-SAVE-07 needs row 6's self-play and closes in the `balance` row (part c).
        # The runner names them.
        "tests": "T-SAVE-04 + GATE-SAVE-PARSE",
    },
    # Row 10 part (b) — the headless replayer and §4.10's canonical state hash. A
    # SEPARATE row from `save` precisely so part (a)'s empty link set above stays a
    # checked claim: widening that row's sources to reach the replayer would have
    # falsified §4.11's "no deps at all" cell silently.
    "replay": {
        "row": 10, "system": "Save & replay, part (b) — replayer + state hash",
        "spec": "spec/replay_spec.md",
        "impl": "Replay.cpp",
        # THE LINK SET IS THE CLAIM, here too. §4.11 gives part (b) rows 1-3 plus row
        # 7's structural half, and closure on rows 4, 5 and 6. Every one of those is a
        # source below: Move/Hex/Data (1-3), Economy/Turn (4-5), Ai (6, whose commands
        # enter the log so T-AI-06 composes into T-SAVE-02), Save (the format part (a)
        # defined).
        #
        # Scenario.cpp IS here, and it was not before. The replayer itself still takes
        # the board as state and still compares scenarioHash as an opaque string --
        # nothing about part (b)'s dependency cell changed. What changed is the SUITE:
        # GATE-REPLAY-FIXTURE builds data/parity_fixture.save from the shipped scenario,
        # so the harness calls loadScenario and scenarioHash, both of which live in
        # Scenario.cpp. Linking it for the fixture is not the same as the replayer
        # depending on it, and §4.11's cell for this row is unchanged by this line.
        "sources": ["Replay.cpp", "Save.cpp", "Hex.cpp", "Data.cpp", "Move.cpp",
                    "Economy.cpp", "Turn.cpp", "Combat.cpp", "Ai.cpp", "Scenario.cpp",
                    "test_replay.cpp"],
        "stem": "test_replay_runner",
        # Four of the five IDs part (b) RUNS also close here. T-SAVE-06 does not: it is
        # marked † in §4.11 and is asserted JOINTLY with T-INT-02, which replays
        # in-engine. This suite supplies that joint assertion's headless half -- the
        # committed fixture and the canonical state hash it carries -- and keeps them
        # fresh via GATE-REPLAY-FIXTURE; it does not run the ID. GATE-REPLAY-* mint no
        # acceptance ID, on the GATE-SAVE-PARSE precedent.
        "tests": "T-SAVE-01, 02, 03, 05 + GATE-REPLAY-*",
    },
    "balance": {
        "row": 10, "system": "Save & replay, part (c) — the self-play log producer",
        "spec": "spec/balance_spec.md",
        "impl": "Balance.cpp",
        # THE LINK SET IS THE CLAIM, a third time. §4.11 puts part (c)'s closure on rows
        # 4, 5 and 6 — the command set, the match that runs to a result, and the AI that
        # plays it — so `Ai.cpp`, `Economy.cpp` and `Turn.cpp` are load-bearing here in a
        # way they are not in part (a)'s set. `Replay.cpp` is part (b), which this part
        # consumes and does not re-implement. `Scenario.cpp` is absent for part (b)'s own
        # reason: the board arrives as state and `scenarioHash` stays an opaque string.
        "sources": ["Balance.cpp", "Replay.cpp", "Save.cpp", "Hex.cpp", "Data.cpp",
                    "Move.cpp", "Economy.cpp", "Turn.cpp", "Combat.cpp", "Ai.cpp",
                    "test_balance.cpp"],
        "stem": "test_balance_runner",
        # ONE acceptance ID closes here — T-SAVE-07, in three clauses. T-SAVE-06 does
        # not, and the reason is now that this suite is HEADLESS rather than that
        # anything is missing: it is row 10's only †, and is asserted jointly with
        # T-INT-02. Nothing headless
        # can close them, which is what † and "jointly" meant all along.
        # GATE-BALANCE-* mint no acceptance ID, on the GATE-SAVE-PARSE and
        # GATE-REPLAY-* precedent.
        "tests": "T-SAVE-07 + GATE-BALANCE-*",
    },
    # Not a §4.7 stub and not a ledger row: the debug-command driver builds no rules
    # system, so its checks are named GATE-DRV-* rather than T-* and move no count in
    # the GDD. It closes §4.4 week 1's OTHER promise, "Playable via debug commands".
    "driver": {
        "row": None, "system": "Debug-command driver", "spec": "spec/driver_spec.md",
        "impl": "Driver.cpp",
        "sources": ["Driver.cpp", "Ai.cpp", "Ui.cpp", "Scenario.cpp", "Move.cpp",
                    "Hex.cpp", "Data.cpp", "Economy.cpp", "Turn.cpp", "Combat.cpp",
                    "test_driver.cpp"],
        "stem": "test_driver_runner", "tests": "GATE-DRV-01..12",
    },
}
# §4.11 dependency order; the driver is last because it delegates to every row above it.
# `save` is row 10 part (a) and depends on nothing, so its position is free — it sits in
# ledger-row order, after row 8 and still ahead of the driver.
WEEK1_ORDER = ("hex", "data", "move", "fame", "turn", "ai", "scenario", "ui", "save",
               "replay", "balance", "driver")
WEEK1_ACCEPT = "acceptance_week1.json"  # the week-1 release record — Test Engineer only

# The playable artifact itself — built from the same sources plus the REPL entry point.
DRIVER_BINARY = "stratocracy_debug"
DRIVER_SOURCES = ["Driver.cpp", "Ai.cpp", "Ui.cpp", "Scenario.cpp", "Move.cpp",
                  "Hex.cpp", "Data.cpp", "Economy.cpp", "Turn.cpp", "Combat.cpp",
                  "driver_main.cpp"]


# --------------------------------------------------------------------------- #
# workspace + compiler plumbing
# --------------------------------------------------------------------------- #
def ensure_workspace() -> Path:
    """Create build/ and copy the fixed (non-authored) sources into it.

    Fixed = Director-owned: the headers that declare the contract and the test
    harnesses that assert it. Implementations are NOT copied here — they are authored
    into build/ by write_*_impl_fn, which is what makes the gate a real check.
    """
    BUILD.mkdir(exist_ok=True)
    for f in (HEADER, TEST, SELFPLAY, *WEEK1_FIXED):
        shutil.copyfile(REF / f, BUILD / f)
    return BUILD


def find_compiler() -> str | None:
    # GCC/Clang first; MSVC cl.exe last (only on PATH inside a VS Developer prompt).
    for cc in ("g++", "clang++", "c++", "cl"):
        if shutil.which(cc):
            return cc
    return None


def _is_msvc(cc: str) -> bool:
    # Match MSVC cl.exe EXACTLY — not "clang++", which also starts with "cl".
    name = os.path.basename(cc).lower()
    if name.endswith(".exe"):
        name = name[:-4]
    return name == "cl"


def _compile(sources: list[str], out_stem: str) -> tuple[bool, str, str | None]:
    """Compile `sources` (basenames inside build/) into an executable.

    Supports GCC/Clang (`-std=c++17 -O2 -o`) and MSVC (`cl /std:c++17 /EHsc /O2 /Fe:`).
    Runs with cwd=build/ so object files and the binary stay contained. Returns
    (ok, log, exe_path); exe_path is None on failure.
    """
    cc = find_compiler()
    if cc is None:
        return (False,
                "no C++ compiler found. Install g++/clang++, or use MSVC: open the "
                "'x64 Native Tools Command Prompt for VS' (which puts cl.exe on PATH) "
                "and run from there.",
                None)
    exe = out_stem + (".exe" if os.name == "nt" else "")
    if _is_msvc(cc):
        cmd = [cc, "/nologo", "/std:c++17", "/EHsc", "/O2", *sources, f"/Fe:{exe}"]
    else:
        cmd = [cc, "-std=c++17", "-O2", *sources, "-o", exe]
    p = subprocess.run(cmd, capture_output=True, text=True,
                       encoding="utf-8", errors="replace", cwd=str(BUILD))
    ok = p.returncode == 0
    # MSVC prints diagnostics to stdout; GCC/Clang to stderr.
    log = ((p.stdout + p.stderr) if _is_msvc(cc) else (p.stderr or p.stdout)).strip()
    return ok, log, (str(BUILD / exe) if ok else None)


# --------------------------------------------------------------------------- #
# plain functions (used by the offline pipeline and by the @tool wrappers)
# --------------------------------------------------------------------------- #
def write_combat_impl_fn(cpp_source: str) -> str:
    ensure_workspace()
    (BUILD / IMPL).write_text(cpp_source, encoding="utf-8")
    return f"[Systems Engineer] wrote {len(cpp_source)} bytes to build/{IMPL}"


def run_test_gate_fn() -> dict:
    """Compile Combat.cpp + the test harness, run it, parse PASS/FAIL. THE gate."""
    ensure_workspace()
    if not (BUILD / IMPL).exists():
        return {"compiled": False, "passed": False, "summary":
                "no build/Combat.cpp — Systems Engineer must author it first",
                "failures": [], "log": ""}
    ok, log, exe = _compile([IMPL, TEST], "test_runner")
    if not ok:
        return {"compiled": False, "passed": False,
                "summary": "compile FAILED", "failures": [], "log": log}
    p = subprocess.run([exe], capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    out = p.stdout
    failures = [ln.split()[1] for ln in out.splitlines()
                if ln.startswith("FAIL")]
    passed = p.returncode == 0 and not failures
    tally = next((ln for ln in out.splitlines() if "passed" in ln), "").strip()
    summary = ("GATE PASS — all invariants hold" if passed
               else f"GATE BLOCK — failing: {', '.join(failures) or 'compile/run error'}")
    return {"compiled": True, "passed": passed, "summary": f"{summary} ({tally})",
            "failures": failures, "log": out.strip()}


def certify_build_fn() -> dict:
    """Test Engineer's authoritative release gate: run every invariant AND write the
    acceptance record. This is the ONLY writer of build/acceptance.json, which the Balance
    Analyst requires — so removing the Test Engineer halts the pipeline (no record -> no
    balance). The Systems Engineer's own run_test_gate is dev-time self-testing; it never
    certifies for release."""
    r = run_test_gate_fn()
    record = {
        "accepted": bool(r["passed"]),
        "tests": "T-COMBAT-01..10, T-REPAIR-01..07",
        "failures": r.get("failures", []),
        "summary": r["summary"],
        "certified_by": "Test Engineer",
    }
    ensure_workspace()
    (BUILD / ACCEPT).write_text(json.dumps(record, indent=2), encoding="utf-8")
    return {**r, "accepted": record["accepted"]}


# --------------------------------------------------------------------------- #
# Week 1 — §4.11 rows 1-3
# --------------------------------------------------------------------------- #
def write_module_impl_fn(row_key: str, cpp_source: str) -> str:
    """Author one week-1 module into build/. Mirrors write_combat_impl_fn."""
    if row_key not in WEEK1_ROWS:
        return f"[Systems Engineer] unknown module '{row_key}'"
    ensure_workspace()
    spec = WEEK1_ROWS[row_key]
    (BUILD / spec["impl"]).write_text(cpp_source, encoding="utf-8")
    return (f"[Systems Engineer] wrote {len(cpp_source)} bytes to build/{spec['impl']} "
            f"(row {spec['row']} — {spec['system']})")


def run_row_gate_fn(row_key: str) -> dict:
    """Compile one week-1 module + its harness, run it, parse PASS/FAIL.

    The runner is executed with cwd=build/ and handed the absolute path to data/, so
    the CSV-backed suites read the canonical tables (§4.8) wherever the gate is invoked
    from, and any scratch files a suite writes stay inside build/.
    """
    ensure_workspace()
    spec = WEEK1_ROWS[row_key]
    label = f"row {spec['row']}" if spec["row"] is not None else "no row"
    missing = [s for s in spec["sources"] if not (BUILD / s).exists()]
    if missing:
        return {"row": spec["row"], "system": spec["system"], "tests": spec["tests"],
                "compiled": False, "passed": False, "failures": [], "log": "",
                "summary": f"{label} ({spec['system']}) missing source(s): "
                           f"{', '.join(missing)} — the Systems Engineer must author "
                           "the implementation first"}
    ok, log, exe = _compile(spec["sources"], spec["stem"])
    if not ok:
        return {"row": spec["row"], "system": spec["system"], "tests": spec["tests"],
                "compiled": False, "passed": False, "failures": [], "log": log,
                "summary": f"{label} ({spec['system']}) compile FAILED"}
    p = subprocess.run([exe, str(DATA)], capture_output=True, text=True,
                       encoding="utf-8", errors="replace", cwd=str(BUILD))
    out = p.stdout
    failures = [ln.split()[1] for ln in out.splitlines() if ln.startswith("FAIL")]
    passed = p.returncode == 0 and not failures
    tally = next((ln for ln in out.splitlines() if "passed" in ln), "").strip()
    summary = (f"{label} ({spec['system']}) GATE PASS — {spec['tests']}"
               if passed else
               f"{label} ({spec['system']}) GATE BLOCK — failing: "
               f"{', '.join(failures) or 'compile/run error'}")
    return {"row": spec["row"], "system": spec["system"], "tests": spec["tests"],
            "compiled": True, "passed": passed, "failures": failures,
            "summary": f"{summary} ({tally})", "log": out.strip()}


def run_week1_gate_fn() -> dict:
    """Every week-1 row, in §4.11 dependency order. Dev-time self-testing."""
    rows = [run_row_gate_fn(k) for k in WEEK1_ORDER]
    passed = all(r["passed"] for r in rows)
    return {"rows": rows, "passed": passed,
            "summary": ("WEEK-1 GATE PASS — rows 1-3 (T-HEX-01..07, T-DATA-01..04+06, "
                        "T-MOVE-01..06) + row 4 (T-FAME-01..09) + row 5 "
                        "(T-TURN-01..10) + row 6 (T-AI-01..06 + GATE-AI-SMOKE) + "
                        "row 7's SUBSET of T-SCN and row 8's SUBSET of T-UI + row 10's "
                        "parts (a), (b) and (c) (T-SAVE-01, 02, 03, 04, 05, 07 + "
                        "GATE-SAVE-PARSE + GATE-REPLAY-* + GATE-BALANCE-*) (see each "
                        "row's not-covered list) + the debug driver "
                        "(GATE-DRV-01..12)" if passed else
                        "WEEK-1 GATE BLOCK — " + "; ".join(
                            f"{r['system']}: {', '.join(r['failures']) or 'compile/run error'}"
                            for r in rows if not r["passed"]))}


def build_driver_fn() -> dict:
    """Compile the playable artifact itself — the debug-command REPL.

    §4.4 week 1 promises two things: the rows, and "Playable via debug commands".
    The gate above proves the driver decides nothing on its own; this produces the
    binary a human actually types into, so the promise has an artifact behind it
    rather than a passing test about one.
    """
    ensure_workspace()
    missing = [s for s in DRIVER_SOURCES if not (BUILD / s).exists()]
    if missing:
        return {"built": False, "summary": f"missing source(s): {', '.join(missing)}",
                "log": "", "exe": None}
    ok, log, exe = _compile(DRIVER_SOURCES, DRIVER_BINARY)
    if not ok:
        return {"built": False, "summary": "driver compile FAILED", "log": log, "exe": None}
    return {"built": True, "exe": exe, "log": "",
            "summary": f"playable artifact built: build/{Path(exe).name} "
                       f"(run it, then type 'help')"}


def certify_week1_fn() -> dict:
    """Test Engineer's release gate for §4.11 rows 1-8 — runs every invariant AND
    writes build/acceptance_week1.json. The ONLY writer of that record.

    The record states what it does NOT cover as well as what it does: T-DATA-05 is the
    in-editor Unreal Automation half of row 2, marked † in §4.11, and no headless run
    can assert it. It is no longer WAITING, though: it is green in the UE project's
    Automation suite at fed8ae9, run against the data bytes of this repo's b1ea992 and
    tied to them by sha256. Row 2's acceptance set is therefore complete, and "not
    covered here" is a statement about this gate's reach, not about the ID.

    Row 7 is in the same posture for a different reason. The Director's scope ruling
    authors no scenario file for the two stretch maps, so four of §4.7 Stub 7's
    fixtures have nothing to run against; the row records a partial pass and stays
    pending. Row 8 held row 2's posture and now holds it alone: T-UI-03 and T-UI-04 are
    in-editor Automation over widget bindings, marked † in §4.11, and while the harness
    now exists, the real Stratocracy widgets they assert over do not.
    Every list is in `not_covered` by name and with a reason.
    """
    r = run_week1_gate_fn()
    record = {
        "accepted": bool(r["passed"]),
        "scope": "GDD §4.11 rows 1-8 (§4.4 week 1's rows 1-3, plus rows 4-8 early) and "
                 "row 10 PART (a) only. Rows 7, 8 and 10 each close a SUBSET of their "
                 "acceptance set. Row 2's headless half is green here and T-DATA-05 is "
                 "green in-editor at UE fed8ae9, so that row's set is complete; see "
                 "not_covered.",
        "rows": [
            {
                "row": row["row"],
                "system": row["system"],
                "tests": row["tests"],
                "passed": row["passed"],
                "failures": row["failures"],
            }
            for row in r["rows"]
        ],
        "not_covered": [
            "T-DATA-05 — in-editor Unreal Automation (DataTable import parity + "
            "EUnitType mirror). §4.11 marks it †; it is not headless and did not run "
            "HERE. It is green in the UE project at fed8ae9, over the data bytes of "
            "this repo's b1ea992, so it is not outstanding — only out of this gate's "
            "reach.",
            "T-MOVE-07 — reserved and unwritten, blocked on the Q2 movement-class "
            "ruling (§4.7 Stub 3).",
            "T-SCN-08 fixtures (a) The Causeway and (b) Longwater March — both need a "
            "stretch map authored as a scenario file, which the Director's scope "
            "ruling refuses (§2.13.7). Not run, and not replaced by a synthetic map.",
            "T-SCN-09's ASSERTING branch — rho asserts hex by hex and the only "
            "scenario file that exists declares `none`, which asserts nothing. Its "
            "REFUSAL branch did run, off the shipped map's own declaration.",
            "T-SCN-11 fixture (c) The Causeway — same scope ruling. Asymmetry (ii) is "
            "exercised on the shipped map instead, which is a weaker witness: no gate "
            "there fails under the Bridge-free reading.",
            "T-SCN-10 — reserved and UNWRITTEN on Q26 (ruled). Nothing is asserted, so "
            "nothing is waiting — a different state from T-MOVE-07, which IS blocked.",
            "T-UI-03 and T-UI-04 — in-editor Unreal Automation over widget bindings. "
            "§4.11 marks both †; they are not headless and did not run. They are now "
            "the WHOLE of what row 8 lacks. The harness they needed now exists (UE "
            "fed8ae9); what they still lack are the real Stratocracy widgets they "
            "assert over. Row 2 no longer shares this posture — its set is complete.",
            "T-SAVE-06 — stateHash stability across the headless and in-engine builds. "
            "§4.11 marks it †, and it is asserted jointly with T-INT-02. The bridge "
            "that landed at UE 0897cb5 replays data/parity_fixture.save in-engine and "
            "compares its own canonical state hash against the one that file carries. "
            "Every "
            "blocker this entry used to name is gone — the in-editor Automation harness "
            "landed at UE fed8ae9, §4.10's canonical state hash was built by part (b), "
            "and the replayer was vendored at f5fdb69, which retired the ruling that "
            "deferred it. It remains the only † of row 10's seven, and it remains "
            "uncloseable by any headless suite in this repo.",
            "Row 10's Balance module is NOT VENDORED into Source/StratRules/. "
            "It is named Balance and not Selfplay, for the reason Balance.h states: "
            "cpp_reference/selfplay.cpp is tracked and the build filesystem is "
            "case-insensitive, so a Selfplay.cpp beside it is the same file. "
            "ue_module/vendored_set.json names it Balance, and that declaration is "
            "what T-INT-01 reads. "
            "§4.9 enumerates the modules the sync script carries and this is not one "
            "of them. The 2026-08-05 ruling recorded here also covered Save and "
            "Replay, and for those two it has been SPENT rather than reversed: it "
            "deferred vendoring until §4.9 part 2 supplied a bridge consumer, that "
            "consumer was built, and both were vendored at f5fdb69 ahead of it. What "
            "the bridge does not consume is Selfplay — it is a headless log producer "
            "that no in-engine code calls — so for that module the ruling still "
            "describes the tree and vendoring would re-date T-INT-01's and T-INT-04's "
            "closures for no consumer.",
            "GATE-DRV-01..12, GATE-SCN-PARSE, GATE-SCN-HASH, GATE-SAVE-PARSE, "
            "GATE-AI-SMOKE and GATE-CAP-PARTIAL gate a tool, two file formats, a smoke "
            "path and a partial-capture reading — not a rules system apiece. They are "
            "not §4.7 stub IDs, flip no §3 ledger row, and are not GDD acceptance IDs.",
        ],
        "summary": r["summary"],
        "certified_by": "Test Engineer",
    }
    ensure_workspace()
    (BUILD / WEEK1_ACCEPT).write_text(json.dumps(record, indent=2), encoding="utf-8")
    return {**r, "accepted": record["accepted"], "record": record}


def run_self_play_fn() -> dict:
    ensure_workspace()
    # Hard dependency on the Test Engineer: no acceptance record -> refuse to run.
    acc = BUILD / ACCEPT
    if not acc.exists():
        return {"ok": False, "log": "",
                "summary": "REFUSED — no acceptance record. The Test Engineer must "
                "certify the build (build/acceptance.json) before balance can run."}
    try:
        rec = json.loads(acc.read_text(encoding="utf-8"))
    except Exception:
        return {"ok": False, "summary": "REFUSED — acceptance record unreadable.", "log": ""}
    if not rec.get("accepted"):
        return {"ok": False, "log": "",
                "summary": "REFUSED — build not accepted by the Test Engineer (gate failed)."}
    if not (BUILD / IMPL).exists():
        return {"ok": False, "summary": "no build/Combat.cpp to balance-test", "log": ""}
    ok, log, exe = _compile([IMPL, SELFPLAY], "selfplay_runner")
    if not ok:
        return {"ok": False, "summary": "self-play compile FAILED", "log": log}
    p = subprocess.run([exe], capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    return {"ok": True, "summary": "self-play complete", "log": p.stdout.strip()}


def read_reference(name: str) -> str:
    """Helper for the offline path: load a bundled reference implementation."""
    return (REF / name).read_text(encoding="utf-8")


# --------------------------------------------------------------------------- #
# GDD §4.11 row 9 — the presentation bridge's headless half (§4.9 Spec Stub 9).
#
# T-INT-01 and T-INT-04 are the two invariants of that stub that are NOT marked † in
# §4.11: they need no editor and no engine, so §4.9's Acceptance line runs them "on
# every gate run" and says of T-INT-04 that "the gate run itself is the assert".
# T-INT-02, T-INT-03 and T-INT-05 are the editor pass and do not run here.
# --------------------------------------------------------------------------- #
STRATRULES_DIR = ("Source", "StratRules")
STRATRULES_MANIFEST = "StratRules.manifest.json"
STRATRULES_BUILD_CS = "StratRules.Build.cs"
# `ue_module/` holds the UBT wrapper and the manifest's fixed text as TRACKED blobs.
# Both are read here from the git object store at rulesCommit — never imported from
# sync_stratrules.py, which would make this check a restatement of the generator
# rather than an assertion about it.
STRATRULES_MODULE_PREFIX = "ue_module/"
STRATRULES_MANIFEST_FIELDS = "manifest_fields.json"
# The vendored set, declared rather than inferred from a glob of cpp_reference/.
# It must partition the crew's modules — see the comment at its use below.
STRATRULES_VENDORED_SET = "vendored_set.json"


def find_ue_project(explicit: str | None = None) -> Path | None:
    """Locate the UE project that holds Source/StratRules/, or None."""
    cand = Path(explicit).resolve() if explicit else (ROOT.parent / "Stratocracy")
    return cand if (cand / "Source").is_dir() else None


def _git_out(args: list[str]) -> tuple[bool, bytes]:
    p = subprocess.run(["git", *args], cwd=str(ROOT), capture_output=True)
    return p.returncode == 0, (p.stdout if p.returncode == 0 else p.stderr)


def run_integration_gate_fn(ue_path: str | None = None) -> dict:
    """§4.9 Stub 9, headless half: T-INT-01 source identity + T-INT-04 no engine deps.

    T-INT-01 DOES NOT TRUST THE MANIFEST'S OWN HASHES, and does not import the vendor
    script's file list. It takes only `rulesCommit` from the manifest, then re-derives
    both the expected file SET and every expected hash from the crew repo at that
    commit via `git ls-tree` / `git show`. A hand edit that changed a vendored source
    and its recorded hash together still fails, and a vendor script that silently
    dropped a module still fails — neither of which a manifest-vs-disk comparison or a
    shared constant could see.

    IT COVERS EVERY FILE IN THE DIRECTORY, by two mechanisms rather than one. The 20
    sources and `StratRules.Build.cs` hash-match tracked blobs (`cpp_reference/` and
    `ue_module/` respectively). `StratRules.manifest.json` is REBUILT here from
    `ue_module/manifest_fields.json` plus the independently re-derived hashes, and
    compared byte-for-byte. The manifest is not hash-matched because it CANNOT be: it
    records `rulesCommit`, and a file's bytes cannot contain the sha of the tree that
    holds them. Recomputation is the strongest available check on it, not a weaker
    substitute — and it is written out here rather than imported, so it asserts
    something about the generator instead of restating it.

    T-INT-04 compiles each vendored implementation to an OBJECT file, not an
    executable: the module has no `main()` by construction, so object compilation is
    what "compiles standalone" can mean here. It runs outside UBT entirely, against
    the vendored copy rather than against cpp_reference/, which is the only way it can
    witness an engine header that vendoring introduced.
    """
    lines: list[str] = []
    results: list[tuple[str, bool, str]] = []

    def check(ident: str, ok: bool, detail: str) -> None:
        results.append((ident, ok, detail))
        lines.append(f"{'PASS' if ok else 'FAIL'}  {ident} {detail}")

    ue = find_ue_project(ue_path)
    if ue is None:
        where = ue_path or str(ROOT.parent / "Stratocracy")
        return {"ran": False, "passed": False, "failures": [],
                "summary": ("INTEGRATION GATE SKIPPED — no UE project at "
                            f"{where}. T-INT-01 and T-INT-04 assert over "
                            "Source/StratRules/, which is not on this machine; "
                            "nothing was checked and nothing is claimed."),
                "log": ""}

    dest = ue.joinpath(*STRATRULES_DIR)
    if not dest.is_dir():
        return {"ran": False, "passed": False, "failures": [],
                "summary": (f"INTEGRATION GATE SKIPPED — {dest} does not exist. "
                            "Run `python sync_stratrules.py` to vendor first."),
                "log": ""}

    # ---- T-INT-01: source identity ------------------------------------------ #
    manifest_path = dest / STRATRULES_MANIFEST
    commit = None
    if not manifest_path.is_file():
        check("T-INT-01", False,
              f"no {STRATRULES_MANIFEST} — nothing records which crew commit these "
              "files came from, so the evidence chain is broken at the vendor step")
    else:
        try:
            commit = json.loads(manifest_path.read_text(encoding="utf-8"))["rulesCommit"]
        except Exception as exc:
            check("T-INT-01", False, f"{STRATRULES_MANIFEST} unreadable: {exc}")

    if commit is not None:
        ok_commit, _ = _git_out(["cat-file", "-e", f"{commit}^{{commit}}"])
        if not ok_commit:
            check("T-INT-01", False,
                  f"recorded rulesCommit {commit[:7]} is not a commit in this crew "
                  "repo — the recorded source cannot be produced")
        else:
            ok_tree, tree = _git_out(["ls-tree", "--name-only", f"{commit}:cpp_reference"])
            crew_names = set(tree.decode("utf-8", "replace").split())
            # A crew MODULE is an X with both cpp_reference/X.h and X.good.cpp. That
            # set is not the vendored set and has not been since Save landed at
            # 737f666: §4.9 vendors ten modules and names three the Director ruled out.
            # Deriving `expected` by globbing cpp_reference/ conflated the two, so any
            # rulesCommit at or after 737f666 demanded the excluded three be vendored
            # and rulesCommit could not be advanced at all. The vendored set is now
            # DECLARED, in a blob tracked under ue_module/ and read here from the
            # object store — never imported from sync_stratrules.py, on the same terms
            # as manifest_fields.json below.
            crew_modules = {n[:-2] for n in crew_names
                            if n.endswith(".h") and f"{n[:-2]}.good.cpp" in crew_names}
            vendored: list[str] = []
            set_problem = None
            ok_set, sblob = _git_out(
                ["show", f"{commit}:{STRATRULES_MODULE_PREFIX}{STRATRULES_VENDORED_SET}"])
            if not ok_set:
                set_problem = (f"no {STRATRULES_MODULE_PREFIX}{STRATRULES_VENDORED_SET} "
                               f"at {commit[:7]} — the vendored set is undeclared")
            else:
                try:
                    decl = json.loads(sblob.decode("utf-8"))
                    vendored = sorted(decl["vendored"])
                    excluded = sorted(decl["excluded"])
                    # The declaration must PARTITION the crew's modules. A module in
                    # neither list is the case that matters: it is how a new crew module
                    # gets vendored by accident, or forgotten in silence. The old glob
                    # bought exactly this property and it is kept rather than dropped.
                    both = sorted(set(vendored) & set(excluded))
                    unaccounted = sorted(crew_modules - set(vendored) - set(excluded))
                    phantom = sorted((set(vendored) | set(excluded)) - crew_modules)
                    if both:
                        set_problem = ("declared both vendored and excluded: "
                                       + ", ".join(both))
                    elif unaccounted:
                        set_problem = (
                            f"{len(unaccounted)} crew module(s) in neither the vendored "
                            f"nor the excluded list at {commit[:7]}: "
                            + ", ".join(unaccounted))
                    elif phantom:
                        set_problem = ("declared module(s) with no cpp_reference source "
                                       f"at {commit[:7]}: " + ", ".join(phantom))
                except Exception as exc:
                    set_problem = f"{STRATRULES_VENDORED_SET} unreadable: {exc}"

            expected = sorted(f"{m}{suf}" for m in vendored
                              for suf in (".h", ".good.cpp"))
            # Every file in the directory is now accounted for: the vendored sources and
            # the UBT wrapper hash-match tracked blobs; the manifest is REBUILT here and
            # compared byte-for-byte. Nothing is exempt.
            hashed = {n: f"cpp_reference/{n}" for n in expected}
            hashed[STRATRULES_BUILD_CS] = (
                f"{STRATRULES_MODULE_PREFIX}{STRATRULES_BUILD_CS}")
            present = sorted(p.name for p in dest.iterdir() if p.is_file())
            accounted = sorted(list(hashed) + [STRATRULES_MANIFEST])
            missing = [n for n in accounted if n not in present]
            extra = [n for n in present if n not in accounted]
            mismatched = []
            derived: dict[str, str] = {}
            for name, path in sorted(hashed.items()):
                if name in missing:
                    continue
                ok_blob, blob = _git_out(["show", f"{commit}:{path}"])
                if not ok_blob:
                    mismatched.append(f"{name} (no {path} at {commit[:7]})")
                    continue
                want = hashlib.sha256(blob).hexdigest()
                derived[name] = want
                got = hashlib.sha256((dest / name).read_bytes()).hexdigest()
                if want != got:
                    mismatched.append(name)

            # ---- the manifest, by recomputation rather than by hash-match --------- #
            # It records rulesCommit, so it cannot be stored in the crew repo at that
            # commit — a file's bytes cannot contain the sha of the tree holding them.
            # Recomputing it from the tracked fields is therefore the STRONGEST check
            # available, not a weaker substitute for hashing a blob.
            manifest_problem = None
            if STRATRULES_MANIFEST in missing:
                manifest_problem = None  # already reported as missing
            else:
                ok_f, fblob = _git_out(
                    ["show", f"{commit}:{STRATRULES_MODULE_PREFIX}"
                             f"{STRATRULES_MANIFEST_FIELDS}"])
                if not ok_f:
                    manifest_problem = (f"no {STRATRULES_MODULE_PREFIX}"
                                        f"{STRATRULES_MANIFEST_FIELDS} at {commit[:7]}")
                else:
                    try:
                        f = json.loads(fblob.decode("utf-8"))
                        want_manifest = json.dumps({
                            "rulesCommit": commit,
                            "generator": f["generator"],
                            "sourceRepo": f["sourceRepo"],
                            "sourcePrefix": f["sourcePrefix"],
                            "modulePrefix": f["modulePrefix"],
                            "note": f["note"],
                            "files": {n: derived[n] for n in expected},
                            "moduleFiles": {
                                STRATRULES_BUILD_CS: derived[STRATRULES_BUILD_CS]},
                        }, indent=2) + "\n"
                        got_manifest = manifest_path.read_bytes()
                        if got_manifest != want_manifest.encode("utf-8"):
                            manifest_problem = (
                                f"{STRATRULES_MANIFEST} is not what {commit[:7]} "
                                "implies — recomputed bytes differ")
                    except KeyError as exc:
                        manifest_problem = f"manifest fields incomplete: {exc}"
                    except Exception as exc:
                        manifest_problem = f"manifest recomputation failed: {exc}"

            problems = []
            if missing:
                problems.append(f"missing {len(missing)}: {', '.join(missing)}")
            if extra:
                problems.append(f"unexpected {len(extra)}: {', '.join(extra)}")
            if mismatched:
                problems.append(f"hash mismatch {len(mismatched)}: {', '.join(mismatched)}")
            if manifest_problem:
                problems.append(manifest_problem)
            if set_problem:
                # An unreadable or non-partitioning declaration makes every other arm
                # meaningless — `expected` is derived FROM it — so report the cause
                # alone rather than burying it under the consequences it produced.
                problems = [set_problem]
            if problems:
                check("T-INT-01", False,
                      f"source identity vs {commit[:7]} — " + "; ".join(problems))
            else:
                check("T-INT-01", True,
                      f"source identity: all {len(present)} files in Source/StratRules/ "
                      f"are accounted for at {commit[:7]} — {len(expected)} sources and "
                      f"{STRATRULES_BUILD_CS} hash-match tracked blobs, "
                      f"{STRATRULES_MANIFEST} recomputes byte-for-byte, and the declared "
                      f"vendored set partitions the {len(crew_modules)} crew modules "
                      f"({len(vendored)} vendored, {len(crew_modules) - len(vendored)} "
                      "ruled out)")

    # ---- T-INT-04: no engine deps ------------------------------------------- #
    cc = find_compiler()
    if cc is None:
        check("T-INT-04", False,
              "no C++ compiler on PATH — the gate run IS the assert, so a gate that "
              "cannot compile asserts nothing")
    else:
        impls = sorted(p.name for p in dest.glob("*.good.cpp"))
        if not impls:
            check("T-INT-04", False, "no vendored implementations to compile")
        else:
            objdir = BUILD / "stratrules_obj"
            objdir.mkdir(parents=True, exist_ok=True)
            broken = []
            for name in impls:
                if _is_msvc(cc):
                    cmd = [cc, "/nologo", "/std:c++17", "/EHsc", "/c",
                           f"/I{dest}", str(dest / name), f"/Fo:{objdir / (name + '.obj')}"]
                else:
                    cmd = [cc, "-std=c++17", "-c", f"-I{dest}", str(dest / name),
                           "-o", str(objdir / (name + ".o"))]
                p = subprocess.run(cmd, capture_output=True, text=True,
                                   encoding="utf-8", errors="replace", cwd=str(objdir))
                if p.returncode != 0:
                    detail = ((p.stdout + p.stderr) if _is_msvc(cc)
                              else (p.stderr or p.stdout)).strip().splitlines()
                    broken.append(f"{name}: {detail[0] if detail else 'compile failed'}")
            if broken:
                check("T-INT-04", False,
                      f"standalone compile under {os.path.basename(cc)} — "
                      + "; ".join(broken))
            else:
                check("T-INT-04", True,
                      f"no engine deps: {len(impls)} vendored implementations compile "
                      f"standalone under {os.path.basename(cc)}, outside UBT")

    failures = [ident for ident, ok, _ in results if not ok]
    passed = not failures
    tally = f"{sum(1 for _, ok, _ in results if ok)}/{len(results)} passed"
    lines.append("")
    lines.append("T-INT-02, T-INT-03 and T-INT-05 are the editor pass (§4.9 "
                 "Acceptance) and DID NOT RUN HERE — this gate is headless and cannot "
                 "run any of them. That is now the ONLY thing this gate can say about "
                 "them: T-INT-02 and T-INT-03 have since RUN AND PASSED in the editor "
                 "pass at UE 0897cb5, where the §4.9 part 2 bridge landed. "
                 "T-INT-05 is the one "
                 "still uncovered, and what it lacks is the real Stratocracy widgets it "
                 "asserts over. Row 9 cannot flip on this gate alone — it never could, "
                 "and whether it flips now is a question for the ledger and not for "
                 "this runner.")
    lines.append(tally)
    summary = ("INTEGRATION GATE PASS — T-INT-01, T-INT-04" if passed
               else f"INTEGRATION GATE BLOCK — failing: {', '.join(failures)}")
    return {"ran": True, "passed": passed, "failures": failures,
            "summary": f"{summary} ({tally})", "log": "\n".join(lines)}


# --------------------------------------------------------------------------- #
# CrewAI @tool wrappers (thin string-returning adapters for the live agents)
# --------------------------------------------------------------------------- #
try:
    from crewai.tools import tool

    @tool("write_combat_impl")
    def write_combat_impl(cpp_source: str) -> str:
        """Write the C++ implementation of the Combat module to build/Combat.cpp.
        Pass the FULL contents of Combat.cpp (implementing the two functions declared
        in Combat.h). Returns a confirmation string."""
        return write_combat_impl_fn(cpp_source)

    @tool("run_test_gate")
    def run_test_gate() -> str:
        """Compile build/Combat.cpp against the fixed test harness and run it.
        This is the merge gate: returns PASS only if every invariant (T-COMBAT-01..10, T-REPAIR-01..07)
        holds. On failure it lists exactly which tests failed so the implementation
        can be corrected. Takes no arguments."""
        r = run_test_gate_fn()
        return f"{r['summary']}\n\n{r['log']}"

    @tool("certify_build")
    def certify_build() -> str:
        """Run the full invariant gate (T-COMBAT-01..10, T-REPAIR-01..07) AND write the build's acceptance
        record to build/acceptance.json. This certification is REQUIRED before self-play can
        run — the Balance Analyst refuses without it. Certify only a fully passing build.
        Takes no arguments."""
        r = certify_build_fn()
        return f"{r['summary']} | accepted={r['accepted']}\n\n{r['log']}"

    @tool("run_self_play")
    def run_self_play() -> str:
        """Compile build/Combat.cpp against the self-play harness and run AI-vs-AI
        duels, returning the balance table (winners + rounds-to-kill). REQUIRES the Test
        Engineer's acceptance record (build/acceptance.json); refuses without it. Takes no
        arguments."""
        r = run_self_play_fn()
        return f"{r['summary']}\n\n{r['log']}"

    @tool("run_week1_gate")
    def run_week1_gate() -> str:
        """Compile and run the acceptance suites for GDD §4.11 rows 1-8 (hex grid &
        math, data tables, movement & pathfinding, capture & Fame economy, turn loop
        & win/tiebreak, opponent AI, scenario file & validator, UI binding contract)
        plus the debug-command driver. Returns PASS only if T-HEX-01..07,
        T-DATA-01..04+06, T-MOVE-01..06, T-FAME-01..09, T-TURN-01..10, T-AI-01..06,
        row 7's SUBSET of T-SCN and row 8's SUBSET of T-UI all hold, and names the
        failing IDs per row otherwise. Takes no arguments."""
        r = run_week1_gate_fn()
        return r["summary"] + "\n\n" + "\n\n".join(row["log"] for row in r["rows"])

    @tool("certify_week1")
    def certify_week1() -> str:
        """Run the full week-1 invariant gate (§4.11 rows 1-8) AND write its acceptance
        record to build/acceptance_week1.json. The record also states what it does not
        cover — T-DATA-05 is in-editor and green there, T-MOVE-07 is unwritten on Q2, and four of row
        7's fixtures have no map to run against. Certify only a fully passing build.
        Takes no arguments."""
        r = certify_week1_fn()
        return f"{r['summary']} | accepted={r['accepted']}"

    CREW_TOOLS_AVAILABLE = True
except Exception:  # crewai not installed — offline path still works
    CREW_TOOLS_AVAILABLE = False
    write_combat_impl = run_test_gate = certify_build = run_self_play = None
    run_week1_gate = certify_week1 = None
