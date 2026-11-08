#!/usr/bin/env python3
"""Extract bounded, credential-free campaign series and render reproducible figures.

Extraction uses only the standard library and reads completed campaign files.
Plotting requires matplotlib; it never connects to ROS, Docker or a receiver.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys
from zoneinfo import ZoneInfo


SCHEMA_VERSION = 1
DIAGNOSTIC_COLUMNS = [
    "utc_second", "position_age_max_s", "transport_age_max_s", "rtcm_age_max_s",
    "ntrip_published_frames_max", "ntrip_streaming_min",
]
FUNCTIONAL_COLUMNS = [
    "utc", "monotonic_s", "position_sequence", "sequence_advance_per_probe_second_hz",
    "position_age_s", "rtk_mode",
]
RESOURCE_COLUMNS = ["utc", "cpu_percent_one_core", "rss_kib"]


def utc_seconds(value: str) -> float:
    timestamp = dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
    if timestamp.tzinfo is None:
        raise ValueError("timestamp must have an explicit timezone")
    return timestamp.timestamp()


def number(value):
    if value is None:
        return None
    result = float(value)
    return result if math.isfinite(result) else None


def summary(values):
    values = sorted(values)
    return {"n": len(values), "min": values[0], "median": statistics.median(values),
            "p95": values[round((len(values)-1)*.95)], "max": values[-1]}


def position_rate(previous: dict | None, current: dict) -> float | None:
    if previous is None:
        return None
    left = previous["ros"]["status"]
    right = current["ros"]["status"]
    identity = (right.get("source_id"), right.get("source_incarnation"))
    if not all(identity) or identity != (left.get("source_id"), left.get("source_incarnation")):
        return None
    elapsed = current["monotonic_seconds"] - previous["monotonic_seconds"]
    advance = right["position_observation_sequence"] - left["position_observation_sequence"]
    return advance / elapsed if elapsed > 0 and advance >= 0 else None


def records(campaign: Path, relative: str, expected: dict, sources: list):
    digest = hashlib.sha256()
    size = 0
    with (campaign / relative).open("rb") as stream:
        for line in stream:
            digest.update(line)
            size += len(line)
            if line.strip():
                yield json.loads(line)
    actual = {"path": relative, "sha256": digest.hexdigest(), "bytes": size}
    if actual != expected[relative]:
        raise ValueError(f"evidence identity mismatch: {relative}")
    sources.append(actual)


def diagnostic_values(array: dict, name: str) -> dict:
    for item in array.get("status", []):
        if item.get("name") == name:
            return {entry["key"]: entry["value"] for entry in item.get("values", [])}
    return {}


def combine(row: list, index: int, value, *, minimum=False):
    value = number(value)
    if value is not None:
        row[index] = value if row[index] is None else (
            min(row[index], value) if minimum else max(row[index], value)
        )


def extract(campaign: Path) -> dict:
    metrics_path = campaign / "final-metrics.json"
    metrics_bytes = metrics_path.read_bytes()
    metrics = json.loads(metrics_bytes)
    expected = {entry["path"]: entry for entry in metrics["manifest"]}
    sources = []
    functional, previous = [], None
    for sample in records(campaign, "functional/samples.jsonl", expected, sources):
        status = sample["ros"]["status"]
        age = diagnostic_values(sample["ros"].get("diagnostics", {}),
                                "universal_gnss/position_payload_freshness")
        functional.append([
            utc_seconds(sample["timestamp_utc"]), sample["monotonic_seconds"],
            status["position_observation_sequence"], position_rate(previous, sample),
            number(age.get("position_observation_age_s")), status["rtk_mode"],
        ])
        previous = sample

    bins, transitions = {}, []
    previous = first = last = None
    fix_types, identities = set(), set()
    ages = {"position_observation_age_s": [], "transport_rx_age_s": [],
            "rtcm_frame_age_s": [], "publication_gap_s": []}
    counts = {"publications": 0, "same_position_sequence": 0,
              "same_position_sequence_and_runtime_stamp": 0,
              "same_position_sequence_new_runtime_stamp": 0,
              "publications_with_new_position": 0, "accepted_sequence_advance": 0,
              "sequence_regressions": 0, "identity_changes": 0,
              "invalid_fix_publications": 0}
    for record in records(campaign, "continuous-gnss.jsonl", expected, sources):
        when = utc_seconds(record["received_utc"])
        if record["type"] == "gnss_status_publication":
            fix_types.add(record["fix_type"])
            identities.add((record["source_id"], record["source_incarnation"]))
            counts["publications"] += 1
            counts["invalid_fix_publications"] += not record["fix_valid"]
            if first is None:
                first = [when, record["rtk_mode"]]
            last = [when, record["rtk_mode"]]
            if previous:
                if record["received_monotonic"] <= previous["received_monotonic"]:
                    raise ValueError("non-increasing publication monotonic clock")
                ages["publication_gap_s"].append(record["received_monotonic"] - previous["received_monotonic"])
                identity = (record["source_id"], record["source_incarnation"])
                if identity != (previous["source_id"], previous["source_incarnation"]):
                    counts["identity_changes"] += 1
                else:
                    advance = record["position_observation_sequence"] - previous["position_observation_sequence"]
                    if advance > 0:
                        counts["publications_with_new_position"] += 1
                        counts["accepted_sequence_advance"] += advance
                    elif advance < 0:
                        counts["sequence_regressions"] += 1
                    else:
                        counts["same_position_sequence"] += 1
                        same_stamp = record["runtime_receipt_stamp_ns"] == previous["runtime_receipt_stamp_ns"]
                        key = ("same_position_sequence_and_runtime_stamp" if same_stamp
                               else "same_position_sequence_new_runtime_stamp")
                        counts[key] += 1
                if record["rtk_mode"] != previous["rtk_mode"]:
                    transitions.append([when, previous["rtk_mode"], record["rtk_mode"]])
            previous = record
        elif record["type"] == "gnss_diagnostics":
            second = math.floor(when)
            row = bins.setdefault(second, [second, None, None, None, None, None])
            ntrip_array, streaming = False, False
            for diagnostic in record["status"]:
                name, values = diagnostic["name"], diagnostic["values"]
                if name == "universal_gnss/position_payload_freshness":
                    combine(row, 1, values.get("position_observation_age_s"))
                    combine(row, 2, values.get("transport_rx_age_s"))
                    for key in ["position_observation_age_s", "transport_rx_age_s"]:
                        value = number(values.get(key))
                        if value is not None:
                            ages[key].append(value)
                if name == "universal_gnss_ntrip/rtcm_forwarding":
                    combine(row, 3, values.get("last_frame_age_s"))
                    combine(row, 4, values.get("published_frame_count"))
                    value = number(values.get("last_frame_age_s"))
                    if value is not None:
                        ages["rtcm_frame_age_s"].append(value)
                if name.startswith("universal_gnss_ntrip/"):
                    ntrip_array = True
                streaming |= name == "universal_gnss_ntrip/ntrip_streaming"
            if ntrip_array:
                combine(row, 5, int(streaming), minimum=True)

    if first is None:
        raise ValueError("continuous status capture is empty")
    if counts["identity_changes"] or counts["sequence_regressions"]:
        raise ValueError("source replacement/regression requires separately segmented plots")
    if sorted({identity[0] for identity in identities}) != metrics["continuous"]["source_ids"]:
        raise ValueError("source ID mismatch")
    if sorted({identity[1] for identity in identities}) != metrics["continuous"]["source_incarnations"]:
        raise ValueError("source incarnation mismatch")
    for key, value in counts.items():
        if value != metrics["continuous"]["counts"].get(key, 0):
            raise ValueError(f"publication accounting mismatch: {key}")
    if len(functional) != metrics["functional"]["samples"]:
        raise ValueError("functional sample count mismatch")
    if transitions != [[utc_seconds(item["utc"]), item["from"], item["to"]]
                       for item in metrics["continuous"]["rtk_transitions"]]:
        raise ValueError("RTK transition mismatch")
    verified_statistics = {key: summary(values) for key, values in ages.items()}
    for key, value in verified_statistics.items():
        if value != metrics["continuous"][key]:
            raise ValueError(f"raw age/interval statistics mismatch: {key}")

    resources = {}
    for record in records(campaign, "live-analysis.jsonl", expected, sources):
        when = utc_seconds(record["utc"])
        for role, cost in record.get("cost", {}).items():
            rss = cost.get("rss")
            resources.setdefault(role, []).append([
                when, number(cost.get("cpu_percent_one_core")),
                number(rss.split()[0]) if rss else None,
            ])
    diagnostics = [bins.get(key, [key, None, None, None, None, None])
                   for key in range(min(bins), max(bins) + 1)]
    return {
        "schema_version": SCHEMA_VERSION,
        "timestamp_domain": "UTC Unix seconds; display Europe/Paris",
        "rate_domain": "position sequence delta / monotonic probe interval, within source/incarnation",
        "aggregation": "diagnostics: per UTC second maxima; streaming: minimum of observed NTRIP-array flags; null means unavailable",
        "functional_columns": FUNCTIONAL_COLUMNS, "functional": functional,
        "diagnostic_columns": DIAGNOSTIC_COLUMNS, "diagnostic_seconds": diagnostics,
        "resource_columns": RESOURCE_COLUMNS, "resources": resources,
        "rtk": {"first": first, "last": last, "transitions": transitions},
        "verified_counts": counts,
        "observed_fix_types": sorted(fix_types), "verified_statistics": verified_statistics,
        "sources": sorted(sources, key=lambda item: item["path"]),
        "metrics_sha256": hashlib.sha256(metrics_bytes).hexdigest(),
        "limitations": [
            "No raw coordinates, receiver settings or credentials are exported.",
            "Best-effort DDS is not lossless serial capture; exact physical switch times are unknown.",
            "No interpolation before periodic RSS measurements; RSS contains shared pages.",
            "CPU is percent of one core and excludes transient collector children/daemon work.",
        ],
    }


def render(series: dict, output: Path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.dates as dates
    import matplotlib.pyplot as plt

    if series.get("schema_version") != SCHEMA_VERSION:
        raise ValueError("unsupported plotting-series schema")
    output.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({"svg.hashsalt": "universal-gnss-20261108", "font.size": 10,
                         "axes.grid": True, "grid.alpha": .25})
    paris = ZoneInfo("Europe/Paris")
    session_start = dt.datetime.fromtimestamp(series["functional"][0][0], paris)
    session_date = session_start.date()
    offset = session_start.strftime("%z")
    zone_label = f"Europe/Paris (UTC{offset[:3]}:{offset[3:]})"

    def times(rows):
        return [dt.datetime.fromtimestamp(row[0], dt.timezone.utc) for row in rows]

    def values(rows, index, scale=1):
        return [float("nan") if row[index] is None else row[index] * scale for row in rows]

    def finish(fig, axes, name):
        for axis in axes:
            axis.xaxis.set_major_formatter(dates.DateFormatter("%H:%M:%S", tz=paris))
            axis.set_xlabel(f"{session_date} — {zone_label}")
        fig.tight_layout()
        fig.savefig(output / name, metadata={"Date": None,
                    "Creator": f"Universal GNSS campaign plotter; matplotlib {matplotlib.__version__}"})
        svg = output / name
        svg.write_text("\n".join(line.rstrip() for line in svg.read_text().splitlines()) + "\n")
        plt.close(fig)

    functional = series["functional"]
    diagnostic = series["diagnostic_seconds"]
    fig, axes = plt.subplots(2, 1, figsize=(11, 7), sharex=True)
    axes[0].plot(times(functional), values(functional, 3), color="#146c43", linewidth=1)
    axes[0].set_ylabel("Sequence advance /\nprobe interval (Hz)")
    axes[0].set_title("Accepted position progression — 20 s functional probes")
    axes[1].scatter(times(functional), values(functional, 4), s=7, label="20 s snapshot age")
    axes[1].plot(times(diagnostic), values(diagnostic, 1), linewidth=.7,
                 color="#c15a00", label="Continuous diagnostics: 1 s maximum")
    axes[1].set_ylabel("Position observation age (s)")
    axes[1].legend(loc="upper right", fontsize=8)
    finish(fig, axes, "gnss-observations.svg")

    rtk = series["rtk"]
    steps = [rtk["first"]] + [[row[0], row[2]] for row in rtk["transitions"]] + [rtk["last"]]
    fig, axes = plt.subplots(2, 1, figsize=(11, 7))
    axes[0].scatter(times(functional), values(functional, 5), s=5, color="#839496", label="20 s probes")
    for axis in axes:
        axis.step(times(steps), values(steps, 1), where="post", color="#146c43", label="Continuous received state")
        valid_3d = series["observed_fix_types"] == [7] and not series["verified_counts"]["invalid_fix_publications"]
        axis.set_yticks([1, 2, 3], ["No RTK\n(valid 3D)" if valid_3d else "No RTK", "Float", "Fixed"])
        axis.set_ylim(.8, 3.2)
    axes[0].legend(loc="lower left", fontsize=8)
    continuous_start = dt.datetime.fromtimestamp(rtk["first"][0], paris).strftime("%H:%M")
    axes[0].set_title(f"Observed RTK state; continuous capture starts at {continuous_start} Paris")
    if rtk["transitions"]:
        start, end = rtk["transitions"][0][0], rtk["transitions"][-1][0]
        axes[1].set_xlim(dt.datetime.fromtimestamp(start-5, dt.timezone.utc),
                         dt.datetime.fromtimestamp(end+5, dt.timezone.utc))
        if rtk["transitions"][0][1] == 3 and rtk["transitions"][-1][2] == 3:
            axes[1].set_title(f"Observed degradation → Fixed: {end-start:.3f} s (received-state timestamps)")
        else:
            axes[1].set_title("Observed RTK transitions — received-state timestamps")
    finish(fig, axes, "gnss-rtk-recovery.svg")

    fig, axes = plt.subplots(3, 1, figsize=(11, 9), sharex=True)
    frame_values = [row[4] for row in diagnostic if row[4] is not None]
    origin = frame_values[0]
    if any(right < left for left, right in zip(frame_values, frame_values[1:])):
        raise ValueError("NTRIP counter reset requires segmented analysis")
    axes[0].plot(times(diagnostic), [float("nan") if row[4] is None else row[4]-origin for row in diagnostic], color="#146c43")
    axes[0].set_ylabel("NTRIP published frames\n(relative counter)")
    axes[0].set_title("Correction observability — 1 s aggregates of continuous diagnostics")
    axes[1].plot(times(diagnostic), values(diagnostic, 3), color="#c15a00", linewidth=.8)
    axes[1].set_ylabel("Last-frame age (s)\nmaximum per second")
    axes[2].step(times(diagnostic), values(diagnostic, 5), where="post", color="#146c43")
    axes[2].set_ylim(-.1, 1.1)
    axes[2].set_yticks([0, 1], ["Other reported state", "Streaming observed"])
    axes[2].set_ylabel("NTRIP diagnostic flag")
    finish(fig, axes, "gnss-corrections.svg")

    resources = series["resources"]
    fig, axes = plt.subplots(3, 1, figsize=(11, 9), sharex=True)
    for role in ["collector", "monitor", "continuous_listener"]:
        rows = resources.get(role, [])
        axes[0].plot(times(rows), values(rows, 1), label=role)
    for role in ["receiver", "ntrip"]:
        rows = resources.get(role, [])
        axes[1].plot(times(rows), values(rows, 1), label=role)
    for role, rows in resources.items():
        axes[2].plot(times(rows), values(rows, 2, 1/1024), label=role)
    axes[0].set_title("Observed process cost; CPU denominator = one core")
    axes[0].set_ylabel("Observer CPU (%)")
    axes[1].set_ylabel("Receiver/NTRIP CPU (%)")
    axes[2].set_ylabel("Per-process RSS (MiB)")
    axes[2].set_title("RSS: 27 late readings only; shared pages, no unique-RAM sum")
    first_rss = min(row[0] for rows in resources.values() for row in rows if row[2] is not None)
    axes[2].axvspan(times(functional)[0], dt.datetime.fromtimestamp(first_rss, dt.timezone.utc),
                   color="#dddddd", alpha=.5, label="No periodic RSS measurement")
    for axis in axes:
        axis.legend(fontsize=7, loc="upper left", ncol=2)
    finish(fig, axes, "gnss-resources.svg")

    names = ["gnss-observations.svg", "gnss-rtk-recovery.svg",
             "gnss-corrections.svg", "gnss-resources.svg"]
    manifest = {
        "schema_version": 1, "python_version": sys.version.split()[0],
        "matplotlib_version": matplotlib.__version__,
        "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "canonical_series_sha256": hashlib.sha256(json.dumps(
            series, sort_keys=True, separators=(",", ":"), allow_nan=False,
        ).encode()).hexdigest(),
        "figures": [{"path": name, "sha256": hashlib.sha256((output/name).read_bytes()).hexdigest(),
                     "bytes": (output/name).stat().st_size} for name in names],
        "determinism": "fixed SVG hash salt; no creation date; trailing whitespace normalized; compare within recorded rendering environment",
    }
    (output / "render-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    extraction = sub.add_parser("extract", help="verify raw hashes and export non-secret plot series")
    extraction.add_argument("campaign", type=Path)
    extraction.add_argument("--output", default="-", help="JSON destination; - for stdout")
    plotting = sub.add_parser("plot", help="render four SVGs from exported series")
    plotting.add_argument("series", type=Path)
    plotting.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    if args.command == "extract":
        payload = json.dumps(extract(args.campaign), separators=(",", ":"), allow_nan=False) + "\n"
        if args.output == "-":
            sys.stdout.write(payload)
        else:
            Path(args.output).write_text(payload, encoding="utf-8")
    else:
        render(json.loads(args.series.read_text()), args.output_dir)


if __name__ == "__main__":
    main()
