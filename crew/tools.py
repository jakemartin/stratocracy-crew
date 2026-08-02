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
# Week 1 — GDD §4.11 rows 1-3, the three rows §4.4 week 1 owes.
#
# Same shape as Combat: Director-owned headers and test harnesses are FIXED (copied
# from cpp_reference/), the implementation is authored into build/, and the gate is a
# real compile + run. Each row gets its own runner so a failure is attributable to one
# ledger row rather than to "week 1".
# --------------------------------------------------------------------------- #
WEEK1_FIXED = ("Hex.h", "Data.h", "Move.h", "Driver.h",
               "test_hex.cpp", "test_data.cpp", "test_move.cpp", "test_driver.cpp",
               "driver_main.cpp")

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
    # Not a §4.7 stub and not a ledger row: the debug-command driver builds no rules
    # system, so its checks are named GATE-DRV-* rather than T-* and move no count in
    # the GDD. It closes §4.4 week 1's OTHER promise, "Playable via debug commands".
    "driver": {
        "row": None, "system": "Debug-command driver", "spec": "spec/driver_spec.md",
        "impl": "Driver.cpp",
        "sources": ["Driver.cpp", "Move.cpp", "Hex.cpp", "Data.cpp", "Combat.cpp",
                    "test_driver.cpp"],
        "stem": "test_driver_runner", "tests": "GATE-DRV-01..07",
    },
}
WEEK1_ORDER = ("hex", "data", "move", "driver")  # §4.11 dependency order; driver last
WEEK1_ACCEPT = "acceptance_week1.json"  # the week-1 release record — Test Engineer only

# The playable artifact itself — built from the same sources plus the REPL entry point.
DRIVER_BINARY = "stratocracy_debug"
DRIVER_SOURCES = ["Driver.cpp", "Move.cpp", "Hex.cpp", "Data.cpp", "Combat.cpp",
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
    missing = [s for s in spec["sources"] if not (BUILD / s).exists()]
    if missing:
        return {"row": spec["row"], "system": spec["system"], "tests": spec["tests"],
                "compiled": False, "passed": False, "failures": [], "log": "",
                "summary": f"missing source(s): {', '.join(missing)} — "
                           "the Systems Engineer must author the implementation first"}
    ok, log, exe = _compile(spec["sources"], spec["stem"])
    if not ok:
        return {"row": spec["row"], "system": spec["system"], "tests": spec["tests"],
                "compiled": False, "passed": False, "failures": [], "log": log,
                "summary": f"row {spec['row']} compile FAILED"}
    p = subprocess.run([exe, str(DATA)], capture_output=True, text=True,
                       encoding="utf-8", errors="replace", cwd=str(BUILD))
    out = p.stdout
    failures = [ln.split()[1] for ln in out.splitlines() if ln.startswith("FAIL")]
    passed = p.returncode == 0 and not failures
    tally = next((ln for ln in out.splitlines() if "passed" in ln), "").strip()
    # The driver has no ledger row, so it is labelled by name rather than by number.
    label = f"row {spec['row']}" if spec["row"] is not None else "no row"
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
                        "T-MOVE-01..06) + the debug driver (GATE-DRV-01..07)" if passed else
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
    """Test Engineer's release gate for §4.11 rows 1-3 — runs every invariant AND
    writes build/acceptance_week1.json. The ONLY writer of that record.

    The record states what it does NOT cover as well as what it does: T-DATA-05 is the
    in-editor Unreal Automation half of row 2, marked † in §4.11, and no headless run
    can assert it. Q29 refuses a ledger flip on a partial acceptance set, so row 2's
    flip waits on the editor pass even when everything here is green.
    """
    r = run_week1_gate_fn()
    record = {
        "accepted": bool(r["passed"]),
        "scope": "GDD §4.11 rows 1-3 (§4.4 week 1)",
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
            "EUnitType mirror). §4.11 marks it †; it is not headless and did not run.",
            "T-MOVE-07 — reserved and unwritten, blocked on the Q2 movement-class "
            "ruling (§4.7 Stub 3).",
            "GATE-DRV-01..07 gate the debug-command driver, which is NOT a §4.7 stub "
            "and NOT a §3 ledger row — it builds no rules system. They flip nothing "
            "and are not GDD acceptance IDs.",
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
        """Compile and run the acceptance suites for GDD §4.11 rows 1-3 (hex grid &
        math, data tables, movement & pathfinding) — the three rows §4.4 week 1 owes.
        Returns PASS only if T-HEX-01..07, T-DATA-01..04+06 and T-MOVE-01..06 all
        hold, and names the failing IDs per row otherwise. Takes no arguments."""
        r = run_week1_gate_fn()
        return r["summary"] + "\n\n" + "\n\n".join(row["log"] for row in r["rows"])

    @tool("certify_week1")
    def certify_week1() -> str:
        """Run the full week-1 invariant gate (§4.11 rows 1-3) AND write its acceptance
        record to build/acceptance_week1.json. The record also states what it does not
        cover — T-DATA-05 is in-editor and T-MOVE-07 is unwritten on Q2. Certify only a
        fully passing build. Takes no arguments."""
        r = certify_week1_fn()
        return f"{r['summary']} | accepted={r['accepted']}"

    CREW_TOOLS_AVAILABLE = True
except Exception:  # crewai not installed — offline path still works
    CREW_TOOLS_AVAILABLE = False
    write_combat_impl = run_test_gate = certify_build = run_self_play = None
    run_week1_gate = certify_week1 = None
