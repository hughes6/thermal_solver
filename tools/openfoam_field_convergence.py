"""Compare volume fields between written times in a parallel OpenFOAM case."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path


def iter_named_leaves(dataset, path=()):
    """Yield named non-empty leaves from a nested PyVista multiblock."""
    if dataset is None:
        return
    if hasattr(dataset, "n_blocks"):
        names = list(dataset.keys()) if hasattr(dataset, "keys") else []
        for index, block in enumerate(dataset):
            name = names[index] if index < len(names) else f"block_{index}"
            yield from iter_named_leaves(block, path + (str(name),))
    elif getattr(dataset, "n_cells", 0):
        yield path, dataset


def field_error(reference, sample, volumes):
    """Return volume-weighted error metrics for scalar or vector cell data."""
    import numpy as np

    reference = np.asarray(reference, dtype=float)
    sample = np.asarray(sample, dtype=float)
    volumes = np.asarray(volumes, dtype=float).reshape(-1)
    if reference.shape != sample.shape:
        raise ValueError(
            f"Field shape changed from {reference.shape} to {sample.shape}"
        )
    if reference.shape[0] != volumes.size:
        raise ValueError("Field and volume cell counts differ")
    if reference.ndim == 1:
        difference = np.abs(sample - reference)
        reference_magnitude = np.abs(reference)
    elif reference.ndim == 2:
        difference = np.linalg.norm(sample - reference, axis=1)
        reference_magnitude = np.linalg.norm(reference, axis=1)
    else:
        raise ValueError(f"Unsupported field rank: {reference.ndim}")
    total_volume = float(np.sum(volumes))
    if not math.isfinite(total_volume) or total_volume <= 0.0:
        raise ValueError("Cell volumes must have a positive finite sum")
    mean_absolute = float(np.sum(volumes * difference) / total_volume)
    rms = float(np.sqrt(np.sum(volumes * difference**2) / total_volume))
    reference_rms = float(
        np.sqrt(np.sum(volumes * reference_magnitude**2) / total_volume)
    )
    return {
        "cells": int(volumes.size),
        "volume": total_volume,
        "mean_absolute": mean_absolute,
        "rms": rms,
        "maximum": float(np.max(difference)) if difference.size else 0.0,
        "reference_rms": reference_rms,
        "relative_rms": rms / reference_rms if reference_rms > 1.0e-30 else math.nan,
    }


def internal_patch_names(reader):
    names = [str(name) for name in reader.patch_array_names]
    return [
        name for name in names
        if name == "internalMesh"
        or name.endswith("/internalMesh")
        or name.endswith(".internalMesh")
    ]


def exact_time(available, requested: float) -> float:
    nearest = min(available, key=lambda value: abs(value - requested))
    tolerance = 1.0e-8 * max(1.0, abs(requested))
    if abs(nearest - requested) > tolerance:
        raise ValueError(
            f"Time {requested:g} is unavailable; nearest written time is {nearest:g}"
        )
    return nearest


def read_snapshot(reader, time: float, fields):
    """Read internal-mesh cell arrays and volumes, copying reader-owned data."""
    import numpy as np

    reader.set_active_time_value(time)
    # In decomposed multi-region cases, a processor can legitimately own zero
    # cells for one solid. VTK warns once per empty processor partition even
    # though the assembled region is valid. Silence that narrow reader noise;
    # the explicit non-empty mesh/field checks below still reject real errors.
    warning_state = None
    try:
        import vtk

        warning_state = vtk.vtkObject.GetGlobalWarningDisplay()
        vtk.vtkObject.GlobalWarningDisplayOff()
    except (ImportError, AttributeError):
        vtk = None
    try:
        data = reader.read()
    finally:
        if vtk is not None and warning_state:
            vtk.vtkObject.GlobalWarningDisplayOn()
    snapshot = {}
    for path, block in iter_named_leaves(data):
        if not path or not str(path[-1]).endswith("internalMesh"):
            continue
        sized = block.compute_cell_sizes(
            length=False, area=False, volume=True, vertex_count=False
        )
        key = "/".join(path)
        snapshot[key] = {
            "volume": np.asarray(sized.cell_data["Volume"], dtype=float).copy(),
            **{
                field: np.asarray(block.cell_data[field], dtype=float).copy()
                for field in fields if field in block.cell_data
            },
        }
    if not snapshot:
        raise ValueError("The OpenFOAM reader returned no internal meshes")
    missing = [
        field for field in fields
        if not any(field in region for region in snapshot.values())
    ]
    if missing:
        raise ValueError("No internal mesh contains fields: " + ", ".join(missing))
    return snapshot


def compare_snapshots(reference, sample, fields):
    """Compare matching regions and also return an all-region aggregate."""
    import numpy as np

    if set(reference) != set(sample):
        missing = sorted(set(reference) - set(sample))
        added = sorted(set(sample) - set(reference))
        raise ValueError(f"Internal mesh set changed; missing={missing}, added={added}")
    rows = []
    for field in fields:
        all_reference = []
        all_sample = []
        all_volume = []
        reference_regions = {
            region for region, values in reference.items() if field in values
        }
        sample_regions = {
            region for region, values in sample.items() if field in values
        }
        if reference_regions != sample_regions:
            raise ValueError(
                f"Regions owning field {field} changed from "
                f"{sorted(reference_regions)} to {sorted(sample_regions)}"
            )
        for region in sorted(reference_regions):
            metrics = field_error(
                reference[region][field], sample[region][field],
                reference[region]["volume"],
            )
            rows.append({"region": region, "field": field, **metrics})
            all_reference.append(reference[region][field])
            all_sample.append(sample[region][field])
            all_volume.append(reference[region]["volume"])
        rows.append({
            "region": "all",
            "field": field,
            **field_error(
                np.concatenate(all_reference), np.concatenate(all_sample),
                np.concatenate(all_volume),
            ),
        })
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path)
    parser.add_argument(
        "--times", nargs="+", type=float, required=True,
        help="written result times to compare, in ascending order",
    )
    parser.add_argument(
        "--fields", nargs="+", default=("U", "T"),
        help="cell fields to compare (default: U T)",
    )
    parser.add_argument(
        "--reference", choices=("final", "previous"), default="final",
        help="compare each time with the final time or its predecessor",
    )
    parser.add_argument("--csv", type=Path, help="optional CSV output path")
    args = parser.parse_args()
    if len(args.times) < 2:
        raise SystemExit("--times requires at least two values")
    if any(b <= a for a, b in zip(args.times, args.times[1:])):
        raise SystemExit("--times must be strictly increasing")

    try:
        import pyvista as pv
        import vtk
    except ImportError as exc:
        raise SystemExit(
            "This tool requires NumPy and PyVista. Install them with:\n"
            "  python -m pip install numpy pyvista"
        ) from exc
    # Decomposed multi-region readers warn for each processor that owns zero
    # cells in a valid solid region. Keep the CLI quiet for its full reader
    # lifetime; explicit structural checks below provide actionable failures.
    vtk.vtkObject.GlobalWarningDisplayOff()

    case = args.case.expanduser().resolve()
    if not (case / "system" / "controlDict").is_file():
        raise SystemExit(f"Not an OpenFOAM case: {case}")
    marker = case / f"{case.name}.foam"
    marker.touch(exist_ok=True)
    reader = pv.POpenFOAMReader(str(marker))
    if any(case.glob("processor[0-9]*")):
        reader.case_type = "decomposed"
    for field in args.fields:
        if field not in reader.cell_array_names:
            raise SystemExit(
                f"Field {field!r} is unavailable; choices: "
                + ", ".join(reader.cell_array_names)
            )
        reader.enable_cell_array(field)
    patches = internal_patch_names(reader)
    if patches:
        reader.disable_all_patch_arrays()
        for patch in patches:
            reader.enable_patch_array(patch)
    available = [float(value) for value in reader.time_values]
    try:
        times = [exact_time(available, value) for value in args.times]
        snapshots = {time: read_snapshot(reader, time, args.fields) for time in times}
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc

    comparisons = []
    pairs = (
        [(time, times[-1]) for time in times[:-1]]
        if args.reference == "final"
        else list(zip(times[:-1], times[1:]))
    )
    for sample_time, reference_time in pairs:
        try:
            rows = compare_snapshots(
                snapshots[reference_time], snapshots[sample_time], args.fields
            )
        except ValueError as exc:
            raise SystemExit(str(exc)) from exc
        for row in rows:
            comparisons.append({
                "sample_time": sample_time,
                "reference_time": reference_time,
                **row,
            })

    headings = (
        "sample_time", "reference_time", "region", "field", "cells",
        "volume", "mean_absolute", "rms", "maximum", "reference_rms",
        "relative_rms",
    )
    if args.csv:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        with args.csv.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=headings)
            writer.writeheader()
            writer.writerows(comparisons)
        print(f"Saved: {args.csv}")

    print("sample -> reference | region | field | RMS | max | relative RMS")
    for row in comparisons:
        if row["region"] != "all":
            continue
        print(
            f"{row['sample_time']:.12g} -> "
            f"{row['reference_time']:.12g} | all | "
            f"{row['field']} | {row['rms']:.8g} | {row['maximum']:.8g} | "
            f"{row['relative_rms']:.6%}"
        )


if __name__ == "__main__":
    main()
