"""Interactive/saved Python viewer for OpenFOAM fluid result fields.

Reads reconstructed cases and live decomposed ``processor*`` cases directly.
Examples are documented in README.md under "Python fluid-results viewer".
"""
from __future__ import annotations

import argparse
from pathlib import Path

from heat_animation import (
    add_pyvista_geometry,
    enable_internal_meshes_only,
    iter_named_pyvista_datasets,
    parse_rack_file,
    read_openfoam_internal_meshes,
    select_openfoam_time,
)


FIELD_ALIASES = {
    "temperature": ("T", "Temperature"),
    "t": ("T", "Temperature"),
    "speed": ("U", "Velocity magnitude"),
    "velocity": ("U", "Velocity magnitude"),
    "u": ("U", "Velocity magnitude"),
    "pressure": ("p", "Pressure"),
    "p": ("p", "Pressure"),
    "p_rgh": ("p_rgh", "Hydrostatic pressure"),
    "k": ("k", "Turbulent kinetic energy"),
    "omega": ("omega", "Specific dissipation rate"),
    "nut": ("nut", "Turbulent viscosity"),
    "alphat": ("alphat", "Turbulent thermal diffusivity"),
}


def resolve_field(name: str) -> tuple[str, str]:
    return FIELD_ALIASES.get(name.lower(), (name, name))


def select_fluid_leaves(dataset):
    """Return internal volume leaves belonging to the fluid region only."""
    named = list(iter_named_pyvista_datasets(dataset))
    internal = [(path, leaf) for path, leaf in named if "internalMesh" in path]
    candidates = internal or named
    fluid = [
        (path, leaf) for path, leaf in candidates
        if any(str(part).lower() == "fluid" for part in path)
    ]
    if fluid:
        return fluid
    # Single-region fluid cases do not always include a named "fluid" parent.
    return candidates if len(candidates) == 1 else []


def scalar_array(leaf, field: str, temperature_units: str, np):
    """Attach/return a plottable scalar and its values for one dataset."""
    association = leaf.cell_data if field in leaf.cell_data else leaf.point_data
    if field not in association:
        return None
    values = np.asarray(association[field])
    shown = leaf.copy(deep=False)
    shown_association = (
        shown.cell_data if field in leaf.cell_data else shown.point_data
    )
    scalar_name = field
    if values.ndim == 2 and values.shape[1] == 3:
        scalar_name = f"{field}_magnitude"
        shown_association[scalar_name] = np.linalg.norm(values, axis=1)
    elif field == "T" and temperature_units == "C":
        scalar_name = "T_C"
        shown_association[scalar_name] = values - 273.15
    return shown, scalar_name, np.asarray(shown_association[scalar_name])


def finite_range(values, np):
    finite = np.asarray(values, dtype=float)
    finite = finite[np.isfinite(finite)]
    if not finite.size:
        return None
    return (float(finite.min()), float(finite.max()),
            float(finite.sum()), int(finite.size))


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="View OpenFOAM fluid temperature, velocity, pressure, or turbulence fields"
    )
    parser.add_argument("--case", required=True, help="OpenFOAM case directory")
    parser.add_argument("--time", default="latest", help="Written time or 'latest'")
    parser.add_argument("--field", default="temperature", help=(
        "temperature, speed, pressure, p_rgh, k, omega, nut, alphat, or an exact field name"))
    parser.add_argument("--slice-axis", choices=("x", "y", "z", "none"), default="y")
    parser.add_argument("--slice-position", type=float, help="Physical slice coordinate")
    parser.add_argument("--vectors", action="store_true", help="Overlay velocity arrows")
    parser.add_argument("--vector-factor", type=int, default=12,
                        help="Approximate arrow decimation factor")
    parser.add_argument("--vector-scale", type=float, default=0.08,
                        help="Arrow length scale in metres per m/s")
    parser.add_argument("--temperature-units", choices=("C", "K"), default="C")
    parser.add_argument("--opacity", type=float, default=0.95)
    parser.add_argument("--clim", nargs=2, type=float, metavar=("MIN", "MAX"),
                        help="Fixed color limits in displayed units")
    parser.add_argument("--rack", help="Geometry report; defaults to <case>/geometry.txt")
    parser.add_argument("--no-geometry", action="store_true")
    parser.add_argument("--save", action="store_true", help="Save a PNG instead of opening a window")
    parser.add_argument("--output", default="fluid_results.png")
    return parser


