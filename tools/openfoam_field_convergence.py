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


def field_error(
    reference, sample, volumes, centers=None, *, remove_uniform_offset=False
):
    """Return volume-weighted error metrics for scalar or vector cell data."""
    import numpy as np

    reference = np.asarray(reference, dtype=float)
    sample = np.asarray(sample, dtype=float)
    volumes = np.asarray(volumes, dtype=float).reshape(-1)
    if centers is not None:
        centers = np.asarray(centers, dtype=float)
    if reference.shape != sample.shape:
        raise ValueError(
            f"Field shape changed from {reference.shape} to {sample.shape}"
        )
    if reference.shape[0] != volumes.size:
        raise ValueError("Field and volume cell counts differ")
    if centers is not None and centers.shape != (volumes.size, 3):
        raise ValueError("Cell centers must have shape (cell count, 3)")
    total_volume = float(np.sum(volumes))
    if not math.isfinite(total_volume) or total_volume <= 0.0:
        raise ValueError("Cell volumes must have a positive finite sum")
    if remove_uniform_offset:
        if reference.ndim != 1:
            raise ValueError("Uniform-offset removal requires a scalar field")
        reference_mean = float(np.sum(volumes * reference) / total_volume)
        sample_mean = float(np.sum(volumes * sample) / total_volume)
        reference = reference - reference_mean
        sample = sample - sample_mean
    if reference.ndim == 1:
        difference = np.abs(sample - reference)
        reference_magnitude = np.abs(reference)
    elif reference.ndim == 2:
        difference = np.linalg.norm(sample - reference, axis=1)
        reference_magnitude = np.linalg.norm(reference, axis=1)
    else:
        raise ValueError(f"Unsupported field rank: {reference.ndim}")
    mean_absolute = float(np.sum(volumes * difference) / total_volume)
    rms = float(np.sqrt(np.sum(volumes * difference**2) / total_volume))
    reference_rms = float(
        np.sqrt(np.sum(volumes * reference_magnitude**2) / total_volume)
    )
    maximum_index = int(np.argmax(difference)) if difference.size else None
    maximum_center = (
        centers[maximum_index]
        if centers is not None and maximum_index is not None
        else (math.nan, math.nan, math.nan)
    )
    return {
        "cells": int(volumes.size),
        "volume": total_volume,
        "mean_absolute": mean_absolute,
        "rms": rms,
        "maximum": float(np.max(difference)) if difference.size else 0.0,
        "maximum_x": float(maximum_center[0]),
        "maximum_y": float(maximum_center[1]),
        "maximum_z": float(maximum_center[2]),
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


def directory_times(directory: Path) -> list[float]:
    """Return numeric OpenFOAM time-directory names without rounding them."""
    values = []
    if not directory.is_dir():
        return values
    for path in directory.iterdir():
        if not path.is_dir():
            continue
        try:
            values.append(float(path.name))
        except ValueError:
            continue
    return sorted(values)


def select_case_type(case: Path, requested, preference: str = "auto") -> str:
    """Choose reconstructed data when it contains every requested time."""
    if preference != "auto":
        return preference
    root_times = directory_times(case)
    if root_times:
        try:
            for value in requested:
                exact_time(root_times, value)
            return "reconstructed"
        except ValueError:
            pass
    if any(case.glob("processor[0-9]*")):
        return "decomposed"
    return "reconstructed"


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
            "center": np.asarray(block.cell_centers().points, dtype=float).copy(),
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
        all_center = []
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
                reference[region]["volume"], reference[region]["center"],
                remove_uniform_offset=field in ("p", "p_rgh"),
            )
            rows.append({"region": region, "field": field, **metrics})
            all_reference.append(reference[region][field])
            all_sample.append(sample[region][field])
            all_volume.append(reference[region]["volume"])
            all_center.append(reference[region]["center"])
        rows.append({
            "region": "all",
            "field": field,
            **field_error(
                np.concatenate(all_reference), np.concatenate(all_sample),
                np.concatenate(all_volume), np.concatenate(all_center),
                remove_uniform_offset=field in ("p", "p_rgh"),
            ),
        })
    return rows


def component_air_partitions(centers, components, tolerance=1.0e-9):
    """Return non-overlapping component-air and external-rack cell masks."""
    import numpy as np

    centers = np.asarray(centers, dtype=float)
    assigned = np.zeros(centers.shape[0], dtype=bool)
    partitions = []
    for name, origin, size in components:
        lower = np.asarray(origin, dtype=float) - tolerance
        upper = np.asarray(origin, dtype=float) + np.asarray(size, dtype=float) + tolerance
        mask = np.all((centers >= lower) & (centers <= upper), axis=1)
        mask &= ~assigned
        if np.any(mask):
            partitions.append((f"fluid/componentAir:{name}", mask))
            assigned |= mask
    partitions.append(("fluid/externalRackAir", ~assigned))
    return partitions


