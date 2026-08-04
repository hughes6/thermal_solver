"""Report the mass-flow-weighted temperature on an OpenFOAM outlet patch."""

from __future__ import annotations

import argparse
from pathlib import Path

def iter_named_leaves(dataset, path=()):
    """Yield ``(block path, leaf)`` pairs from a nested PyVista dataset."""
    if dataset is None:
        return
    if hasattr(dataset, "n_blocks"):
        names = list(dataset.keys()) if hasattr(dataset, "keys") else []
        for index, block in enumerate(dataset):
            name = names[index] if index < len(names) else f"block_{index}"
            yield from iter_named_leaves(block, path + (str(name),))
    elif getattr(dataset, "n_cells", 0):
        yield path, dataset


def patch_basename(name: str) -> str:
    return name.replace("\\", "/").rstrip("/").split("/")[-1]


def select_time(reader, requested: str) -> float:
    times = [float(value) for value in reader.time_values]
    if not times:
        raise ValueError("The OpenFOAM case contains no readable result times")
    if requested.lower() == "latest":
        return max(times)

    target = float(requested)
    selected = min(times, key=lambda value: abs(value - target))
    tolerance = 1.0e-8 * max(1.0, abs(target))
    if abs(selected - target) > tolerance:
        raise ValueError(
            f"Time {target:g} is unavailable; nearest written time is {selected:g}"
        )
    return selected


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Calculate an outlet temperature weighted by OpenFOAM mass flux phi."
        )
    )
    parser.add_argument("case", type=Path, help="OpenFOAM case directory")
    parser.add_argument(
        "--outlet",
        default="Validation_outlet",
        help="Outlet boundary-patch name (default: Validation_outlet)",
    )
    parser.add_argument(
        "--time",
        default="latest",
        help="Written result time or 'latest' (default: latest)",
    )
    args = parser.parse_args()

    try:
        import numpy as np
        import pyvista as pv
    except ImportError as exc:
        raise SystemExit(
            "This tool requires NumPy and PyVista. Install them with:\n"
            "  python -m pip install numpy pyvista"
        ) from exc

    case = args.case.expanduser().resolve()
    if not (case / "system" / "controlDict").is_file():
        raise SystemExit(f"Not an OpenFOAM case: {case}")

    marker = case / f"{case.name}.foam"
    marker.touch(exist_ok=True)
    reader = pv.POpenFOAMReader(str(marker))
    if any(case.glob("processor[0-9]*")):
        reader.case_type = "decomposed"
    # VTK does not consistently enable surface fields (notably phi) when it
    # first opens a multi-region case. Request every available cell field
    # before reading the selected boundary patch.
    for field in reader.cell_array_names:
        reader.enable_cell_array(field)

    matching_arrays = [
        name
        for name in reader.patch_array_names
        if patch_basename(str(name)) == args.outlet
    ]
    if not matching_arrays:
        available = "\n  ".join(str(name) for name in reader.patch_array_names)
        raise SystemExit(
            f"Could not find outlet patch {args.outlet!r}.\n"
            f"Available patches:\n  {available}"
        )

    reader.disable_all_patch_arrays()
    for name in matching_arrays:
        reader.enable_patch_array(name)

    try:
        selected_time = select_time(reader, args.time)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    reader.set_active_time_value(selected_time)
    data = reader.read()

    weighted_temperature_sum = 0.0
    mass_flow_sum = 0.0
    matched_cells = 0

    for path, block in iter_named_leaves(data):
        if not path or patch_basename(path[-1]) != args.outlet:
            continue

        required_cell_fields = ("T", "phi", "rho", "U")
        if any(
            field not in block.cell_data and field in block.point_data
            for field in required_cell_fields
        ):
            block = block.point_data_to_cell_data(pass_point_data=True)
        if "T" not in block.cell_data:
            raise SystemExit(
                f"Patch {'/'.join(path)} does not contain temperature T.\n"
                f"Available arrays: {', '.join(block.array_names)}"
            )

        temperature = np.asarray(block.cell_data["T"], dtype=float).reshape(-1)
        if "phi" in block.cell_data:
            mass_flow = np.abs(
                np.asarray(block.cell_data["phi"], dtype=float).reshape(-1)
            )
        elif "rho" in block.cell_data and "U" in block.cell_data:
            # Some VTK/OpenFOAM combinations omit surfaceScalarField data on
            # boundary blocks. Reconstruct each face's mass flow as
            # |rho * U dot n * area| from the volume-field boundary values.
            geometry = block.compute_cell_sizes(
                length=False, area=True, volume=False
            ).compute_normals(
                cell_normals=True,
                point_normals=False,
                consistent_normals=False,
                auto_orient_normals=False,
            )
            density = np.asarray(
                geometry.cell_data["rho"], dtype=float
            ).reshape(-1)
            velocity = np.asarray(
                geometry.cell_data["U"], dtype=float
            ).reshape((-1, 3))
            normals = np.asarray(
                geometry.cell_data["Normals"], dtype=float
            ).reshape((-1, 3))
            area = np.asarray(
                geometry.cell_data["Area"], dtype=float
            ).reshape(-1)
            mass_flow = np.abs(
                density * np.einsum("ij,ij->i", velocity, normals) * area
            )
        else:
            raise SystemExit(
                f"Patch {'/'.join(path)} has no phi array and cannot reconstruct "
                "mass flow without both rho and U.\n"
                f"Available arrays: {', '.join(block.array_names)}"
            )
        if temperature.size != mass_flow.size:
            raise SystemExit(
                f"Patch {'/'.join(path)} has {temperature.size} T values but "
                f"{mass_flow.size} phi values"
            )

        weighted_temperature_sum += float(np.sum(temperature * mass_flow))
        mass_flow_sum += float(np.sum(mass_flow))
        matched_cells += block.n_cells

    if not matched_cells:
        raise SystemExit(
            f"The reader returned no boundary faces for patch {args.outlet!r}"
        )
    if mass_flow_sum <= 0.0:
        raise SystemExit(f"Mass flow through patch {args.outlet!r} is zero")

    temperature_k = weighted_temperature_sum / mass_flow_sum
    print(f"Result time:                  {selected_time:g} s")
    print(f"Outlet patch:                 {args.outlet}")
    print(f"Outlet faces:                 {matched_cells}")
    print(f"Absolute outlet mass flow:    {mass_flow_sum:.8g} kg/s")
    print(f"Mass-weighted temperature:    {temperature_k:.6f} K")
    print(f"Mass-weighted temperature:    {temperature_k - 273.15:.6f} C")


if __name__ == "__main__":
    main()
