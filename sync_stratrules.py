#!/usr/bin/env python3
"""Vendor the certified headless rules sources into the UE project's StratRules module.

    python sync_stratrules.py                 # vendor HEAD into ../Stratocracy
    python sync_stratrules.py --commit <sha>  # vendor a specific crew commit
    python sync_stratrules.py --ue <path>     # a UE project somewhere else

GDD §4.9: "The UE project vendors them verbatim into a UBT runtime module,
`Source/StratRules/`, via a sync script that records the source commit hash."
This is that script, and T-INT-01 is the check that it stayed true (crew/tools.py).

THE SOURCES ARE READ FROM THE GIT OBJECT STORE, NOT FROM THE WORKING TREE. That is
the whole point: `git show <commit>:cpp_reference/<f>` is by construction the bytes
that commit holds, so T-INT-01's source identity is true the moment this script
finishes and can only be broken afterwards — by editing a vendored file, by adding
or deleting one, or by crew moving on without a re-sync. Vendoring the working tree
instead would let a dirty edit be recorded under a clean commit hash, which is the
one failure T-INT-01 exists to catch and the one it could then never see.

Written as bytes with no newline translation, because T-INT-01 compares hashes.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent

# The vendored set: the ten rules modules, header + implementation.
#
# It is derived, not chosen. §4.9 requires `StratRules` to be pure C++17 in
# `namespace strat` with no engine headers, and a UBT module cannot contain a second
# `main()` — which excludes every test_*.cpp, driver_main.cpp and selfplay.cpp. The
# *.buggy.cpp files are the deliberately-wrong pass-1 fixtures the gate blocks on and
# are not shippable code. Driver is IN the set and is not optional: Ai.good.cpp links
# against it, which is why §4.11 row 6's gate carries Driver.cpp too.
MODULES = ("Combat", "Hex", "Data", "Move", "Economy", "Turn", "Ai", "Scenario",
           "Ui", "Driver")

# Vendored names are the crew names, unchanged — `Ui.good.cpp` stays `Ui.good.cpp`.
# UBT globs *.cpp, so the suffix costs nothing, and it makes T-INT-01 a path-for-path
# identity with no rename map for a later reader to get wrong.
def vendored_names() -> list[str]:
    names = []
    for m in MODULES:
        names.append(f"{m}.h")
        names.append(f"{m}.good.cpp")
    return sorted(names)


# The UBT wrapper and the fixed manifest text are TRACKED IN THIS REPO under
# `ue_module/`, and are read from the object store exactly like the sources above.
# They were previously literals in this file and were exempt from T-INT-01's
# comparison; they are not exempt any more. Keeping them as tracked blobs is what
# lets the check re-derive them WITHOUT importing anything from this script — a
# check that called the generator's own constant would assert nothing about the
# generator.
MODULE_PREFIX = "ue_module/"
BUILD_CS_NAME = "StratRules.Build.cs"
MANIFEST_FIELDS = "manifest_fields.json"

MANIFEST = "StratRules.manifest.json"


def git(*args: str, cwd: Path = ROOT) -> str:
    p = subprocess.run(["git", *args], cwd=str(cwd), capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if p.returncode != 0:
        raise SystemExit(f"git {' '.join(args)} failed: {(p.stderr or p.stdout).strip()}")
    return p.stdout


def git_bytes(*args: str, cwd: Path = ROOT) -> bytes:
    p = subprocess.run(["git", *args], cwd=str(cwd), capture_output=True)
    if p.returncode != 0:
        raise SystemExit(f"git {' '.join(args)} failed: "
                         f"{p.stderr.decode('utf-8', 'replace').strip()}")
    return p.stdout


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--ue", default=str(ROOT.parent / "Stratocracy"),
                    help="UE project root (default: ../Stratocracy)")
    ap.add_argument("--commit", default=None,
                    help="crew commit to vendor (default: HEAD)")
    args = ap.parse_args()

    ue = Path(args.ue).resolve()
    if not (ue / "Source").is_dir():
        print(f"REFUSED: {ue} has no Source/ -- not a UE project root.")
        return 2

    commit = git("rev-parse", args.commit or "HEAD").strip()
    short = commit[:7]

    # A dirty cpp_reference is not fatal -- the bytes come from the object store
    # either way -- but the operator should know the vendored bytes are not the ones
    # on disk. Say it plainly rather than silently vendoring something else.
    dirty = git("status", "--porcelain", "--", "cpp_reference").strip()
    if dirty:
        print("NOTE: cpp_reference has uncommitted changes. Vendoring the committed "
              f"bytes of {short}, NOT the working tree:")
        for line in dirty.splitlines():
            print("   " + line)

    dest = ue / "Source" / "StratRules"
    dest.mkdir(parents=True, exist_ok=True)

    names = vendored_names()
    entries = {}
    for name in names:
        blob = git_bytes("show", f"{commit}:cpp_reference/{name}")
        (dest / name).write_bytes(blob)
        entries[name] = hashlib.sha256(blob).hexdigest()

    # The UBT wrapper comes from the object store too, so it is a vendored file with a
    # different prefix rather than a generated one, and T-INT-01 hash-matches it.
    build_blob = git_bytes("show", f"{commit}:{MODULE_PREFIX}{BUILD_CS_NAME}")
    (dest / BUILD_CS_NAME).write_bytes(build_blob)
    module_entries = {BUILD_CS_NAME: hashlib.sha256(build_blob).hexdigest()}

    # The manifest's fixed text is likewise tracked, so the check can rebuild this
    # file byte-for-byte from the commit alone without importing anything from here.
    fields = json.loads(
        git_bytes("show", f"{commit}:{MODULE_PREFIX}{MANIFEST_FIELDS}")
        .decode("utf-8"))

    # KEY ORDER IS PART OF THE ARTIFACT: T-INT-01 compares the manifest's bytes, so
    # this ordering is reproduced deliberately in crew/tools.py rather than shared.
    manifest = {
        "rulesCommit": commit,
        "generator": fields["generator"],
        "sourceRepo": fields["sourceRepo"],
        "sourcePrefix": fields["sourcePrefix"],
        "modulePrefix": fields["modulePrefix"],
        "note": fields["note"],
        "files": {n: entries[n] for n in names},
        "moduleFiles": module_entries,
    }
    (dest / MANIFEST).write_text(json.dumps(manifest, indent=2) + "\n",
                                 encoding="utf-8", newline="\n")

    print(f"vendored {len(names)} sources + {len(module_entries)} module file(s) "
          f"from {short} -> {dest}")
    print(f"  rulesCommit {commit}")
    print(f"  wrote {BUILD_CS_NAME} and {MANIFEST}")
    print("\nSource identity is now true by construction. Run the gate to assert it:")
    print("    python run.py --integration")
    return 0


if __name__ == "__main__":
    sys.exit(main())
