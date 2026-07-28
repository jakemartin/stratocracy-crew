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

HEADER = "Combat.h"
TEST = "test_combat.cpp"
SELFPLAY = "selfplay.cpp"
IMPL = "Combat.cpp"          # the file the Systems Engineer authors
ACCEPT = "acceptance.json"   # the release record — ONLY the Test Engineer writes this


# --------------------------------------------------------------------------- #
# workspace + compiler plumbing
# --------------------------------------------------------------------------- #
def ensure_workspace() -> Path:
    """Create build/ and copy the fixed (non-authored) sources into it."""
    BUILD.mkdir(exist_ok=True)
    for f in (HEADER, TEST, SELFPLAY):
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
        "tests": "T-COMBAT-01..08",
        "failures": r.get("failures", []),
        "summary": r["summary"],
        "certified_by": "Test Engineer",
    }
    ensure_workspace()
    (BUILD / ACCEPT).write_text(json.dumps(record, indent=2), encoding="utf-8")
    return {**r, "accepted": record["accepted"]}


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
        This is the merge gate: returns PASS only if every invariant (T-COMBAT-01..08)
        holds. On failure it lists exactly which tests failed so the implementation
        can be corrected. Takes no arguments."""
        r = run_test_gate_fn()
        return f"{r['summary']}\n\n{r['log']}"

    @tool("certify_build")
    def certify_build() -> str:
        """Run the full invariant gate (T-COMBAT-01..08) AND write the build's acceptance
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

    CREW_TOOLS_AVAILABLE = True
except Exception:  # crewai not installed — offline path still works
    CREW_TOOLS_AVAILABLE = False
    write_combat_impl = run_test_gate = certify_build = run_self_play = None
