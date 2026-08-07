"""Compare reconstructed volume fields between two OpenFOAM cases."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

try:
    from .openfoam_field_convergence import (
        compare_snapshots,
        exact_time,
        internal_patch_names,
        read_snapshot,
    )
except ImportError:  # Direct execution: python tools/openfoam_cross_case_comparison.py
    from openfoam_field_convergence import (
        compare_snapshots,
        exact_time,
        internal_patch_names,
        read_snapshot,
    )


def reconstructed_snapshot(case: Path, requested_time: float, fields):
    """Read one root checkpoint without depending on processor partitioning."""
    import pyvista as pv
    import vtk

    case = case.expanduser().resolve()
    if not (case / "system" / "controlDict").is_file():
        raise ValueError(f"Not an OpenFOAM case: {case}")
    marker = case / f"{case.name}.foam"
    marker.touch(exist_ok=True)
    vtk.vtkObject.GlobalWarningDisplayOff()
    reader = pv.POpenFOAMReader(str(marker))
    reader.case_type = "reconstructed"
    for field in fields:
        if field not in reader.cell_array_names:
            raise ValueError(
                f"Field {field!r} is unavailable in {case}; choices: "
                + ", ".join(reader.cell_array_names)
            )
        reader.enable_cell_array(field)
    patches = internal_patch_names(reader)
    if patches:
        reader.disable_all_patch_arrays()
        for patch in patches:
            reader.enable_patch_array(patch)
    time = exact_time([float(value) for value in reader.time_values], requested_time)
    return time, read_snapshot(reader, time, fields)


def validate_geometry(reference, sample) -> None:
    """Require identical region/cell ordering before comparing field arrays."""
    import numpy as np

    if set(reference) != set(sample):
        raise ValueError("Reconstructed cases contain different mesh regions")
    for region in sorted(reference):
        reference_centers = reference[region]["center"]
        sample_centers = sample[region]["center"]
        if reference_centers.shape != sample_centers.shape:
            raise ValueError(f"Cell count differs in {region}")
        scale = max(
            1.0,
            float(np.max(np.abs(reference_centers))) if reference_centers.size else 1.0,
        )
        displacement = np.linalg.norm(sample_centers - reference_centers, axis=1)
        if displacement.size and float(np.max(displacement)) > 1.0e-12 * scale:
            raise ValueError(
                f"Cell ordering or geometry differs in {region}; "
                f"maximum centre displacement={float(np.max(displacement)):.9g}"
            )


def maximum_difference(reference, sample, field):
    """Return the owning cell and values for the largest field difference."""
    import numpy as np

    maximum = None
    for region in sorted(reference):
        if field not in reference[region]:
            continue
        reference_values = np.asarray(reference[region][field], dtype=float)
        sample_values = np.asarray(sample[region][field], dtype=float)
        delta = sample_values - reference_values
        magnitude = np.abs(delta) if delta.ndim == 1 else np.linalg.norm(delta, axis=1)
        index = int(np.argmax(magnitude))
        candidate = {
            "difference": float(magnitude[index]),
            "region": region,
            "index": index,
            "center": reference[region]["center"][index],
            "volume": float(reference[region]["volume"][index]),
            "sample": sample_values[index],
            "reference": reference_values[index],
        }
        if maximum is None or candidate["difference"] > maximum["difference"]:
            maximum = candidate
    return maximum


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sample_case", type=Path)
    parser.add_argument("reference_case", type=Path)
    parser.add_argument("--sample-time", type=float, required=True)
    parser.add_argument("--reference-time", type=float, required=True)
    parser.add_argument("--fields", nargs="+", default=("U", "T"))
    parser.add_argument("--csv", type=Path)
    args = parser.parse_args()

    try:
        sample_time, sample = reconstructed_snapshot(
            args.sample_case, args.sample_time, args.fields
        )
        reference_time, reference = reconstructed_snapshot(
            args.reference_case, args.reference_time, args.fields
        )
        validate_geometry(reference, sample)
        rows = compare_snapshots(reference, sample, args.fields)
    except (ImportError, ValueError) as exc:
        raise SystemExit(str(exc)) from exc

    headings = (
        "sample_case", "sample_time", "reference_case", "reference_time",
        "region", "field", "cells", "volume", "mean_absolute", "rms",
        "maximum", "reference_rms", "relative_rms",
    )
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
    if args.csv:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        with args.csv.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=headings)
            writer.writeheader()
            writer.writerows(output_rows)
        print(f"Saved: {args.csv}")

    print("sample -> reference | region | field | RMS | max | relative RMS")
    for row in output_rows:
        if row["region"] == "all":
            print(
                f"{sample_time:.12g} -> {reference_time:.12g} | all | "
                f"{row['field']} | {row['rms']:.8g} | {row['maximum']:.8g} | "
                f"{row['relative_rms']:.6%}"
            )
    for field in args.fields:
        maximum = maximum_difference(reference, sample, field)
        if maximum is None or not math.isfinite(maximum["difference"]):
            continue
        center = ", ".join(f"{value:.9g}" for value in maximum["center"])
        print(
            f"max {field}: difference={maximum['difference']:.9g}, "
            f"region={maximum['region']}, cell={maximum['index']}, "
            f"center=({center}), volume={maximum['volume']:.9g}, "
            f"sample={maximum['sample']}, reference={maximum['reference']}"
        )


if __name__ == "__main__":
    main()
