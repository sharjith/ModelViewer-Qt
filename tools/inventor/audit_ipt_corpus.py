# SPDX-License-Identifier: GPL-3.0-only
"""Run the native IPT probe on a local corpus and report neutral sample IDs."""

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import subprocess


def samples_in(folder):
    return sorted(
        (
            path
            for path in folder.rglob("*")
            if path.is_file()
            and path.suffix.casefold() == ".ipt"
            and all(
                part.casefold() != "oldversions"
                for part in path.relative_to(folder).parts[:-1]
            )
        ),
        key=lambda path: (path.stat().st_size, path.relative_to(folder).as_posix()),
    )


def file_hash(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def diagnose(message):
    if "failed OCCT solid validation" in message:
        return "solid_validation"
    if "Missing saved Inventor B-spline data" in message:
        return "spline_payload"
    if "Unsupported Inventor torus radii" in message:
        return "torus_profile"
    if "Could not construct a trimmed Inventor edge" in message or "Could not construct Inventor edge" in message:
        return "edge_construction"
    if "Unsupported Inventor ASM record layout" in message:
        return "record_layout"
    if "no saved solid bodies" in message or "one shell without wire geometry" in message:
        return "body_profile"
    return "other"


def audit(probe, timeout, sample_id, path):
    report = {
        "sample": sample_id,
        "sha256": file_hash(path),
    }
    try:
        process = subprocess.run(
            [probe, "--part-solid", path],
            capture_output=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return dict(report, status="timeout")
    if process.returncode == 0:
        return dict(report, status="reconstructed", geometry=json.loads(process.stdout))
    diagnostic = process.stderr.strip().replace(str(path), "<sample>").replace(path.name, "<sample>")
    return dict(
        report,
        status="rejected",
        failure_group=diagnose(diagnostic),
        diagnostic=diagnostic,
        exit_code=process.returncode,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--folder", nargs=2, action="append", metavar=("ID", "PATH"), required=True)
    parser.add_argument("--timeout", type=float, default=25, help="Seconds per part")
    args = parser.parse_args()
    if args.timeout <= 0 or not args.probe.is_file():
        parser.error("An existing probe and a positive per-part timeout are required")
    probe = args.probe.resolve()
    results = {}
    for package, folder in args.folder:
        if package in results or not Path(folder).is_dir():
            parser.error("Each package needs a unique ID and an existing folder")
        entries = [(f"{package}{i:02}", path) for i, path in enumerate(samples_in(Path(folder)), 1)]
        with ThreadPoolExecutor(max_workers=2) as pool:
            rows = list(pool.map(lambda item: audit(probe, args.timeout, *item), entries))
        results[package] = {
            "results": dict(sorted(Counter(row["status"] for row in rows).items())),
            "failure_groups": dict(
                sorted(Counter(row["failure_group"] for row in rows if "failure_group" in row).items())
            ),
            "parts": rows,
        }
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
