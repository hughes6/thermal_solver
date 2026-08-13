"""Interactive/saved Python viewer for OpenFOAM fluid result fields.

Reads reconstructed cases and live decomposed ``processor*`` cases directly.
Examples are documented in README.md under "Python fluid-results viewer".
"""
from __future__ import annotations

import argparse
import inspect
from pathlib import Path

import heat_animation
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


def seed_points_from_openings(rack, kind: str, bounds, resolution: int, np):
    """Create seeds just inside selected rack fans/vents."""
    selected = [opening for opening in rack.openings
                if kind == "all" or opening.kind.lower() == kind]
    if not selected:
        return np.empty((0, 3), dtype=float)
    center = np.asarray([
        (bounds[0] + bounds[1]) / 2,
        (bounds[2] + bounds[3]) / 2,
        (bounds[4] + bounds[5]) / 2,
    ])
    domain_scale = max(bounds[1] - bounds[0], bounds[3] - bounds[2],
                       bounds[5] - bounds[4])
    inward_offset = max(domain_scale * 1.0e-4, 1.0e-6)
    points = []
    side = max(1, int(resolution ** 0.5))
    coordinates = np.linspace(-0.4, 0.4, side) if side > 1 else np.asarray([0.0])
    for opening in selected:
        base = np.asarray(opening.center, dtype=float)
        toward_center = center - base
        norm = np.linalg.norm(toward_center)
        if norm:
            base += inward_offset * toward_center / norm
        direction = np.asarray(opening.direction, dtype=float)
        axis = int(np.argmax(np.abs(direction))) if np.linalg.norm(direction) else 2
        tangential = [index for index in range(3) if index != axis]
        extents = np.asarray(opening.size, dtype=float)
        if opening.diameter > 0:
            extents[tangential] = opening.diameter
        for first in coordinates:
            for second in coordinates:
                point = base.copy()
                point[tangential[0]] += first * extents[tangential[0]]
                point[tangential[1]] += second * extents[tangential[1]]
                points.append(point)
    return np.asarray(points, dtype=float)


def plane_seed_points(bounds, axis: str, position, resolution: int, np):
    """Create a regular seed plane spanning the fluid bounds."""
    axis_index = {"x": 0, "y": 1, "z": 2}[axis]
    low = np.asarray([bounds[0], bounds[2], bounds[4]], dtype=float)
    high = np.asarray([bounds[1], bounds[3], bounds[5]], dtype=float)
    location = ((low[axis_index] + high[axis_index]) / 2
                if position is None else position)
    if not low[axis_index] <= location <= high[axis_index]:
        raise ValueError("Streamline seed plane lies outside the fluid bounds")
    tangential = [index for index in range(3) if index != axis_index]
    side = max(2, int(resolution ** 0.5))
    first = np.linspace(low[tangential[0]], high[tangential[0]], side + 2)[1:-1]
    second = np.linspace(low[tangential[1]], high[tangential[1]], side + 2)[1:-1]
    points = []
    for a in first:
        for b in second:
            point = (low + high) / 2
            point[axis_index] = location
            point[tangential[0]] = a
            point[tangential[1]] = b
            points.append(point)
    return np.asarray(points, dtype=float)


