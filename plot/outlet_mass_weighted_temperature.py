"""Report the mass-flow-weighted temperature on an OpenFOAM outlet patch."""

from __future__ import annotations

import argparse
from pathlib import Path

try:
    from tools.openfoam_field_delta import resolve_time_directory
    from tools.validate_openfoam_case import latest_result_paths, patch_values
except ModuleNotFoundError:
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from tools.openfoam_field_delta import resolve_time_directory
    from tools.validate_openfoam_case import latest_result_paths, patch_values


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


def read_surface_report(report_root: Path) -> dict[float, float]:
    """Read scalar surfaceFieldValue output across restart directories."""
    samples: dict[float, float] = {}
    # OpenFOAM appends the time to a filename when post-processing would
    # otherwise overwrite an initialized terminal report. Read both forms.
    for path in report_root.glob("*/surfaceFieldValue*.dat"):
        with path.open("r", encoding="utf-8", errors="replace") as stream:
            for line in stream:
                columns = line.replace("(", " ").replace(")", " ").split()
                if not columns or columns[0].startswith("#"):
                    continue
                try:
                    samples[float(columns[0])] = float(columns[1])
                except (ValueError, IndexError):
                    continue
    return samples


def report_value_at_time(samples: dict[float, float], time: float):
    tolerance = 1.0e-8 * max(1.0, abs(time))
    matches = [key for key in samples if abs(key - time) <= tolerance]
    return samples[max(matches)] if matches else None


def select_report_time(samples: dict[float, float], requested: str):
    """Select a requested report time without loading a VTK case."""
    if not samples:
        return None
    if requested.lower() == "latest":
        return max(samples)
    target = float(requested)
    matches = [
        time for time in samples
        if abs(time - target) <= 1.0e-8 * max(1.0, abs(target))
    ]
    return max(matches) if matches else None


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


def direct_result_paths(case: Path, requested: str) -> tuple[float, list[Path]]:
    if requested.lower() == "latest":
        return latest_result_paths(case)
    target = float(requested)
    processors = sorted(
        (path for path in case.glob("processor*") if path.is_dir()),
        key=lambda path: int(path.name[9:]),
    )
    if processors:
        paths = [resolve_time_directory(processor, requested)
                 for processor in processors]
        return float(paths[0].name), paths
    matches = []
    for path in case.iterdir():
        if not path.is_dir():
            continue
        try:
            value = float(path.name)
        except ValueError:
            continue
        if abs(value - target) <= 1.0e-8 * max(1.0, abs(target)):
            matches.append((abs(value - target), value, path))
    if not matches:
        raise ValueError(f"Time {target:g} is unavailable")
    _, value, path = min(matches)
    return value, [path]


