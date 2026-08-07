"""Vendor the canonical data files into the Unreal project -- GDD §4.8 and §4.9.

Three kinds travel this path: the §4.8 tables, and from 2026-08-07 both Stub 7's
shipped scenario file, which §4.9 part 2's bridge loads, and the committed §4.10
parity fixture, which the editor pass replays. See TABLES / SCENARIOS / FIXTURES
below for why the latter two are here and why that makes neither a §4.8 table.

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

# The canonical set, declared rather than globbed, so the manifest's scope is a
# stated list and not whatever happens to sit in `data/`.
#
# TWO KINDS OF FILE, and the manifest says which is which rather than implying
# that everything it carries is a §4.8 table. The three CSVs are the §4.8 tables
# T-DATA-05 compares row-for-row. ferrum_crossing.json is Stub 7's scenario file
# and is NOT a §4.8 table -- that reading is unchanged. What changed on 2026-08-07
# is that it now has to be HERE: §4.9 part 2's bridge calls `strat::loadScenario`
# on the shipped scenario, and T-INT-02 replays the same log headless and
# in-engine to one hash. Both sides must seed `GameState` from the SAME BYTES
# through the same vendored Scenario module; a seed hand-built on the engine side
# would be the ported-not-vendored divergence T-INT-02 exists to catch.
#
# THREE KINDS AS OF 2026-08-07, and parity_fixture.save is the third. It is neither
# a table nor a scenario: it is the committed §4.10 save the editor pass REPLAYS, the
# subject of T-INT-02 and of T-SAVE-06's in-engine half. It is carried here for the
# same reason the scenario is -- the headless and in-engine replays must consume the
# SAME BYTES, and a fixture re-emitted on the engine side would compare the engine
# against itself. The crew-side GATE-REPLAY-FIXTURE keeps it fresh at the source;
# GATE-DATA-VENDOR keeps this copy honest against that source.
TABLES = ["units.csv", "terrain.csv", "effectiveness.csv"]
SCENARIOS = ["ferrum_crossing.json"]
FIXTURES = ["parity_fixture.save"]

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
    for name in TABLES + SCENARIOS + FIXTURES:
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
            "unchanged from the crew repo. THREE KINDS OF FILE, and being carried here "
            "makes none of them another. units.csv, terrain.csv and effectiveness.csv "
            "are the §4.8 tables; T-DATA-05 compares the imported UDataTable rows "
            "against those bytes. ferrum_crossing.json is Stub 7's scenario file and "
            "is NOT a §4.8 table -- T-DATA-05 asserts nothing about it and imports it "
            "into no UDataTable. It is carried from 2026-08-07 because §4.9 part 2's "
            "bridge loads the shipped scenario through strat::loadScenario, and "
            "T-INT-02 requires the headless and in-engine replays to seed GameState "
            "from the same bytes. parity_fixture.save is the committed §4.10 save the "
            "editor pass REPLAYS -- the subject of T-INT-02 and of T-SAVE-06's "
            "in-engine half -- and is likewise imported into no UDataTable; it is "
            "carried for the same reason as the scenario, because a fixture re-emitted "
            "on the engine side would compare the engine against itself. It is kept "
            "fresh at the source by the crew's GATE-REPLAY-FIXTURE, and this copy is "
            "kept equal to that source by GATE-DATA-VENDOR. Every file named in `files` "
            "below is hash-checked by GATE-DATA-VENDOR regardless of which kind it is. "
            "This manifest records "
            "dataCommit, so like StratRules.manifest.json it cannot itself be stored "
            "at that commit and is verified by recomputation rather than by hash-match."
        ),
        "files": entries,
    }
    (dest / MANIFEST).write_text(
        json.dumps(manifest, indent=2, sort_keys=False) + "\n", encoding="utf-8")

    print(f"\nVendored {len(TABLES)} tables + {len(SCENARIOS)} scenario(s) + "
          f"{len(FIXTURES)} replay fixture(s) to {dest} at {commit[:7]}.")
    print("Re-import the DataTables in the editor if the bytes changed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