def append_fluid_partition_rows(rows, reference, sample, fields, components):
    """Append field-error metrics for component air and external rack air."""
    import numpy as np

    fluid_regions = [name for name in reference if name.endswith("fluid/internalMesh")]
    if len(fluid_regions) != 1:
        raise ValueError(
            "Geometry partitioning requires exactly one fluid internal mesh; "
            f"found {fluid_regions}"
        )
    region = fluid_regions[0]
    values = reference[region]
    partition_fields = {}
    for field in fields:
        if field not in values or field not in sample[region]:
            continue
        reference_field = values[field]
        sample_field = sample[region][field]
        if field in ("p", "p_rgh"):
            volumes = values["volume"]
            total_volume = float(np.sum(volumes))
            reference_field = reference_field - float(
                np.sum(volumes * reference_field) / total_volume
            )
            sample_field = sample_field - float(
                np.sum(volumes * sample_field) / total_volume
            )
        partition_fields[field] = (reference_field, sample_field)
    for label, mask in component_air_partitions(values["center"], components):
        if not mask.any():
            continue
        for field in fields:
            if field not in partition_fields:
                continue
            reference_field, sample_field = partition_fields[field]
            rows.append({
                "region": label,
                "field": field,
                **field_error(
                    reference_field[mask], sample_field[mask],
                    values["volume"][mask], values["center"][mask],
                ),
            })


def load_component_air_boxes(path: Path):
    """Read named component Air-region boxes from exported geometry.txt."""
    import sys

    plot_directory = Path(__file__).resolve().parents[1] / "plot"
    sys.path.insert(0, str(plot_directory))
    try:
        from heat_animation import parse_rack_file
    finally:
        sys.path.pop(0)
    rack = parse_rack_file(str(path))
    return [
        (component.name, region.origin, region.size)
        for component in rack.components
        for region in component.regions
        if region.kind.strip().lower() == "air"
    ]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path)
    parser.add_argument(
        "--times", nargs="+", type=float, required=True,
        help="written result times to compare, in ascending order",
    )
    parser.add_argument(
        "--fields", nargs="+", default=("U", "T"),
        help=("cell fields to compare (default: U T); p and p_rgh are "
              "compared after removing their arbitrary uniform gauge offset"),
    )
    parser.add_argument(
        "--reference", choices=("final", "previous"), default="final",
        help="compare each time with the final time or its predecessor",
    )
    parser.add_argument(
        "--case-type", choices=("auto", "reconstructed", "decomposed"),
        default="auto",
        help=("OpenFOAM data layout; auto prefers reconstructed data when it "
              "contains every requested time"),
    )
    parser.add_argument("--csv", type=Path, help="optional CSV output path")
    parser.add_argument(
        "--geometry", type=Path,
        help=("exported geometry.txt; report component-air and external-rack-"
              "air field errors separately"),
    )
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
    case_type = select_case_type(case, args.times, args.case_type)
    reader.case_type = case_type
    print(f"Reading {case_type} OpenFOAM data")
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
            if args.geometry:
                components = load_component_air_boxes(
                    args.geometry.expanduser().resolve()
                )
                append_fluid_partition_rows(
                    rows, snapshots[reference_time], snapshots[sample_time],
                    args.fields, components,
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
        "relative_rms", "maximum_x", "maximum_y", "maximum_z",
    )
    if args.csv:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        with args.csv.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=headings)
            writer.writeheader()
            writer.writerows(comparisons)
        print(f"Saved: {args.csv}")

    print(
        "sample -> reference | region | field | RMS | max | "
        "max location (m) | relative RMS"
    )
    for row in comparisons:
        if row["region"] != "all" and not (
            args.geometry and row["region"].startswith("fluid/")
        ):
            continue
        print(
            f"{row['sample_time']:.12g} -> "
            f"{row['reference_time']:.12g} | {row['region']} | "
            f"{row['field']} | {row['rms']:.8g} | {row['maximum']:.8g} | "
            f"({row['maximum_x']:.6g}, {row['maximum_y']:.6g}, "
            f"{row['maximum_z']:.6g}) | "
            f"{row['relative_rms']:.6%}"
        )


if __name__ == "__main__":
    main()