def direct_patch_temperature(
    case: Path, patch: str, requested: str
) -> tuple[float, float, float, int]:
    time_s, result_paths = direct_result_paths(case, requested)
    mass_flow = 0.0
    weighted_temperature = 0.0
    faces = 0
    for result_path in result_paths:
        fluid = result_path / "fluid"
        fluxes = patch_values(fluid / "phi", patch)
        temperatures = patch_values(fluid / "T", patch)
        if len(temperatures) == 1:
            weighted_temperature += temperatures[0] * sum(fluxes)
        elif len(temperatures) == len(fluxes):
            weighted_temperature += sum(
                temperature * flux
                for temperature, flux in zip(temperatures, fluxes)
            )
        else:
            raise ValueError(
                f"Patch {patch!r} has {len(temperatures)} T values but "
                f"{len(fluxes)} phi values in {result_path}"
            )
        mass_flow += sum(fluxes)
        faces += len(fluxes)
    if not faces:
        raise ValueError(f"Patch {patch!r} has no faces at t={time_s:g} s")
    return time_s, mass_flow, weighted_temperature / mass_flow, faces


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
    parser.add_argument(
        "--minimum-mass-flow",
        type=float,
        default=1.0e-8,
        help=(
            "reject weighted temperatures when absolute net flow is at or "
            "below this value in kg/s (default: 1e-8)"
        ),
    )
    args = parser.parse_args()
    if args.minimum_mass_flow < 0.0:
        raise SystemExit("--minimum-mass-flow cannot be negative")

    case = args.case.expanduser().resolve()
    if not (case / "system" / "controlDict").is_file():
        raise SystemExit(f"Not an OpenFOAM case: {case}")

    report_root = (
        case
        / "postProcessing"
        / "fluid"
        / f"{args.outlet}_mass_weighted_temperature"
    )
    try:
        direct_time, direct_flow, direct_temperature, direct_faces = (
            direct_patch_temperature(case, args.outlet, args.time)
        )
        direct_failure = None
    except (FileNotFoundError, ValueError) as direct_error:
        direct_failure = str(direct_error)
    exact_samples = read_surface_report(report_root)
    selected_report_time = select_report_time(exact_samples, args.time)
    report_is_current = (
        selected_report_time is not None
        and (
            args.time.lower() != "latest"
            or direct_failure is not None
            or abs(selected_report_time - direct_time)
               <= 1.0e-8 * max(1.0, abs(direct_time))
        )
    )
    if report_is_current:
        mass_samples = read_surface_report(
            case
            / "postProcessing"
            / "fluid"
            / f"{args.outlet}_mass_flow"
        )
        exact_mass_flow = report_value_at_time(
            mass_samples, selected_report_time
        )
        if exact_mass_flow is not None:
            if abs(exact_mass_flow) <= args.minimum_mass_flow:
                raise SystemExit(
                    f"Net mass flow through patch {args.outlet!r} is "
                    f"{exact_mass_flow:.8g} kg/s; its mass-weighted "
                    "temperature is undefined."
                )
            exact_temperature = exact_samples[selected_report_time]
            print(f"Result time:                  {selected_report_time:g} s")
            print(f"Outlet patch:                 {args.outlet}")
            print(f"Net outlet mass flow:         {exact_mass_flow:.8g} kg/s")
            print(f"Mass-weighted temperature:    {exact_temperature:.6f} K")
            print(
                "Mass-weighted temperature:    "
                f"{exact_temperature - 273.15:.6f} C"
            )
            print("Weighting source:              OpenFOAM weightedAverage(T, phi)")
            return

    if direct_failure is None:
        if abs(direct_flow) <= args.minimum_mass_flow:
            raise SystemExit(
                f"Net mass flow through patch {args.outlet!r} is "
                f"{direct_flow:.8g} kg/s; its mass-weighted temperature is "
                "undefined."
            )
        print(f"Result time:                  {direct_time:g} s")
        print(f"Outlet patch:                 {args.outlet}")
        print(f"Outlet faces:                 {direct_faces}")
        print(f"Net outlet mass flow:         {direct_flow:.8g} kg/s")
        print(f"Mass-weighted temperature:    {direct_temperature:.6f} K")
        print(
            "Mass-weighted temperature:    "
            f"{direct_temperature - 273.15:.6f} C"
        )
        print("Weighting source:              direct OpenFOAM boundary T and phi")
        return

    try:
        import numpy as np
        import pyvista as pv
    except ImportError as exc:
        raise SystemExit(
            "No matching complete OpenFOAM report or direct boundary fields "
            f"were found ({direct_failure}). VTK fallback "
            "requires NumPy and PyVista:\n"
            "  python -m pip install numpy pyvista"
        ) from exc

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
            mass_flow = np.asarray(
                block.cell_data["phi"], dtype=float
            ).reshape(-1)
        elif "rho" in block.cell_data and "U" in block.cell_data:
            # Some VTK/OpenFOAM combinations omit surfaceScalarField data on
            # boundary blocks. Reconstruct each face's mass flow as
            # rho * U dot n * area from the volume-field boundary values.
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
            mass_flow = (
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
    if abs(mass_flow_sum) <= args.minimum_mass_flow:
        raise SystemExit(
            f"Net mass flow through patch {args.outlet!r} is "
            f"{mass_flow_sum:.8g} kg/s; its mass-weighted temperature is "
            "undefined."
        )

    temperature_k = weighted_temperature_sum / mass_flow_sum
    print(f"Result time:                  {selected_time:g} s")
    print(f"Outlet patch:                 {args.outlet}")
    print(f"Outlet faces:                 {matched_cells}")
    print(f"Net outlet mass flow:         {mass_flow_sum:.8g} kg/s")
    print(f"Mass-weighted temperature:    {temperature_k:.6f} K")
    print(f"Mass-weighted temperature:    {temperature_k - 273.15:.6f} C")
    print("Weighting source:              VTK boundary-field reconstruction")
    print(
        "Warning: exact OpenFOAM temperature report is unavailable for this "
        "time. Re-export the case with the current v2.2 exporter before "
        "using this value for validation."
    )


if __name__ == "__main__":
    main()
