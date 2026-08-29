"""Compare component and whole-case temperatures between OpenFOAM cases."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

try:
    from .openfoam_cross_case_comparison import (
        reconstructed_snapshot,
        validate_geometry,
    )
    from .openfoam_field_convergence import field_error
except ImportError:  # Direct execution from the repository root.
    from openfoam_cross_case_comparison import (
        reconstructed_snapshot,
        validate_geometry,
    )
    from openfoam_field_convergence import field_error


CSV_HEADINGS = (
    "sample_case",
    "sample_time",
    "reference_case",
    "reference_time",
    "region",
    "cells",
    "volume_m3",
    "reference_mean_K",
    "sample_mean_K",
    "mean_delta_K",
    "reference_peak_K",
    "sample_peak_K",
    "peak_delta_K",
    "cellwise_rms_K",
    "cellwise_max_abs_K",
)


def _temperature_row(region, reference_values, sample_values, volumes):
    import numpy as np

    reference_values = np.asarray(reference_values, dtype=float)
    sample_values = np.asarray(sample_values, dtype=float)
    volumes = np.asarray(volumes, dtype=float)
    metrics = field_error(reference_values, sample_values, volumes)
    volume = float(np.sum(volumes))
    reference_mean = float(np.sum(volumes * reference_values) / volume)
    sample_mean = float(np.sum(volumes * sample_values) / volume)
    reference_peak = float(np.max(reference_values))
    sample_peak = float(np.max(sample_values))
    return {
        "region": region,
        "cells": metrics["cells"],
        "volume_m3": volume,
        "reference_mean_K": reference_mean,
        "sample_mean_K": sample_mean,
        "mean_delta_K": sample_mean - reference_mean,
        "reference_peak_K": reference_peak,
        "sample_peak_K": sample_peak,
        "peak_delta_K": sample_peak - reference_peak,
        "cellwise_rms_K": metrics["rms"],
        "cellwise_max_abs_K": metrics["maximum"],
    }


def temperature_summary_rows(reference, sample):
    """Return volume-weighted temperature summaries by region and in aggregate."""
    import numpy as np

    validate_geometry(reference, sample)
    reference_regions = {
        region for region, values in reference.items() if "T" in values
    }
    sample_regions = {
        region for region, values in sample.items() if "T" in values
    }
    if reference_regions != sample_regions:
        raise ValueError(
            "Regions owning T changed from "
            f"{sorted(reference_regions)} to {sorted(sample_regions)}"
        )

    rows = []
    all_reference = []
    all_sample = []
    all_volume = []
    for region in sorted(reference_regions):
        reference_values = reference[region]
        sample_values = sample[region]
        rows.append(
            _temperature_row(
                region,
                reference_values["T"],
                sample_values["T"],
                reference_values["volume"],
            )
        )
        all_reference.append(reference_values["T"])
        all_sample.append(sample_values["T"])
        all_volume.append(reference_values["volume"])
    rows.append(
        _temperature_row(
            "all",
            np.concatenate(all_reference),
            np.concatenate(all_sample),
            np.concatenate(all_volume),
        )
    )
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sample_case", type=Path)
    parser.add_argument("reference_case", type=Path)
    parser.add_argument("--sample-time", type=float, required=True)
    parser.add_argument("--reference-time", type=float, required=True)
    parser.add_argument("--csv", type=Path, required=True)
    args = parser.parse_args()

    try:
        sample_time, sample = reconstructed_snapshot(
            args.sample_case, args.sample_time, ("T",)
        )
        reference_time, reference = reconstructed_snapshot(
            args.reference_case, args.reference_time, ("T",)
        )
        rows = temperature_summary_rows(reference, sample)
    except (ImportError, ValueError) as exc:
        raise SystemExit(str(exc)) from exc

    output_rows = [
        {
            "sample_case": str(args.sample_case.resolve()),
            "sample_time": sample_time,
            "reference_case": str(args.reference_case.resolve()),
            "reference_time": reference_time,
            **row,
        }
        for row in rows
    ]
    args.csv.parent.mkdir(parents=True, exist_ok=True)
    with args.csv.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=CSV_HEADINGS)
        writer.writeheader()
        writer.writerows(output_rows)

    worst_average = max(output_rows, key=lambda row: abs(row["mean_delta_K"]))
    worst_cell = max(output_rows, key=lambda row: row["cellwise_max_abs_K"])
    print(f"Saved: {args.csv}")
    print(
        "largest region-average change: "
        f"{worst_average['region']} {worst_average['mean_delta_K']:+.9g} K"
    )
    print(
        "largest cellwise change: "
        f"{worst_cell['region']} {worst_cell['cellwise_max_abs_K']:.9g} K"
    )


if __name__ == "__main__":
    main()
