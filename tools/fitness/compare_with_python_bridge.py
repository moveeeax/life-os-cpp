#!/usr/bin/env python3
"""Compare the C++ service data against the Python bridge SQLite.

Input: the bridge SQLite file and a directory of CSV exports from Postgres
(see the query headers in docs/parity-report-2026-09.md). Times in the CSV
are epoch seconds (EXTRACT(EPOCH ...)::bigint); in SQLite they are ISO
strings with an offset, parsed by datetime.fromisoformat.

Only the intersection is compared: records later than the --until cutoff
(the moment of the bridge's last sync) are not reported as mismatches, the
bridge has none of them by construction. Everything else is compared without
tolerance; floating-point fields use an epsilon of 1e-6 for float rounding,
which is not a data tolerance.

Exit 0: no mismatches, 1: mismatches found; the report is printed to stdout.
"""

import argparse
import csv
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path

EPS = 1e-6


def iso_epoch(value: str) -> int:
    return int(datetime.fromisoformat(value).timestamp())


def feq(a, b) -> bool:
    if a is None and b is None:
        return True
    if a is None or b is None:
        return False
    return abs(float(a) - float(b)) <= EPS


def load_csv(path: Path):
    with path.open(newline="") as fh:
        return list(csv.DictReader(fh))


def opt_float(v):
    return None if v in (None, "") else float(v)