def main() -> None:
    args = build_argument_parser().parse_args()
    try:
        import numpy as np
        import pyvista as pv
    except ImportError as exc:
        raise SystemExit(
            "Fluid visualization requires NumPy and PyVista. Install them with:\n"
            "  python -m pip install numpy pyvista"
        ) from exc

    case = Path(args.case).expanduser().resolve()
    if not (case / "system" / "controlDict").is_file():
        raise SystemExit(f"Not an OpenFOAM case: {case}")
    marker = case / f"{case.name}.foam"
    marker.touch(exist_ok=True)
    reader = pv.POpenFOAMReader(str(marker))
    if any(case.glob("processor[0-9]*")):
        reader.case_type = "decomposed"
    reader.cell_to_point_creation = True
    try:
        enable_internal_meshes_only(reader)
    except AttributeError:
        pass
    selected_time = select_openfoam_time(reader, args.time)
    multiblock = read_openfoam_internal_meshes(reader)
    leaves = select_fluid_leaves(multiblock)
    if not leaves:
        raise SystemExit("No uniquely identifiable fluid internal mesh was found")

    field, label = resolve_field(args.field)
    prepared = []
    ranges = []
    available = set()
    for _, leaf in leaves:
        available.update(leaf.cell_data.keys())
        available.update(leaf.point_data.keys())
        item = scalar_array(leaf, field, args.temperature_units, np)
        if item is None:
            continue
        stats = finite_range(item[2], np)
        if stats is not None:
            ranges.append(stats)
            prepared.append(item)
    if not prepared:
        raise SystemExit(
            f"Field {field!r} is unavailable at t={selected_time:g}. "
            f"Available fluid fields: {', '.join(sorted(available)) or 'none'}"
        )

    low = min(item[0] for item in ranges)
    high = max(item[1] for item in ranges)
    mean = sum(item[2] for item in ranges) / sum(item[3] for item in ranges)
    clim = tuple(args.clim) if args.clim else (low, high)
    unit = " C" if field == "T" and args.temperature_units == "C" else (
        " K" if field == "T" else ""
    )
    print(f"Case: {case}")
    print(f"Time: {selected_time:g} s")
    print(f"Fluid {label}: min={low:.8g}{unit}, mean={mean:.8g}{unit}, max={high:.8g}{unit}")

    bounds = (
        min(item[0].bounds[0] for item in prepared), max(item[0].bounds[1] for item in prepared),
        min(item[0].bounds[2] for item in prepared), max(item[0].bounds[3] for item in prepared),
        min(item[0].bounds[4] for item in prepared), max(item[0].bounds[5] for item in prepared),
    )
    axis_index = {"x": 0, "y": 1, "z": 2}.get(args.slice_axis)
    normal = {"x": (1, 0, 0), "y": (0, 1, 0), "z": (0, 0, 1)}.get(args.slice_axis)
    position = args.slice_position
    if axis_index is not None and position is None:
        position = (bounds[2 * axis_index] + bounds[2 * axis_index + 1]) / 2

    plotter = pv.Plotter(off_screen=args.save, window_size=(1600, 1000))
    plotted = 0
    for leaf, scalar_name, _ in prepared:
        shown = leaf
        if axis_index is not None:
            if not leaf.bounds[2 * axis_index] <= position <= leaf.bounds[2 * axis_index + 1]:
                continue
            origin = list(leaf.center)
            origin[axis_index] = position
            shown = leaf.slice(normal=normal, origin=origin)
        if shown.n_cells == 0:
            continue
        plotter.add_mesh(shown, scalars=scalar_name, cmap="turbo", clim=clim,
                         opacity=args.opacity, show_scalar_bar=(plotted == 0),
                         scalar_bar_args={"title": f"{label}{unit}"})
        if args.vectors and "U" in shown.point_data:
            points = shown.extract_points(
                np.arange(shown.n_points) % max(1, args.vector_factor) == 0,
                adjacent_cells=False,
            )
            if points.n_points and "U" in points.point_data:
                glyphs = points.glyph(orient="U", scale="U", factor=args.vector_scale)
                plotter.add_mesh(glyphs, color="white")
        plotted += 1
    if not plotted:
        raise SystemExit("The requested slice does not intersect the fluid mesh")

    if not args.no_geometry:
        geometry = Path(args.rack) if args.rack else case / "geometry.txt"
        try:
            add_pyvista_geometry(plotter, pv, parse_rack_file(str(geometry)))
        except (FileNotFoundError, ValueError) as exc:
            print(f"Warning: {exc}; continuing without geometry overlay")
    plotter.add_text(f"Fluid {label} — t={selected_time:g} s", position="upper_left")
    plotter.add_axes()
    plotter.show_grid()
    plotter.view_isometric()
    if args.save:
        output = Path(args.output)
        if output.suffix.lower() not in {".png", ".jpg", ".jpeg", ".tif", ".tiff"}:
            output = output.with_suffix(".png")
        plotter.show(screenshot=str(output), auto_close=True)
        print(f"Saved: {output.resolve()}")
    else:
        plotter.show()


if __name__ == "__main__":
    main()
