"""
Summarise Moonlight Bench reports, one row per variant label.

Each report is the JSON a Vita writes after a benchmark run. Runs that share
a label are averaged, and the min-max spread is shown for the headline
metrics, so run-to-run noise is visible next to the differences.

Usage: compare.py REPORT.json [REPORT.json ...]
"""

import json
import sys
from collections import defaultdict

# (column title, path into the report)
METRICS = [
    ("input lag p50 ms", ("input_lag_ms", "p50")),
    ("input lag p95 ms", ("input_lag_ms", "p95")),
    ("latency est p50 ms", ("latency_estimate_ms", "p50")),
    ("drawn fps", ("frames", "fps_rendered")),
    ("net lost", ("frames", "lost_network")),
    ("skips beyond rate", ("page", "skipped_beyond_rate")),
    ("repeats", ("page", "repeated")),
    ("decode p95 ms", ("decode_ms", "p95")),
    ("hw decode p50 ms", ("hw_decode_ms", "p50")),
    ("assemble p50 ms", ("assemble_ms", "p50")),
    ("render p95 ms", ("render_ms", "p95")),
    ("vblank wait p50 ms", ("vblank_wait_ms", "p50")),
    ("vblank wait p95 ms", ("vblank_wait_ms", "p95")),
    ("replaced", ("frames", "replaced_before_shown")),
    ("Mbps", ("bitrate_mbps",)),
    ("rssi %", ("wifi", "rssi_avg_percent")),
]


def lookup(report: dict, path: tuple[str, ...]) -> float | None:
    """
    Return the number at path in report, or None if it isn't there.
    """
    value = report
    for key in path:
        if not isinstance(value, dict) or key not in value:
            return None
        value = value[key]
    return float(value) if isinstance(value, (int, float)) else None


def format_cell(values: list[float]) -> str:
    """
    Return the mean, with the min-max spread when there is more than one run.
    """
    if not values:
        return "-"
    mean = sum(values) / len(values)
    if len(values) == 1 or max(values) == min(values):
        return f"{mean:.1f}"
    return f"{mean:.1f} ({min(values):.1f}-{max(values):.1f})"


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    by_label: dict[str, list[dict]] = defaultdict(list)
    failures = []
    for path in sys.argv[1:]:
        try:
            with open(path, encoding="utf-8") as f:
                report = json.load(f)
        except (OSError, json.JSONDecodeError) as error:
            failures.append(f"{path}: unreadable report ({error})")
            continue
        label = report.get("label") or "(no label)"
        if not report.get("ok", False):
            failures.append(f"{path}: {report.get('error', 'unknown error')}")
            continue
        by_label[label].append(report)

    headers = ["variant", "runs"] + [title for title, _ in METRICS]
    rows = []
    for label, reports in by_label.items():
        cells = [label, str(len(reports))]
        for _, path in METRICS:
            values = [v for v in (lookup(r, path) for r in reports) if v is not None]
            cells.append(format_cell(values))
        rows.append(cells)

    if rows:
        print("| " + " | ".join(headers) + " |")
        print("|" + "|".join("---" for _ in headers) + "|")
        for row in rows:
            print("| " + " | ".join(row) + " |")
        print()
        print("Mean over runs, with the min-max spread in brackets. Lower is better except drawn fps, Mbps and rssi.")
    if failures:
        print()
        print("Failed runs:")
        for failure in failures:
            print(f"  {failure}")
    return 0 if rows else 1


if __name__ == "__main__":
    sys.exit(main())