def opt_int(v):
    return None if v in (None, "") else int(v)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--sqlite", required=True, type=Path)
    ap.add_argument("--pg-dir", required=True, type=Path)
    ap.add_argument("--until", required=True,
                    help="ISO cutoff with offset: the moment of the bridge's last sync")
    args = ap.parse_args()

    cutoff = int(datetime.fromisoformat(args.until).timestamp())
    br = sqlite3.connect(f"file:{args.sqlite}?mode=ro", uri=True)
    br.row_factory = sqlite3.Row
    issues = []
    explained = []
    sections = []

    def note(kind, text):
        (issues if kind == "issue" else explained).append(text)

    # --- daily_activity: steps, distance, kcal per day -----------------------
    cutoff_date = datetime.fromtimestamp(cutoff, tz=timezone.utc).date().isoformat()
    b_act = {(r["date"], r["device_id"] or ""): r for r in br.execute(
        "SELECT date, device_id, steps, distance_m, active_kcal, total_kcal "
        "FROM daily_activity")}
    c_act = {(r["date"], r["device_id"]): r
             for r in load_csv(args.pg_dir / "daily_activity.csv")}
    checked = 0
    for key in sorted(set(b_act) | set(c_act)):
        date = key[0]
        if date > cutoff_date:
            explained.append(f"activity {key}: later than bridge cutoff")
            continue
        if key not in c_act:
            issues.append(f"activity {key}: in bridge, missing in Postgres")
            continue
        if key not in b_act:
            issues.append(f"activity {key}: in Postgres, missing in bridge")
            continue
        b, c = b_act[key], c_act[key]
        checked += 1
        if int(b["steps"]) != int(c["steps"]):
            issues.append(f"activity {key}: steps {b['steps']} != {c['steps']}")
        for f in ("distance_m", "active_kcal", "total_kcal"):
            bv, cv = b[f], opt_float(c[f])
            if bv in (None, 0) and cv is None:
                continue  # bridge zero = port NULL: the "zero means NULL" rule
            if not feq(bv, cv):
                issues.append(f"activity {key}: {f} {bv} != {cv}")
    sections.append(f"daily_activity: checked days {checked}, "
                    f"bridge {len(b_act)}, port {len(c_act)}")

    # --- sleep_sessions: bounds, duration, score, provenance -----------------
    b_sleep = {r["sleep_id"]: r for r in br.execute(
        "SELECT sleep_id, start_at, end_at, duration_minutes, sleep_score, "
        "sleep_score_source FROM sleep_sessions")}
    c_sleep = {r["sleep_id"]: r
               for r in load_csv(args.pg_dir / "sleep_sessions.csv")}
    checked = 0
    for sid in sorted(set(b_sleep) | set(c_sleep)):
        if sid not in c_sleep:
            issues.append(f"sleep {sid}: in bridge, missing in Postgres")
            continue
        if sid not in b_sleep:
            c = c_sleep[sid]
            if int(c["start_epoch"]) > cutoff:
                explained.append(f"sleep {sid}: later than bridge cutoff")
            else:
                issues.append(f"sleep {sid}: in Postgres, missing in bridge")
            continue
        b, c = b_sleep[sid], c_sleep[sid]
        checked += 1
        if iso_epoch(b["start_at"]) != int(c["start_epoch"]):
            issues.append(f"sleep {sid}: start {b['start_at']} != epoch {c['start_epoch']}")
        if iso_epoch(b["end_at"]) != int(c["end_epoch"]):
            issues.append(f"sleep {sid}: end {b['end_at']} != epoch {c['end_epoch']}")
        if int(b["duration_minutes"]) != int(c["duration_minutes"]):
            issues.append(f"sleep {sid}: duration {b['duration_minutes']} != {c['duration_minutes']}")
        if opt_int(b["sleep_score"]) != opt_int(c["sleep_score"]):
            issues.append(f"sleep {sid}: score {b['sleep_score']} != {c['sleep_score']}")
        if (b["sleep_score_source"] or "") != (c["sleep_score_source"] or ""):
            issues.append(f"sleep {sid}: score_source {b['sleep_score_source']!r} != {c['sleep_score_source']!r}")
    sections.append(f"sleep_sessions: checked sessions {checked}, "
                    f"bridge {len(b_sleep)}, port {len(c_sleep)}")

    # --- body_measurements: weight per measurement ---------------------------
    b_body = {(iso_epoch(r["timestamp"]), r["device_id"] or ""): r["weight_kg"]
              for r in br.execute(
                  "SELECT timestamp, device_id, weight_kg FROM body_measurements")}
    c_body = {(int(r["epoch"]), r["device_id"]): float(r["weight_kg"])
              for r in load_csv(args.pg_dir / "body_measurements.csv")}
    checked = 0
    for key in sorted(set(b_body) | set(c_body)):
        if key not in c_body:
            issues.append(f"body {key}: in bridge, missing in Postgres")
            continue
        if key not in b_body:
            if key[0] > cutoff:
                explained.append(f"body {key}: later than bridge cutoff")
            else:
                issues.append(f"body {key}: in Postgres, missing in bridge")
            continue
        checked += 1
        if not feq(b_body[key], c_body[key]):
            issues.append(f"body {key}: weight {b_body[key]} != {c_body[key]}")
    sections.append(f"body_measurements: checked measurements {checked}, "
                    f"bridge {len(b_body)}, port {len(c_body)}")

    # --- samples: full set comparison up to the cutoff -----------------------
    def compare_samples(name, bridge_sql, csv_name, key_of_bridge, key_of_csv):
        b_set = {key_of_bridge(r) for r in br.execute(bridge_sql)}
        c_all = load_csv(args.pg_dir / csv_name)
        c_set = {key_of_csv(r) for r in c_all}
        b_cut = {k for k in b_set if k[0] <= cutoff}
        c_cut = {k for k in c_set if k[0] <= cutoff}
        for k in sorted(b_cut - c_cut)[:20]:
            issues.append(f"{name} {k}: in bridge, missing in Postgres")
        missing = len(b_cut - c_cut)
        extra = len(c_cut - b_cut)
        if missing > 20:
            issues.append(f"{name}: ... and {missing - 20} more bridge records missing in Postgres")
        for k in sorted(c_cut - b_cut)[:20]:
            issues.append(f"{name} {k}: in Postgres, missing in bridge")
        if extra > 20:
            issues.append(f"{name}: ... and {extra - 20} more extra records in Postgres")
        later = len(c_set) - len(c_cut)
        if later:
            explained.append(f"{name}: {later} Postgres records later than bridge cutoff")
        sections.append(f"{name}: up to cutoff bridge {len(b_cut)}, port {len(c_cut)}, "
                        f"matched {len(b_cut & c_cut)}")

    compare_samples(
        "heart_rate",
        "SELECT timestamp, bpm, sample_type FROM heart_rate_samples",
        "heart_rate_samples.csv",
        lambda r: (iso_epoch(r["timestamp"]), int(r["bpm"]), r["sample_type"]),
        lambda r: (int(r["epoch"]), int(r["bpm"]), r["sample_type"]))
    compare_samples(
        "stress",
        "SELECT timestamp, stress_score FROM stress_samples",
        "stress_samples.csv",
        lambda r: (iso_epoch(r["timestamp"]), int(r["stress_score"])),
        lambda r: (int(r["epoch"]), int(r["stress_score"])))
    compare_samples(
        "spo2",
        "SELECT timestamp, spo2_pct FROM spo2_samples",
        "spo2_samples.csv",
        lambda r: (iso_epoch(r["timestamp"]), int(r["spo2_pct"])),
        lambda r: (int(r["epoch"]), int(r["spo2_pct"])))
    compare_samples(
        "workouts",
        "SELECT start_at, workout_id FROM workouts",
        "workouts.csv",
        lambda r: (iso_epoch(r["start_at"]), r["workout_id"]),
        lambda r: (int(r["start_epoch"]), r["workout_id"]))
    compare_samples(
        "abnormal_heart_beat",
        "SELECT start_at, event_id FROM abnormal_heart_beat_events",
        "abnormal_heart_beat_events.csv",
        lambda r: (iso_epoch(r["start_at"]), r["event_id"]),
        lambda r: (int(r["start_epoch"]), r["event_id"]))

    print("=== Summary ===")
    for line in sections:
        print(" ", line)
    print(f"\n=== Explained differences: {len(explained)} ===")
    for line in explained[:15]:
        print(" ", line)
    if len(explained) > 15:
        print(f"  ... and {len(explained) - 15} more")
    print(f"\n=== Mismatches: {len(issues)} ===")
    for line in issues:
        print(" ", line)
    return 1 if issues else 0


if __name__ == "__main__":
    sys.exit(main())