def streamline_color_array(streamlines, choice: str, field: str,
                           temperature_units: str, np):
    """Attach the selected streamline color scalar and return its name."""
    if choice == "solid":
        return None
    if choice == "speed":
        if "U" not in streamlines.point_data:
            return None
        name = "streamline_speed"
        streamlines.point_data[name] = np.linalg.norm(
            np.asarray(streamlines.point_data["U"]), axis=1)
        return name
    source = "T" if choice == "temperature" else field
    if source not in streamlines.point_data:
        return None
    name = source
    if source == "T" and temperature_units == "C":
        name = "streamline_T_C"
        streamlines.point_data[name] = (
            np.asarray(streamlines.point_data["T"]) - 273.15)
    return name


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
    parser.add_argument("--contours", type=int, default=0,
                        help="Overlay this many scalar contour lines")
    parser.add_argument("--streamlines", action="store_true",
                        help="Integrate long paths through the U velocity field")
    parser.add_argument("--seed", choices=("auto", "plane", "fans", "vents"),
                        default="auto", help="Streamline seed source")
    parser.add_argument("--seed-axis", choices=("x", "y", "z"), default="y",
                        help="Normal of a plane streamline seed")
    parser.add_argument("--seed-position", type=float,
                        help="Physical coordinate of the seed plane")
    parser.add_argument("--seed-count", type=int, default=100,
                        help="Approximate total seeds, or seeds per opening")
    parser.add_argument("--streamline-direction",
                        choices=("forward", "backward", "both"), default="both")
    parser.add_argument("--streamline-length", type=float, default=10.0,
                        help="Maximum integrated path length in metres")
    parser.add_argument("--streamline-radius", type=float, default=0.002,
                        help="Tube radius in metres; zero draws simple lines")
    parser.add_argument("--streamline-color",
                        choices=("speed", "temperature", "field", "solid"),
                        default="speed")
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
    # Geometry-overlay helpers share the optional NumPy dependency with the
    # temperature viewer, whose normal entry point initializes this module
    # global. Initialize it here when the helpers are reused independently.
    heat_animation.np = np

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

    rack = None
    geometry = Path(args.rack) if args.rack else case / "geometry.txt"
    if not args.no_geometry or args.streamlines:
        try:
            rack = parse_rack_file(str(geometry))
        except (FileNotFoundError, ValueError) as exc:
            if not args.no_geometry:
                print(f"Warning: {exc}; continuing without geometry overlay")

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
        if args.contours:
            contour_source = shown
            if scalar_name not in contour_source.point_data:
                contour_source = contour_source.cell_data_to_point_data()
            if scalar_name in contour_source.point_data:
                contours = contour_source.contour(
                    isosurfaces=args.contours, scalars=scalar_name,
                    rng=clim,
                )
                if contours.n_cells:
                    plotter.add_mesh(contours, color="black", line_width=2.0)
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

    if args.streamlines:
        velocity_leaves = []
        for _, leaf in leaves:
            velocity = leaf
            if "U" not in velocity.point_data and "U" in velocity.cell_data:
                velocity = velocity.cell_data_to_point_data()
            if "U" in velocity.point_data:
                velocity_leaves.append(velocity)
        if not velocity_leaves:
            raise SystemExit("Streamlines require a point or cell U velocity field")
        volume = (velocity_leaves[0] if len(velocity_leaves) == 1
                  else pv.merge(velocity_leaves, merge_points=True))
        seed_mode = args.seed
        if seed_mode == "auto":
            seed_mode = "vents" if rack and any(
                opening.kind.lower() == "vent" for opening in rack.openings
            ) else "plane"
        if seed_mode in {"fans", "vents"}:
            if rack is None:
                raise SystemExit(
                    f"--seed {seed_mode} requires a readable geometry.txt")
            seeds = seed_points_from_openings(
                rack, seed_mode[:-1], bounds, args.seed_count, np)
            if not seeds.size:
                raise SystemExit(f"No {seed_mode} were found in geometry.txt")
        else:
            seeds = plane_seed_points(
                bounds, args.seed_axis, args.seed_position,
                args.seed_count, np)
        source = pv.PolyData(seeds)
        kwargs = {
            "vectors": "U",
            "integration_direction": args.streamline_direction,
            "max_steps": 5000,
            "terminal_speed": 1.0e-8,
        }
        signature = inspect.signature(volume.streamlines_from_source)
        if "max_length" in signature.parameters:
            kwargs["max_length"] = args.streamline_length
        else:
            # Compatibility with older PyVista releases where max_time is the
            # integration-distance limit for steady spatial vector fields.
            kwargs["max_time"] = args.streamline_length
        streamlines = volume.streamlines_from_source(source, **kwargs)
        if streamlines.n_cells:
            color_name = streamline_color_array(
                streamlines, args.streamline_color, field,
                args.temperature_units, np)
            shown_streamlines = (streamlines.tube(radius=args.streamline_radius)
                                 if args.streamline_radius > 0 else streamlines)
            plotter.add_mesh(
                shown_streamlines,
                scalars=color_name,
                color="white" if color_name is None else None,
                cmap="viridis",
                line_width=2.0,
                show_scalar_bar=(color_name is not None),
                scalar_bar_args={"title": f"Streamline {args.streamline_color}"},
            )
            print(f"Streamlines: {streamlines.n_cells} paths/cells from {len(seeds)} seeds")
        else:
            print("Warning: no streamlines were produced; try a different seed source or position")

    if not args.no_geometry and rack is not None:
        add_pyvista_geometry(plotter, pv, rack)
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
