"""Compare OpenFOAM fields on different meshes by sampling a reference mesh."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

try:
    from .openfoam_field_convergence import (
        exact_time,
        field_error,
        internal_patch_names,
        iter_named_leaves,
    )
except ImportError:  # Direct execution: python tools/openfoam_mesh_comparison.py
    from openfoam_field_convergence import (
        exact_time,
        field_error,
        internal_patch_names,
        iter_named_leaves,
    )


def read_internal_blocks(case: Path, requested_time: float, fields):
    """Read reconstructed internal meshes and retain their PyVista geometry."""
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
    reader.set_active_time_value(time)
    data = reader.read()
    blocks = {}
    for path, block in iter_named_leaves(data):
        if not path or not str(path[-1]).endswith("internalMesh"):
            continue
        key = "/".join(path)
        blocks[key] = block.copy(deep=True)
    if not blocks:
        raise ValueError("The OpenFOAM reader returned no internal meshes")
    return time, blocks


def mesh_summary(block, field):
    """Return volume-weighted mean and extrema without assuming equal cells."""
    import numpy as np

    sized = block.compute_cell_sizes(
        length=False, area=False, volume=True, vertex_count=False
    )
    volumes = np.asarray(sized.cell_data["Volume"], dtype=float)
    values = np.asarray(block.cell_data[field], dtype=float)
    magnitude = np.abs(values) if values.ndim == 1 else np.linalg.norm(values, axis=1)
    total = float(np.sum(volumes))
    if total <= 0.0 or values.shape[0] != volumes.size:
        raise ValueError("Invalid mesh volume or field length")
    if values.ndim == 1:
        mean = float(np.sum(volumes * values) / total)
    else:
        mean = float(np.sum(volumes * magnitude) / total)
    return {
        "cells": int(volumes.size),
        "volume": total,
        "mean": mean,
        "minimum": float(np.min(magnitude)),
        "maximum": float(np.max(magnitude)),
    }


def sampled_error(reference, sample, field):
    """Compare sample cells with the reference cells containing their centres."""
    import numpy as np

    sample_centres = sample.cell_centers().points
    reference_cells = np.asarray(
        reference.find_containing_cell(sample_centres), dtype=int
    )
    valid = reference_cells >= 0
    coverage = float(np.count_nonzero(valid) / valid.size) if valid.size else 0.0
    if coverage < 0.99:
        raise ValueError(
            f"Reference sampling covered only {coverage:.3%} of sample cell centres"
        )
    sampled_reference = np.asarray(reference.cell_data[field], dtype=float)[
        reference_cells[valid]
    ]
    sample_values = np.asarray(sample.cell_data[field], dtype=float)[valid]
    volumes = np.asarray(
        sample.compute_cell_sizes(
            length=False, area=False, volume=True, vertex_count=False
        ).cell_data["Volume"],
        dtype=float,
    )[valid]
    return coverage, field_error(sampled_reference, sample_values, volumes)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sample_case", type=Path)
    parser.add_argument("reference_case", type=Path)
    parser.add_argument("--sample-time", type=float, required=True)
    parser.add_argument("--reference-time", type=float, required=True)
    parser.add_argument("--fields", nargs="+", default=("T", "U", "p"))
    parser.add_argument("--csv", type=Path)
    args = parser.parse_args()

    try:
        sample_time, sample = read_internal_blocks(
            args.sample_case, args.sample_time, args.fields
        )
        reference_time, reference = read_internal_blocks(
            args.reference_case, args.reference_time, args.fields
        )
        if set(sample) != set(reference):
            raise ValueError("Cases contain different internal mesh regions")
        rows = []
        for region in sorted(sample):
            for field in args.fields:
                if field not in sample[region].cell_data or field not in reference[region].cell_data:
                    continue
                coverage, error = sampled_error(reference[region], sample[region], field)
                sample_stats = mesh_summary(sample[region], field)
                reference_stats = mesh_summary(reference[region], field)
                rows.append({
                    "region": region,
                    "field": field,
                    "coverage": coverage,
                    "sample_cells": sample_stats["cells"],
                    "reference_cells": reference_stats["cells"],
                    "sample_volume": sample_stats["volume"],
                    "reference_volume": reference_stats["volume"],
                    "sample_mean": sample_stats["mean"],
                    "reference_mean": reference_stats["mean"],
                    "mean_difference": sample_stats["mean"] - reference_stats["mean"],
                    "sample_maximum": sample_stats["maximum"],
                    "reference_maximum": reference_stats["maximum"],
                    "maximum_difference": sample_stats["maximum"] - reference_stats["maximum"],
                    **error,
                })
    except (ImportError, ValueError) as exc:
        raise SystemExit(str(exc)) from exc

    if args.csv:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        with args.csv.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
            writer.writeheader()
            writer.writerows(rows)
        print(f"Saved: {args.csv}")

    print("region | field | cells sample/ref | mean delta | max delta | sampled RMS | relative RMS | coverage")
    for row in rows:
        print(
            f"{row['region']} | {row['field']} | {row['sample_cells']}/{row['reference_cells']} | "
            f"{row['mean_difference']:.8g} | {row['maximum_difference']:.8g} | "
            f"{row['rms']:.8g} | {row['relative_rms']:.6%} | {row['coverage']:.3%}"
        )


if __name__ == "__main__":
    main()
