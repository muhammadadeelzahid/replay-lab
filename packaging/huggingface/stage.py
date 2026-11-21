#!/usr/bin/env python3
"""Builds the folder uploaded to the Hugging Face dataset repository.

    python3 -I packaging/huggingface/stage.py [bundles] [out]

bundles  the run bundles, one folder per run (default: viewer_bundles_phasef)
out      the folder to write (default: hf_dataset), replaced if it exists

The out folder holds README.md (the dataset card), LICENSE, runs.csv (one row per run) and
runs/ (the bundles). The bundles are hard links to the originals, except the logs: cluster
paths in them (home, project and scratch folders) are replaced by $HOME, $PROJECT and
$SCRATCH. Nothing else in the logs changes, so line numbers stay the same.

runs.csv is filled from print_report (build/release/tools/print_report), which computes the
same numbers as the app.
"""

import csv
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

# Longest first: the project folder is also reached through the home folder.
REPLACEMENTS = [
    ("/home/adeel/projects/def-ekram/adeel", "$PROJECT"),
    ("/project/6030214/adeel", "$PROJECT"),
    ("/home/adeel", "$HOME"),
    ("/scratch/adeel", "$SCRATCH"),
]
# Anything still matching after the replacements stops the staging.
LEFTOVER = re.compile(r"adeel|6030214|def-ekram", re.IGNORECASE)


def scrub(text: str) -> str:
    for old, new in REPLACEMENTS:
        text = text.replace(old, new)
    return text


def stage_bundle(source: Path, target: Path) -> None:
    for path in sorted(source.rglob("*")):
        relative = path.relative_to(source)
        destination = target / relative
        if path.is_dir():
            destination.mkdir(parents=True, exist_ok=True)
        elif relative.parts[0] == "logs":
            text = path.read_text(encoding="utf-8", errors="surrogateescape")
            clean = scrub(text)
            if LEFTOVER.search(clean):
                sys.exit(f"personal detail left in {path}")
            destination.write_text(clean, encoding="utf-8", errors="surrogateescape")
        else:
            os.link(path, destination)


def report(print_report: Path, bundle: Path) -> dict:
    """The numbers print_report gives for a run."""
    out = subprocess.run([str(print_report), str(bundle)], check=True, capture_output=True, text=True).stdout
    row = {}
    if m := re.search(r"segments shown\s+(\d+)", out):
        row["segments_shown"] = int(m[1])
    row["cut_off"] = "cut off" in out
    if m := re.search(r"bitrate played\s+([\d.]+) Mbps, (\d+) switches", out):
        row["avg_bitrate_mbps"], row["switches"] = float(m[1]), int(m[2])
    if m := re.search(r"startup delay\s+([\d.]+) s", out):
        row["startup_s"] = float(m[1])
    if m := re.search(r"stalls\s+(\d+) \((\d+) player pauses\), ([\d.]+) s", out):
        row["stalls"], row["player_pauses"], row["stalled_s"] = int(m[1]), int(m[2]), float(m[3])
    if m := re.search(r"VMAF shown\s+([\d.]+) average, ([\d.]+) lowest segment, ([\d.]+) s below 60", out):
        row["vmaf_mean"], row["vmaf_min_segment"], row["below_vmaf60_s"] = float(m[1]), float(m[2]), float(m[3])
    return row


def main() -> None:
    bundles = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "viewer_bundles_phasef"
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "hf_dataset"
    print_report = ROOT / "build/release/tools/print_report"
    if not print_report.exists():
        sys.exit("build the release preset first (print_report)")
    if out.exists():
        shutil.rmtree(out)
    (out / "runs").mkdir(parents=True)

    rows = []
    for source in sorted(p for p in bundles.iterdir() if (p / "run.json").is_file()):
        stage_bundle(source, out / "runs" / source.name)
        info = json.loads((source / "run.json").read_text())
        summary = json.loads((source / "summary.json").read_text())["ues"][0]
        match = re.match(r"(.+)_(llm|robustmpc|mpc|bola)_(\d+)Mbps_run_(\d+)$", source.name)
        row = {
            "run_id": source.name,
            "video": info["ues"][0]["video"],
            "algorithm": info["policy"]["name"],
            "feeder_mbps": int(match[3]) if match else None,
            "run": int(match[4]) if match else None,
            "seed": info["network"].get("rng_run"),
            "segments_total": summary.get("total_segments"),
            "completed": summary.get("completed"),
        }
        row.update(report(print_report, out / "runs" / source.name))
        rows.append(row)

    columns = ["run_id", "video", "algorithm", "feeder_mbps", "run", "seed", "segments_shown",
               "segments_total", "completed", "cut_off", "avg_bitrate_mbps", "switches",
               "startup_s", "stalls", "player_pauses", "stalled_s", "vmaf_mean",
               "vmaf_min_segment", "below_vmaf60_s"]
    with open(out / "runs.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows)

    shutil.copy(HERE / "README.md", out / "README.md")
    shutil.copy(HERE / "LICENSE", out / "LICENSE")
    print(f"staged {len(rows)} runs in {out}")


if __name__ == "__main__":
    main()
