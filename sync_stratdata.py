"""Vendor the canonical data CSVs into the Unreal project -- GDD §4.8.

§4.8's principle is "authored once, read twice, proven equal": each table is ONE
canonical CSV in this repo under `data/`. The headless loader parses those bytes
directly. The editor imports the same bytes into a UDataTable. T-DATA-05 asserts
every imported row equals the CSV field-for-field.

For the editor side to read "the same file", the UE project needs those bytes.
This script copies them from the GIT OBJECT STORE at a named commit -- never from
the working tree -- so the vendored copy is by construction the committed bytes,
the same property `sync_stratrules.py` relies on for the sources.

WHY THIS IS A SEPARATE SCRIPT, not a few lines added to sync_stratrules.py.
That script's output is hash-matched by T-INT-01, and its manifest records
`rulesCommit`. Both T-INT-01 and T-INT-04 are GREEN at `e19605e`. Adding a new
category of vendored file to that mechanism would change the manifest those two
IDs are asserted over and re-date closures this round has no mandate to move.
The data CSVs are therefore vendored on their own path, with their own manifest,
under `Data/` -- outside `Source/StratRules/`, which T-INT-01 requires to contain
nothing unaccounted for.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent

# The canonical set, declared rather than globbed. `data/` also holds
# ferrum_crossing.json, which is Stub 7's scenario file and NOT a §4.8 table --
# globbing the directory would vendor it and make the manifest claim more than
# §4.8 describes. Declaring the three names keeps the manifest's scope equal to
# the schema section's scope.
TABLES = ["units.csv", "terrain.csv", "effectiveness.csv"]

MANIFEST = "StratData.manifest.json"


def git(*args: str) -> str:
    p = subprocess.run(["git", *args], cwd=str(ROOT), capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    if p.returncode != 0:
        raise SystemExit(f"git {' '.join(args)} failed: {(p.stderr or p.stdout).strip()}")
    return p.stdout


def git_bytes(*args: str) -> bytes:
    p = subprocess.run(["git", *args], cwd=str(ROOT), capture_output=True)
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

    # A dirty data/ is not fatal -- the bytes come from the object store either
    # way -- but the operator should be told the vendored bytes are not the ones
    # on disk, rather than discovering it from a parity failure later.
    dirty = git("status", "--porcelain", "--", "data").strip()
    if dirty:
        print(f"NOTE: data/ has uncommitted changes. Vendoring the committed "
              f"bytes of {commit[:7]}, NOT the working tree:")
        for line in dirty.splitlines():
            print("   " + line)

    dest = ue / "Data"
    dest.mkdir(parents=True, exist_ok=True)

    entries = {}
    for name in TABLES:
        blob = git_bytes("show", f"{commit}:data/{name}")
        (dest / name).write_bytes(blob)
        entries[name] = hashlib.sha256(blob).hexdigest()
        print(f"  vendored data/{name}  {len(blob)} bytes  {entries[name][:12]}")

    manifest = {
        "dataCommit": commit,
        "generator": "sync_stratdata.py",
        "sourceRepo": "stratocracy-crew",
        "sourcePrefix": "data/",
        "note": (
            "Vendored verbatim from the git object store at dataCommit; names are "
            "unchanged from the crew repo. These are the §4.8 tables only -- "
            "data/ferrum_crossing.json is Stub 7's scenario file and is deliberately "
            "not here. T-DATA-05 compares the imported UDataTable rows against these "
            "bytes. This manifest records dataCommit, so like StratRules.manifest.json "
            "it cannot itself be stored at that commit and is verified by "
            "recomputation rather than by hash-match."
        ),
        "files": entries,
    }
    (dest / MANIFEST).write_text(
        json.dumps(manifest, indent=2, sort_keys=False) + "\n", encoding="utf-8")

    print(f"\nVendored {len(TABLES)} tables to {dest} at {commit[:7]}.")
    print("Re-import the DataTables in the editor if the bytes changed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
