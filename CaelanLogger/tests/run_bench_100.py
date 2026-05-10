#!/usr/bin/env python3

import argparse
import csv
import re
import statistics
import subprocess
import sys
from pathlib import Path


def run_cmd(cmd, cwd=None):
    return subprocess.run(
        cmd,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )


def parse_section(output: str, section_name: str) -> dict:
    pattern = rf"\[{re.escape(section_name)}\](.*?)(?=\n\[|\nValidation:|\nLog dirs:|\Z)"
    m = re.search(pattern, output, re.S)
    if not m:
        return {}

    text = m.group(1)

    def grab_float(label):
        m2 = re.search(rf"{re.escape(label)}:\s*([0-9]+(?:\.[0-9]+)?)", text)
        return float(m2.group(1)) if m2 else None

    def grab_int(label):
        m2 = re.search(rf"{re.escape(label)}:\s*([0-9]+)", text)
        return int(m2.group(1)) if m2 else None

    def grab_checksum():
        m2 = re.search(r"checksum:\s*(0x[0-9a-fA-F]+)", text)
        return m2.group(1) if m2 else ""

    def grab_dropped():
        m2 = re.search(r"dropped:\s*([0-9]+)\s*/\s*([0-9]+)\s*\(([0-9]+(?:\.[0-9]+)?)%\)", text)
        if not m2:
            return None, None, None
        return int(m2.group(1)), int(m2.group(2)), float(m2.group(3))

    dropped, dropped_denominator, dropped_pct = grab_dropped()

    # Supports both simplified benchmark output and older output.
    producer_time = grab_float("producer time")
    end_to_end_time = grab_float("end-to-end time")
    old_time = grab_float("time")

    if producer_time is None:
        producer_time = old_time
    if end_to_end_time is None:
        end_to_end_time = old_time

    return {
        "producer_time_ms": producer_time,
        "end_to_end_time_ms": end_to_end_time,
        "attempted": grab_int("attempted"),
        "logged": grab_int("logged"),
        "dropped": dropped,
        "dropped_denominator": dropped_denominator,
        "dropped_pct": dropped_pct,
        "producer_lines_sec": grab_float("producer lines/sec"),
        "end_to_end_lines_sec": grab_float("end-to-end lines/sec"),
        "checksum": grab_checksum(),
    }


def parse_validation(output: str) -> dict:
    out = {}

    m = re.search(r"sync logged \+ dropped =\s*([0-9]+)\s*/\s*([0-9]+)", output)
    if m:
        out["sync_valid_sum"] = int(m.group(1))
        out["sync_valid_expected"] = int(m.group(2))

    m = re.search(r"async logged \+ dropped =\s*([0-9]+)\s*/\s*([0-9]+)", output)
    if m:
        out["async_valid_sum"] = int(m.group(1))
        out["async_valid_expected"] = int(m.group(2))

    return out


def flatten_result(run_id: int, returncode: int, output: str) -> dict:
    sync = parse_section(output, "SyncLogger (mutex + write)")
    async_ = parse_section(output, "AsyncLogger (your logger)")
    valid = parse_validation(output)

    row = {
        "run": run_id,
        "returncode": returncode,
    }

    for k, v in sync.items():
        row[f"sync_{k}"] = v

    for k, v in async_.items():
        row[f"async_{k}"] = v

    row.update(valid)

    return row


def write_summary(results_csv: Path, summary_csv: Path):
    rows = []
    with results_csv.open(newline="") as f:
        reader = csv.DictReader(f)
        rows = list(reader)

    numeric_cols = []
    for col in rows[0].keys():
        if col in ("run", "returncode", "sync_checksum", "async_checksum"):
            continue

        values = []
        ok = True
        for r in rows:
            if r[col] == "" or r[col] is None:
                ok = False
                break
            try:
                values.append(float(r[col]))
            except ValueError:
                ok = False
                break

        if ok:
            numeric_cols.append(col)

    with summary_csv.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["metric", "count", "mean", "median", "stdev", "min", "max"])

        for col in numeric_cols:
            values = [float(r[col]) for r in rows]
            stdev = statistics.stdev(values) if len(values) >= 2 else 0.0
            writer.writerow([
                col,
                len(values),
                statistics.mean(values),
                statistics.median(values),
                stdev,
                min(values),
                max(values),
            ])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=100)
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--out-dir", default="bench_runs")
    ap.add_argument("--no-build", action="store_true")
    args = ap.parse_args()

    repo = Path.cwd()
    build_dir = repo / args.build_dir
    bench_exe = build_dir / "caelogger_bench"

    out_dir = repo / args.out_dir
    raw_dir = out_dir / "raw"
    raw_dir.mkdir(parents=True, exist_ok=True)

    results_csv = out_dir / "bench_results.csv"
    summary_csv = out_dir / "bench_summary.csv"

    if not args.no_build:
        print(f"Building target caelogger_bench in {build_dir}...")
        build = run_cmd(["cmake", "--build", str(build_dir), "--target", "caelogger_bench"])
        if build.returncode != 0:
            print(build.stdout)
            print("Build failed.", file=sys.stderr)
            sys.exit(build.returncode)

    if not bench_exe.exists():
        print(f"Benchmark executable not found: {bench_exe}", file=sys.stderr)
        sys.exit(1)

    rows = []

    for i in range(1, args.runs + 1):
        print(f"Run {i}/{args.runs}...", flush=True)

        p = run_cmd([str(bench_exe)])
        raw_path = raw_dir / f"run_{i:04d}.txt"
        raw_path.write_text(p.stdout)

        row = flatten_result(i, p.returncode, p.stdout)
        rows.append(row)

        async_dropped = row.get("async_dropped")
        async_logged = row.get("async_logged")
        async_attempted = row.get("async_attempted")
        async_e2e = row.get("async_end_to_end_time_ms")

        print(
            f"  rc={p.returncode} "
            f"async_logged={async_logged} "
            f"async_dropped={async_dropped} "
            f"attempted={async_attempted} "
            f"async_e2e_ms={async_e2e}"
        )

    # Stable column order.
    fieldnames = [
        "run",
        "returncode",

        "sync_producer_time_ms",
        "sync_end_to_end_time_ms",
        "sync_attempted",
        "sync_logged",
        "sync_dropped",
        "sync_dropped_denominator",
        "sync_dropped_pct",
        "sync_producer_lines_sec",
        "sync_end_to_end_lines_sec",
        "sync_checksum",

        "async_producer_time_ms",
        "async_end_to_end_time_ms",
        "async_attempted",
        "async_logged",
        "async_dropped",
        "async_dropped_denominator",
        "async_dropped_pct",
        "async_producer_lines_sec",
        "async_end_to_end_lines_sec",
        "async_checksum",

        "sync_valid_sum",
        "sync_valid_expected",
        "async_valid_sum",
        "async_valid_expected",
    ]

    # Include any extra parsed fields safely.
    extra = sorted(set().union(*(r.keys() for r in rows)) - set(fieldnames))
    fieldnames += extra

    with results_csv.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    if rows:
        write_summary(results_csv, summary_csv)

    print()
    print(f"Wrote raw outputs to: {raw_dir}")
    print(f"Wrote results CSV to: {results_csv}")
    print(f"Wrote summary CSV to: {summary_csv}")


if __name__ == "__main__":
    main()