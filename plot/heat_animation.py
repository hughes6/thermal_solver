"""Visualize native or OpenFOAM rack temperature fields with rack geometry.

Usage:
  python heat_animation.py --sim simulation.csv --rack output.txt
  python heat_animation.py --sim simulation.csv --rack output.txt --save
  python heat_animation.py --format openfoam --case openfoam_cases/my_model
  python heat_animation.py --format openfoam --case openfoam_cases/my_model --time 3600 --save
  python heat_animation.py --format openfoam --case openfoam_cases/my_model --animate --slice-axis none --save

OpenFOAM mode uses PyVista's OpenFOAM reader, so no intermediate CSV is
required. Install its optional dependencies with ``python -m pip install
pyvista imageio imageio-ffmpeg``. The case may be reconstructed or decomposed; a harmless ``.foam``
reader marker is created in the case directory when needed.
"""
from __future__ import annotations

import argparse
import csv
import itertools
import re
from dataclasses import dataclass, field
from pathlib import Path

@dataclass
class InternalRegionGeom:
    kind: str
    size: tuple[float, float, float]
    origin: tuple[float, float, float]
    direction: tuple[float, float, float] = (0.0, 0.0, 0.0)
    diameter: float = 0.0


@dataclass
class ComponentGeom:
    name: str
    height: float
    width: float
    depth: float
    origin: tuple[float, float, float]
    regions: list[InternalRegionGeom] = field(default_factory=list)


@dataclass
class OpeningGeom:
    name: str
    kind: str
    center: tuple[float, float, float]
    direction: tuple[float, float, float]
    size: tuple[float, float, float] = (0.0, 0.0, 0.0)
    shape: str = "Rectangular"
    diameter: float = 0.0
    label_detail: str = ""


@dataclass
class RackGeom:
    width: float
    depth: float
    height: float
    components: list[ComponentGeom] = field(default_factory=list)
    openings: list[OpeningGeom] = field(default_factory=list)


def first_float(text: str) -> float:
    match = re.search(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", text)
    if not match:
        raise ValueError(f"No numeric value in: {text}")
    return float(match.group())


def three_floats(text: str) -> tuple[float, float, float]:
    vals = re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", text)
    if len(vals) < 3:
        raise ValueError(f"Expected three values in: {text}")
    return tuple(float(v) for v in vals[:3])


def parse_simple_block(lines: list[str], start: int) -> tuple[dict[str, str], int]:
    values: dict[str, str] = {}
    i = start + 1
    while i < len(lines):
        if re.match(r"^(Component|Fan|Vent)\s+\d+:", lines[i]):
            break
        if ":" in lines[i]:
            key, value = lines[i].split(":", 1)
            values[key.strip().lower()] = value.strip()
        i += 1
    return values, i


def parse_component(lines: list[str], start: int, name: str) -> tuple[ComponentGeom, int]:
    values: dict[str, str] = {}
    regions: list[InternalRegionGeom] = []
    i = start + 1

    while i < len(lines):
        line = lines[i]
        if re.match(r"^(Component|Fan|Vent)\s+\d+:", line):
            break

        region_match = re.match(r"^Internal Region\s+\d+:$", line)
        if region_match:
            region_values: dict[str, str] = {}
            i += 1
            while i < len(lines):
                nested = lines[i]
                if (re.match(r"^Internal Region\s+\d+:$", nested)
                        or re.match(r"^(Component|Fan|Vent)\s+\d+:", nested)):
                    break
                if ":" in nested:
                    key, value = nested.split(":", 1)
                    region_values[key.strip().lower()] = value.strip()
                i += 1

            if {"type", "size", "global_position"} <= region_values.keys():
                regions.append(InternalRegionGeom(
                    kind=region_values["type"],
                    size=three_floats(region_values["size"]),
                    origin=three_floats(region_values["global_position"]),
                    direction=three_floats(region_values.get("direction", "0 0 0")),
                    diameter=first_float(region_values.get("diameter", "0")),
                ))
            continue

        if ":" in line:
            key, value = line.split(":", 1)
            values[key.strip().lower()] = value.strip()
        i += 1

    dims = three_floats(values["dimensions"])  # width, depth, height
    return ComponentGeom(
        name=name,
        width=dims[0], depth=dims[1], height=dims[2],
        origin=three_floats(values["coordinates"]),
        regions=regions,
    ), i


def parse_rack_file(filename: str) -> RackGeom:
    lines = [line.strip() for line in Path(filename).read_text().splitlines() if line.strip()]

    rack_h = rack_w = rack_d = None
    for line in lines:
        if line.startswith("height:"):
            rack_h = first_float(line.split(":", 1)[1])
        elif line.startswith("width:"):
            rack_w = first_float(line.split(":", 1)[1])
        elif line.startswith("depth:"):
            rack_d = first_float(line.split(":", 1)[1])

    if rack_h is None or rack_w is None or rack_d is None:
        raise ValueError("Could not parse rack height, width, and depth")

    rack = RackGeom(width=rack_w, depth=rack_d, height=rack_h)
    i = 0
    while i < len(lines):
        line = lines[i]
        match = re.match(r"^Component\s+\d+:\s*(.*)$", line)
        if match:
            comp, i = parse_component(lines, i, match.group(1))
            rack.components.append(comp)
            continue

        match = re.match(r"^Fan\s+\d+:\s*(.*)$", line)
        if match:
            values, i = parse_simple_block(lines, i)
            rack.openings.append(OpeningGeom(
                name=match.group(1), kind="Fan",
                center=three_floats(values.get("f_center", values.get("center", ""))),
                direction=three_floats(values.get("f_direction", values.get("direction", ""))),
                size=three_floats(values.get("f_size", "0 0 0")),
                shape=values.get("shape", "Circular").strip(),
                diameter=first_float(values.get("diameter", "0")),
                label_detail=values.get("type", ""),
            ))
            continue

        match = re.match(r"^Vent\s+\d+:\s*(.*)$", line)
        if match:
            values, i = parse_simple_block(lines, i)

            # Vent output now mirrors fan output:
            #   shape, diameter, v_size, v_center, v_direction
            # Keep fallbacks for older output.txt files.
            vent_size_text = values.get("v_size", values.get("size", "0 0 0"))
            far = values.get(
                "free_area_ratio",
                values.get("free area ratio", "?")
            )

            rack.openings.append(OpeningGeom(
                name=match.group(1),
                kind="Vent",
                center=three_floats(
                    values.get("v_center", values.get("center", ""))
                ),
                direction=three_floats(
                    values.get("v_direction", values.get("direction", ""))
                ),
                size=three_floats(vent_size_text),
                shape=values.get("shape", "Rectangular").strip(),
                diameter=first_float(values.get("diameter", "0")),
                label_detail=f"FAR={far}",
            ))
            continue
        i += 1
    return rack


def read_spacing(filename: str) -> tuple[float, float, float]:
    with open(filename, "r", encoding="utf-8") as stream:
        stream.readline()
        parts = stream.readline().strip().split(",")
    if len(parts) < 6:
        raise ValueError("Second CSV line must contain dx,value,dy,value,dz,value")
    return float(parts[1]), float(parts[3]), float(parts[5])


def infer_rack(df: pd.DataFrame, dx: float, dy: float, dz: float) -> RackGeom:
    return RackGeom(
        width=(int(df["x"].max()) + 1) * dx,
        depth=(int(df["y"].max()) + 1) * dy,
        height=(int(df["z"].max()) + 1) * dz,
    )


def draw_box_edges(ax, origin, size, **kwargs):
    x0, y0, z0 = origin
    sx, sy, sz = size
    corners = np.array([
        [x0, y0, z0], [x0 + sx, y0, z0], [x0 + sx, y0 + sy, z0], [x0, y0 + sy, z0],
        [x0, y0, z0 + sz], [x0 + sx, y0, z0 + sz], [x0 + sx, y0 + sy, z0 + sz], [x0, y0 + sy, z0 + sz],
    ])
    edges = [(0,1),(1,2),(2,3),(3,0),(4,5),(5,6),(6,7),(7,4),(0,4),(1,5),(2,6),(3,7)]
    for a, b in edges:
        ax.plot(*zip(corners[a], corners[b]), **kwargs)


def dominant_axis(direction) -> int:
    return int(np.argmax(np.abs(np.asarray(direction, dtype=float))))


def draw_opening(ax, opening: OpeningGeom, color):
    x, y, z = opening.center
    direction = np.asarray(opening.direction, dtype=float)
    norm = np.linalg.norm(direction)
    if norm > 0:
        direction /= norm
    axis = dominant_axis(direction)

    circular = opening.shape.lower().startswith("circular") and opening.diameter > 0
    if circular:
        radius = opening.diameter / 2.0
        if axis == 0:
            patch = plt.Circle((y, z), radius, fill=False, color=color, linewidth=2)
            ax.add_patch(patch); art3d.pathpatch_2d_to_3d(patch, z=x, zdir="x")
        elif axis == 1:
            patch = plt.Circle((x, z), radius, fill=False, color=color, linewidth=2)
            ax.add_patch(patch); art3d.pathpatch_2d_to_3d(patch, z=y, zdir="y")
        else:
            patch = plt.Circle((x, y), radius, fill=False, color=color, linewidth=2)
            ax.add_patch(patch); art3d.pathpatch_2d_to_3d(patch, z=z, zdir="z")
        scale = max(opening.diameter, 0.02)
    else:
        sx, sy, sz = opening.size
        if axis == 0:
            patch = plt.Rectangle((y - sy/2, z - sz/2), sy, sz, fill=False, color=color, linewidth=2)
            ax.add_patch(patch); art3d.pathpatch_2d_to_3d(patch, z=x, zdir="x")
            scale = max(sy, sz, 0.02)
        elif axis == 1:
            patch = plt.Rectangle((x - sx/2, z - sz/2), sx, sz, fill=False, color=color, linewidth=2)
            ax.add_patch(patch); art3d.pathpatch_2d_to_3d(patch, z=y, zdir="y")
            scale = max(sx, sz, 0.02)
        else:
            patch = plt.Rectangle((x - sx/2, y - sy/2), sx, sy, fill=False, color=color, linewidth=2)
            ax.add_patch(patch); art3d.pathpatch_2d_to_3d(patch, z=z, zdir="z")
            scale = max(sx, sy, 0.02)
    ax.quiver(x, y, z, *(direction * scale * 0.8), color=color, linewidth=2, arrow_length_ratio=0.25)


def run_native(args: argparse.Namespace) -> None:
    global np, pd, plt, mpatches, FuncAnimation, art3d
    try:
        import numpy as np
        import pandas as pd
        import matplotlib.pyplot as plt
        import matplotlib.patches as mpatches
        from matplotlib.animation import FuncAnimation
        from mpl_toolkits.mplot3d import art3d
    except ImportError as exc:
        raise SystemExit(
            "Native visualization requires NumPy, pandas, and Matplotlib. Install them with:\n"
            "  python -m pip install numpy pandas matplotlib"
        ) from exc

    if not 0.0 <= args.alpha <= 1.0:
        raise SystemExit("--alpha must be between 0 and 1")

    dx, dy, dz = read_spacing(args.sim)
    df = pd.read_csv(args.sim, skiprows=[1])
    try:
        rack = parse_rack_file(args.rack or "output.txt")
    except (FileNotFoundError, ValueError) as exc:
        print(f"Warning: {exc}; inferring rack dimensions from the CSV")
        rack = infer_rack(df, dx, dy, dz)

    steps = sorted(df["step"].unique())[::max(1, args.skip)]
    tmin, tmax = float(df["T"].min()), float(df["T"].max())
    if np.isclose(tmin, tmax):
        tmax = tmin + 1.0

    fig = plt.figure(figsize=(12, 8))
    ax = fig.add_subplot(111, projection="3d")
    fig.subplots_adjust(right=0.82)
    scalar = plt.cm.ScalarMappable(cmap="inferno", norm=plt.Normalize(tmin, tmax))
    cbar = fig.colorbar(scalar, ax=ax, shrink=0.68, pad=0.08)
    cbar.set_label("Temperature (°C)")

    geom_colors = itertools.cycle(plt.rcParams["axes.prop_cycle"].by_key()["color"])
    component_colors = [next(geom_colors) for _ in rack.components]
    opening_colors = [next(geom_colors) for _ in rack.openings]

    def draw_geometry():
        draw_box_edges(ax, (0, 0, 0), (rack.width, rack.depth, rack.height), color="black", linewidth=1.5)
        handles = [mpatches.Patch(facecolor="none", edgecolor="black", label="Rack")]
        seen_region_types: set[str] = set()

        for comp, color in zip(rack.components, component_colors):
            draw_box_edges(ax, comp.origin, (comp.width, comp.depth, comp.height), color=color, linewidth=2)
            handles.append(mpatches.Patch(facecolor="none", edgecolor=color, label=f"Component: {comp.name}"))
            for region in comp.regions:
                kind = region.kind.lower()
                if kind in {"fan", "vent"}:
                    # Fan/vent global_position is its center, unlike the
                    # minimum-corner position used by volumetric regions.
                    draw_opening(ax, OpeningGeom(
                        name=f"{comp.name}: {region.kind}",
                        kind=region.kind,
                        center=region.origin,
                        direction=region.direction,
                        size=region.size,
                        shape="Circular" if region.diameter > 0 else "Rectangular",
                        diameter=region.diameter,
                    ), "limegreen" if kind == "vent" else "dodgerblue")
                    if kind not in seen_region_types:
                        handles.append(mpatches.Patch(
                            facecolor="none",
                            edgecolor="limegreen" if kind == "vent" else "dodgerblue",
                            label=f"Internal region: {region.kind}"))
                        seen_region_types.add(kind)
                    continue
                region_color = "deepskyblue" if kind == "air" else "orangered" if kind == "heatsource" else "limegreen"
                draw_box_edges(ax, region.origin, region.size, color=region_color, linewidth=2.2, linestyle="--")
                if kind not in seen_region_types:
                    handles.append(mpatches.Patch(facecolor="none", edgecolor=region_color,
                                                  linestyle="--", label=f"Internal region: {region.kind}"))
                    seen_region_types.add(kind)

        for opening, color in zip(rack.openings, opening_colors):
            draw_opening(ax, opening, color)
            details = f", {opening.label_detail}" if opening.label_detail else ""
            handles.append(mpatches.Patch(facecolor="none", edgecolor=color,
                                          label=f"{opening.kind}: {opening.name}{details}"))
        return handles

    def update(step):
        ax.clear()
        frame = df[df["step"] == step].iloc[::max(1, args.stride)]
        x = (frame["x"].to_numpy() + 0.5) * dx
        y = (frame["y"].to_numpy() + 0.5) * dy
        z = (frame["z"].to_numpy() + 0.5) * dz
        temperatures = frame["T"].to_numpy()
        is_component = frame["is_component"].to_numpy().astype(bool)

        sizes = np.where(is_component, 70.0, 32.0)
        ax.scatter(x, y, z, c=temperatures, cmap="inferno", vmin=tmin, vmax=tmax,
                   marker="s", s=sizes, alpha=args.alpha, edgecolors="none")

        handles = draw_geometry()
        time_value = float(frame["time"].iloc[0])
        ax.set_xlim(0, rack.width); ax.set_ylim(0, rack.depth); ax.set_zlim(0, rack.height)
        ax.set_box_aspect((rack.width, rack.depth, rack.height))
        ax.set_xlabel("Width, x (m)"); ax.set_ylabel("Depth, y (m)"); ax.set_zlabel("Height, z (m)")
        ax.set_title(f"Rack temperature field — step {step}, time {time_value:.3f} s")
        ax.view_init(elev=24, azim=35)
        ax.legend(handles=handles, loc="upper left", bbox_to_anchor=(1.02, 1.0), fontsize=8)
        return ()

    animation = FuncAnimation(fig, update, frames=steps, interval=1000 / max(1, args.fps), blit=False)
    if args.save:
        animation.save(args.output, fps=args.fps, dpi=180)
        print(f"Saved: {args.output}")
    else:
        plt.show()


def iter_pyvista_datasets(dataset):
    """Yield leaf datasets from an arbitrarily nested PyVista MultiBlock."""
    if dataset is None:
        return
    if hasattr(dataset, "n_blocks"):
        for block in dataset:
            yield from iter_pyvista_datasets(block)
    elif getattr(dataset, "n_cells", 0) or getattr(dataset, "n_points", 0):
        yield dataset


def iter_named_pyvista_datasets(dataset, path=()):
    """Yield ``(block path, leaf)`` pairs from a nested PyVista dataset."""
    if dataset is None:
        return
    if hasattr(dataset, "n_blocks"):
        names = list(dataset.keys()) if hasattr(dataset, "keys") else []
        for index, block in enumerate(dataset):
            name = names[index] if index < len(names) else f"block_{index}"
            yield from iter_named_pyvista_datasets(block, path + (str(name),))
    elif getattr(dataset, "n_cells", 0) or getattr(dataset, "n_points", 0):
        yield path, dataset


def dataset_with_temperature(dataset, temperature_units: str):
    """Return (dataset, scalar_name), accepting either cell or point T data."""
    if "T" not in dataset.cell_data and "T" not in dataset.point_data:
        return None, None
    if temperature_units == "K":
        return dataset, "T"

    converted = dataset.copy(deep=False)
    if "T" in converted.cell_data:
        converted.cell_data["T_C"] = np.asarray(converted.cell_data["T"]) - 273.15
    if "T" in converted.point_data:
        converted.point_data["T_C"] = np.asarray(converted.point_data["T"]) - 273.15
    return converted, "T_C"


def select_openfoam_time(reader, requested: str) -> float:
    times = [float(value) for value in reader.time_values]
    if not times:
        raise ValueError("The OpenFOAM case contains no readable result times")
    if requested.lower() == "latest":
        selected = max(times)
    else:
        target = float(requested)
        selected = min(times, key=lambda value: abs(value - target))
        tolerance = 1.0e-8 * max(1.0, abs(target))
        if abs(selected - target) > tolerance:
            print(f"Warning: time {target:g} is unavailable; using nearest time {selected:g}")
    reader.set_active_time_value(selected)
    return selected


def enable_internal_meshes_only(reader) -> None:
    """Read volume meshes without empty boundary-patch blocks.

    Exported multi-region cases can legitimately contain patches with zero
    faces on some regions. Asking VTK to load every patch produces noisy
    ``mesh contains no cells`` warnings and adds no temperature volume data.
    """
    names = list(getattr(reader, "patch_array_names", ()))
    internal = [
        name for name in names
        if name == "internalMesh"
        or name.endswith("/internalMesh")
        or name.endswith(".internalMesh")
    ]
    if not internal:
        return
    reader.disable_all_patch_arrays()
    for name in internal:
        reader.enable_patch_array(name)


def read_openfoam_internal_meshes(reader):
    """Read selected meshes without warnings from empty processor partitions."""
    warning_state = None
    try:
        import vtk

        warning_state = vtk.vtkObject.GetGlobalWarningDisplay()
        vtk.vtkObject.GlobalWarningDisplayOff()
    except (ImportError, AttributeError):
        vtk = None
    try:
        return reader.read()
    finally:
        if vtk is not None and warning_state:
            vtk.vtkObject.GlobalWarningDisplayOn()


def select_openfoam_animation_times(available, start_time=None, end_time=None,
                                    skip=1):
    """Select an inclusive, ordered subset of written OpenFOAM times."""
    if skip < 1:
        raise ValueError("--skip must be at least 1")
    times = sorted({float(value) for value in available})
    if start_time is not None:
        times = [value for value in times if value >= start_time]
    if end_time is not None:
        times = [value for value in times if value <= end_time]
    if not times:
        raise ValueError("No written OpenFOAM times are inside the requested range")
    return times[::skip]


def add_pyvista_box(plotter, pv, origin, size, color, width=2.0, label=None,
                    style="wireframe"):
    x, y, z = origin
    sx, sy, sz = size
    box = pv.Box(bounds=(x, x + sx, y, y + sy, z, z + sz))
    plotter.add_mesh(box, color=color, style=style, line_width=width,
                     opacity=0.8, label=label)


def add_pyvista_geometry(plotter, pv, rack: RackGeom) -> None:
    add_pyvista_box(
        plotter, pv, (0, 0, 0), (rack.width, rack.depth, rack.height),
        "black", width=3, label="Rack"
    )
    palette = itertools.cycle([
        "dodgerblue", "gold", "mediumorchid", "limegreen", "cyan", "salmon"
    ])
    for comp in rack.components:
        color = next(palette)
        add_pyvista_box(
            plotter, pv, comp.origin, (comp.width, comp.depth, comp.height),
            color, label=f"Component: {comp.name}"
        )
        for region in comp.regions:
            kind = region.kind.lower()
            if kind in {"fan", "vent"}:
                direction = np.asarray(region.direction, dtype=float)
                norm = np.linalg.norm(direction)
                if norm == 0:
                    continue
                direction /= norm
                axis = dominant_axis(direction)
                color = "limegreen" if kind == "vent" else "dodgerblue"
                if region.diameter > 0:
                    opening_mesh = pv.Disc(
                        center=region.origin, inner=0.0,
                        outer=region.diameter / 2.0, normal=direction,
                        r_res=1, c_res=64)
                else:
                    # Build global-axis corners explicitly. pv.Plane chooses
                    # an arbitrary in-plane basis, which can rotate a
                    # rectangular width/height footprint by 90 degrees.
                    x, y, z = region.origin
                    sx, sy, sz = region.size
                    if axis == 0:
                        points = [
                            (x, y-sy/2, z-sz/2), (x, y+sy/2, z-sz/2),
                            (x, y+sy/2, z+sz/2), (x, y-sy/2, z+sz/2)]
                    elif axis == 1:
                        points = [
                            (x-sx/2, y, z-sz/2), (x+sx/2, y, z-sz/2),
                            (x+sx/2, y, z+sz/2), (x-sx/2, y, z+sz/2)]
                    else:
                        points = [
                            (x-sx/2, y-sy/2, z), (x+sx/2, y-sy/2, z),
                            (x+sx/2, y+sy/2, z), (x-sx/2, y+sy/2, z)]
                    opening_mesh = pv.PolyData(
                        np.asarray(points, dtype=float),
                        faces=np.asarray([4, 0, 1, 2, 3]))
                plotter.add_mesh(
                    opening_mesh, color=color, style="wireframe",
                    line_width=3, label=f"Internal region: {region.kind}")
                continue
            region_color = {
                "air": "deepskyblue",
                "heatsource": "orangered",
            }.get(kind, "limegreen")
            add_pyvista_box(
                plotter, pv, region.origin, region.size, region_color,
                width=3, label=f"Internal region: {region.kind}"
            )

    if rack.openings:
        centers = np.asarray([opening.center for opening in rack.openings], dtype=float)
        vectors = np.asarray([opening.direction for opening in rack.openings], dtype=float)
        lengths = np.linalg.norm(vectors, axis=1)
        nonzero = lengths > 0
        vectors[nonzero] /= lengths[nonzero, None]
        scale = max(rack.width, rack.depth, rack.height) * 0.045
        plotter.add_arrows(centers, vectors * scale, color="white", label="Fans / vents")
        plotter.add_point_labels(
            centers, [opening.name for opening in rack.openings],
            font_size=9, point_size=4, text_color="white", shape_opacity=0.25
        )


def prepare_temperature_datasets(multiblock, temperature_units):
    """Return temperature-bearing leaves and their finite scalar ranges."""
    prepared = []
    scalar_ranges = []
    for leaf in iter_pyvista_datasets(multiblock):
        leaf, scalar_name = dataset_with_temperature(leaf, temperature_units)
        if leaf is None:
            continue
        values = (leaf.cell_data.get(scalar_name)
                  if scalar_name in leaf.cell_data else leaf.point_data.get(scalar_name))
        finite = np.asarray(values)[np.isfinite(values)]
        if finite.size:
            scalar_ranges.append((float(finite.min()), float(finite.max())))
        prepared.append((leaf, scalar_name))
    return prepared, scalar_ranges


def temperature_region_statistics(multiblock, temperature_units):
    """Calculate volume-weighted mean and maximum T for each mesh region."""
    candidates = list(iter_named_pyvista_datasets(multiblock))
    internal = [item for item in candidates if "internalMesh" in item[0]]
    if internal:
        candidates = internal
    accumulators = {}
    for path, leaf in candidates:
        leaf, scalar_name = dataset_with_temperature(leaf, temperature_units)
        if leaf is None:
            continue
        region = next(
            (name for name in path
             if name not in {"internalMesh", "boundary"}),
            "rack",
        )
        if scalar_name in leaf.cell_data:
            values = np.asarray(leaf.cell_data[scalar_name], dtype=float)
            try:
                weights = np.asarray(
                    leaf.compute_cell_sizes(
                        length=False, area=False, volume=True
                    ).cell_data["Volume"],
                    dtype=float,
                )
            except (KeyError, TypeError, AttributeError):
                weights = np.ones(values.shape, dtype=float)
        else:
            values = np.asarray(leaf.point_data[scalar_name], dtype=float)
            weights = np.ones(values.shape, dtype=float)
        valid = np.isfinite(values) & np.isfinite(weights) & (weights > 0)
        if not valid.any():
            continue
        stats = accumulators.setdefault(
            region, {"weighted_sum": 0.0, "weight": 0.0, "maximum": -np.inf}
        )
        stats["weighted_sum"] += float(np.sum(values[valid] * weights[valid]))
        stats["weight"] += float(np.sum(weights[valid]))
        stats["maximum"] = max(stats["maximum"], float(np.max(values[valid])))
    return {
        region: {
            "mean": values["weighted_sum"] / values["weight"],
            "maximum": values["maximum"],
        }
        for region, values in accumulators.items()
        if values["weight"] > 0
    }


def temperature_region_hotspots(multiblock, temperature_units):
    """Return each internal mesh region's hottest value and coordinates."""
    candidates = list(iter_named_pyvista_datasets(multiblock))
    internal = [item for item in candidates if "internalMesh" in item[0]]
    if internal:
        candidates = internal
    hotspots = []
    for path, leaf in candidates:
        leaf, scalar_name = dataset_with_temperature(leaf, temperature_units)
        if leaf is None:
            continue
        if scalar_name in leaf.cell_data:
            values = np.asarray(leaf.cell_data[scalar_name])
            points = leaf.cell_centers().points
        else:
            values = np.asarray(leaf.point_data[scalar_name])
            points = leaf.points
        if not values.size or not np.isfinite(values).any():
            continue
        index = int(np.nanargmax(values))
        region = next(
            (name for name in path
             if name not in {"internalMesh", "boundary"}),
            "rack",
        )
        hotspots.append({
            "region": region,
            "maximum": float(values[index]),
            "point": np.asarray(points[index], dtype=float),
        })
    return hotspots


def run_openfoam_convergence_report(args, reader) -> None:
    """Write CSV data and a PNG temperature-history convergence report."""
    try:
        times = select_openfoam_animation_times(
            reader.time_values, args.start_time, args.end_time, args.skip
        )
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    records = []
    regions = set()
    for index, selected_time in enumerate(times, start=1):
        reader.set_active_time_value(selected_time)
        stats = temperature_region_statistics(
            read_openfoam_internal_meshes(reader), args.temperature_units
        )
        if not stats:
            print(
                f"Skipping OpenFOAM time {selected_time:g}: no T field "
                "(function-object-only or incomplete write)"
            )
            continue
        regions.update(stats)
        records.append((selected_time, stats))
        print(f"Read convergence sample {index}/{len(times)}: t={selected_time:g} s")

    if not records:
        raise SystemExit("No complete OpenFOAM time containing T was found")

    output = Path(args.output).expanduser().resolve()
    if output.suffix.lower() not in {".png", ".jpg", ".jpeg"}:
        output = output.with_suffix(".png")
    output.parent.mkdir(parents=True, exist_ok=True)
    csv_output = output.with_suffix(".csv")
    ordered_regions = sorted(regions, key=lambda value: (value.lower() != "fluid", value))
    with csv_output.open("w", newline="", encoding="utf-8") as stream:
        columns = ["time_s"]
        for region in ordered_regions:
            columns.extend((f"{region}_mean", f"{region}_maximum"))
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        for selected_time, stats in records:
            row = {"time_s": selected_time}
            for region in ordered_regions:
                if region in stats:
                    row[f"{region}_mean"] = stats[region]["mean"]
                    row[f"{region}_maximum"] = stats[region]["maximum"]
            writer.writerow(row)

    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise SystemExit(
            "Convergence plotting requires Matplotlib. Install it with: "
            "python -m pip install matplotlib"
        ) from exc
    figure, (rack_axis, component_axis) = plt.subplots(
        2, 1, figsize=(12, 9), sharex=True, constrained_layout=True
    )
    time_values = [record[0] for record in records]
    fluid_regions = [region for region in ordered_regions
                     if region.lower() == "fluid"]
    component_regions = [region for region in ordered_regions
                         if region.lower() != "fluid"]
    for region in fluid_regions:
        means = [stats.get(region, {}).get("mean", np.nan) for _, stats in records]
        maxima = [stats.get(region, {}).get("maximum", np.nan) for _, stats in records]
        rack_axis.plot(time_values, means, label=f"{region} mean", linewidth=2)
        rack_axis.plot(time_values, maxima, "--", label=f"{region} maximum")
    global_maxima = [
        max(value["maximum"] for value in stats.values()) for _, stats in records
    ]
    rack_axis.plot(time_values, global_maxima, color="black", linewidth=2,
                   label="Rack global maximum")
    for region in component_regions:
        means = [stats.get(region, {}).get("mean", np.nan) for _, stats in records]
        maxima = [stats.get(region, {}).get("maximum", np.nan) for _, stats in records]
        line = component_axis.plot(time_values, means, label=f"{region} mean")[0]
        component_axis.plot(time_values, maxima, "--", color=line.get_color(),
                            alpha=0.75, label=f"{region} maximum")
    rack_axis.set_title("Rack / fluid temperature convergence")
    component_axis.set_title("Component temperature convergence")
    component_axis.set_xlabel("Simulation time (s)")
    for axis in (rack_axis, component_axis):
        axis.set_ylabel(f"Temperature ({args.temperature_units})")
        axis.grid(True, alpha=0.3)
        axis.legend(loc="best", fontsize=8)
    figure.savefig(output, dpi=180)
    plt.close(figure)

    final_time, final_stats = records[-1]
    previous = records[-2] if len(records) > 1 else None
    print(f"Saved convergence plot: {output}")
    print(f"Saved convergence data: {csv_output}")
    print(f"Final sample: t={final_time:g} s")
    for region in ordered_regions:
        current = final_stats.get(region)
        if current is None:
            continue
        delta_text = ""
        if previous and region in previous[1]:
            delta = current["maximum"] - previous[1][region]["maximum"]
            elapsed = final_time - previous[0]
            rate = delta / elapsed if elapsed else np.nan
            delta_text = f", max change={delta:+.3g} {args.temperature_units}, rate={rate:+.3g} {args.temperature_units}/s"
        print(
            f"  {region}: mean={current['mean']:.3g}, "
            f"maximum={current['maximum']:.3g} {args.temperature_units}{delta_text}"
        )


def run_openfoam_animation(args, pv, reader, rack) -> None:
    """Render written OpenFOAM result times to an MP4 or GIF."""
    try:
        times = select_openfoam_animation_times(
            reader.time_values, args.start_time, args.end_time, args.skip
        )
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc

    print(f"Scanning {len(times)} OpenFOAM frames for a common temperature scale...")
    ranges = []
    for selected_time in times:
        reader.set_active_time_value(selected_time)
        _, frame_ranges = prepare_temperature_datasets(
            read_openfoam_internal_meshes(reader), args.temperature_units
        )
        ranges.extend(frame_ranges)
    if not ranges:
        raise SystemExit("No T field was found in the requested OpenFOAM time range")
    common_range = (min(value[0] for value in ranges),
                    max(value[1] for value in ranges))

    output = Path(args.output).expanduser().resolve()
    if output.suffix.lower() not in {".mp4", ".gif"}:
        output = output.with_suffix(".mp4")
    output.parent.mkdir(parents=True, exist_ok=True)
    plotter = pv.Plotter(off_screen=True, window_size=(1600, 1000))
    try:
        if output.suffix.lower() == ".gif":
            plotter.open_gif(str(output), fps=args.fps)
        else:
            plotter.open_movie(str(output), framerate=args.fps)
    except Exception as exc:
        raise SystemExit(
            f"Could not create animation '{output}': {exc}. "
            "Install animation support with: "
            "python -m pip install imageio imageio-ffmpeg"
        ) from exc

    axis = args.slice_axis.lower()
    normal_by_axis = {"x": (1, 0, 0), "y": (0, 1, 0), "z": (0, 0, 1)}
    axis_index = {"x": 0, "y": 1, "z": 2}.get(axis)
    camera_position = None
    written = 0
    for frame_number, selected_time in enumerate(times, start=1):
        reader.set_active_time_value(selected_time)
        prepared, _ = prepare_temperature_datasets(
            read_openfoam_internal_meshes(reader), args.temperature_units
        )
        plotter.clear()
        plotted = 0
        for leaf, scalar_name in prepared:
            shown = leaf
            if axis_index is not None:
                position = args.slice_position
                if position is None:
                    position = (leaf.bounds[2 * axis_index]
                                + leaf.bounds[2 * axis_index + 1]) / 2.0
                low, high = leaf.bounds[2 * axis_index:2 * axis_index + 2]
                if position < low or position > high:
                    continue
                origin = list(leaf.center)
                origin[axis_index] = position
                shown = leaf.slice(normal=normal_by_axis[axis], origin=origin)
                if shown.n_cells == 0:
                    continue
            plotter.add_mesh(
                shown, scalars=scalar_name, cmap="inferno", clim=common_range,
                opacity=args.opacity, show_scalar_bar=(plotted == 0),
                scalar_bar_args={"title": f"Temperature ({args.temperature_units})"},
            )
            plotted += 1
        if not plotted:
            raise SystemExit(
                f"The requested view contains no temperature cells at t={selected_time:g}"
            )
        if rack is not None:
            add_pyvista_geometry(plotter, pv, rack)
        plotter.add_text(
            f"OpenFOAM temperature - t = {selected_time:g} s",
            position="upper_left", font_size=13
        )
        plotter.add_axes()
        if camera_position is None:
            plotter.view_isometric()
            plotter.reset_camera()
            camera_position = plotter.camera_position
        else:
            plotter.camera_position = camera_position
        plotter.write_frame()
        written += 1
        print(f"Rendered frame {frame_number}/{len(times)}: t={selected_time:g} s")
    plotter.close()
    print(
        f"Saved {written}-frame OpenFOAM animation: {output}\n"
        f"Temperature range: {common_range[0]:.3g} to {common_range[1]:.3g} "
        f"{args.temperature_units}"
    )


def run_openfoam(args: argparse.Namespace) -> None:
    global np
    try:
        import numpy as np
        import pyvista as pv
    except ImportError as exc:
        raise SystemExit(
            "OpenFOAM visualization requires NumPy and PyVista. Install them with:\n"
            "  python -m pip install pyvista"
        ) from exc

    try:
        from .run_metadata import resolve_case
    except ImportError:  # Direct execution: python plot/heat_animation.py
        from run_metadata import resolve_case
    try:
        case_directory = resolve_case(args.case)
    except (FileNotFoundError, ValueError) as exc:
        raise SystemExit(str(exc)) from exc
    if not (case_directory / "system" / "controlDict").is_file():
        raise SystemExit(f"Not an OpenFOAM case: {case_directory}")

    marker = case_directory / f"{case_directory.name}.foam"
    marker.touch(exist_ok=True)
    warning_state = None
    try:
        import vtk

        warning_state = vtk.vtkObject.GetGlobalWarningDisplay()
        vtk.vtkObject.GlobalWarningDisplayOff()
    except (ImportError, AttributeError):
        vtk = None
    try:
        reader = pv.POpenFOAMReader(str(marker))
        # vtkPOpenFOAMReader defaults to reconstructed mode and will otherwise
        # silently ignore newer processor*/<time> results during a parallel run.
        if any(case_directory.glob("processor[0-9]*")):
            reader.case_type = "decomposed"
        reader.cell_to_point_creation = True
        try:
            enable_internal_meshes_only(reader)
        except AttributeError:
            pass
        selected_time = select_openfoam_time(reader, args.time)
        multiblock = read_openfoam_internal_meshes(reader)
    finally:
        if vtk is not None and warning_state:
            vtk.vtkObject.GlobalWarningDisplayOn()

    rack = None
    rack_path = Path(args.rack) if args.rack else case_directory / "geometry.txt"
    try:
        rack = parse_rack_file(str(rack_path))
    except (FileNotFoundError, ValueError) as exc:
        print(f"Warning: {exc}; showing the exact OpenFOAM mesh without box overlays")

    if args.animate:
        run_openfoam_animation(args, pv, reader, rack)
        return
    if args.convergence_report:
        run_openfoam_convergence_report(args, reader)
        return

    plotter = pv.Plotter(off_screen=args.save, window_size=(1600, 1000))
    plotted = 0
    scalar_ranges: list[tuple[float, float]] = []
    prepared, scalar_ranges = prepare_temperature_datasets(
        multiblock, args.temperature_units
    )

    if not prepared:
        raise SystemExit(
            f"No T field was found at OpenFOAM time {selected_time:g}. "
            "Confirm that this is a written result time."
        )

    mesh_bounds = (
        min(leaf.bounds[0] for leaf, _ in prepared),
        max(leaf.bounds[1] for leaf, _ in prepared),
        min(leaf.bounds[2] for leaf, _ in prepared),
        max(leaf.bounds[3] for leaf, _ in prepared),
        min(leaf.bounds[4] for leaf, _ in prepared),
        max(leaf.bounds[5] for leaf, _ in prepared),
    )
    mesh_size = tuple(
        mesh_bounds[2 * axis + 1] - mesh_bounds[2 * axis] for axis in range(3)
    )
    if rack is not None:
        rack_size = (rack.width, rack.depth, rack.height)
        mismatch = any(
            abs(expected - actual) > 0.05 * max(expected, actual, 1.0e-12)
            for expected, actual in zip(rack_size, mesh_size)
        )
        if mismatch:
            print(
                "Warning: rack geometry dimensions "
                f"{rack_size} do not match OpenFOAM mesh dimensions {mesh_size}; "
                "ignoring the stale geometry overlay"
            )
            rack = None

    common_range = (
        min(value[0] for value in scalar_ranges),
        max(value[1] for value in scalar_ranges),
    )
    axis = args.slice_axis.lower()
    normal_by_axis = {"x": (1, 0, 0), "y": (0, 1, 0), "z": (0, 0, 1)}
    axis_index = {"x": 0, "y": 1, "z": 2}.get(axis)
    slice_position = args.slice_position
    if axis_index is not None and slice_position is None:
        if rack is not None:
            slice_position = mesh_bounds[2 * axis_index] + (
                rack.width, rack.depth, rack.height
            )[axis_index] / 2
        else:
            low = mesh_bounds[2 * axis_index]
            high = mesh_bounds[2 * axis_index + 1]
            slice_position = (low + high) / 2

    hotspots = temperature_region_hotspots(multiblock, args.temperature_units)
    hottest = max(hotspots, key=lambda item: item["maximum"], default=None)
    hottest_value = hottest["maximum"] if hottest is not None else -np.inf
    hottest_point = hottest["point"] if hottest is not None else None
    hottest_region = hottest["region"] if hottest is not None else None
    for leaf, scalar_name in prepared:
        shown = leaf
        if axis != "none":
            bounds = leaf.bounds
            low, high = bounds[2 * axis_index], bounds[2 * axis_index + 1]
            if slice_position < low or slice_position > high:
                continue
            origin = list(leaf.center)
            origin[axis_index] = slice_position
            shown = leaf.slice(normal=normal_by_axis[axis], origin=origin)
            if shown.n_cells == 0:
                continue
        plotter.add_mesh(
            shown, scalars=scalar_name, cmap="inferno", clim=common_range,
            opacity=args.opacity, show_scalar_bar=(plotted == 0),
            scalar_bar_args={"title": f"Temperature ({args.temperature_units})"},
        )
        outline = shown.extract_feature_edges(
            boundary_edges=True, feature_edges=False,
            manifold_edges=False, non_manifold_edges=False
        )
        if outline.n_cells:
            plotter.add_mesh(outline, color="white", line_width=1.2)
        plotted += 1

    if not plotted:
        raise SystemExit("The requested slice does not intersect any temperature region")
    if rack is not None:
        add_pyvista_geometry(plotter, pv, rack)
    if hottest_point is not None:
        coordinates = np.asarray(hottest_point, dtype=float)
        print(
            f"Global hotspot ({hottest_region}): "
            f"{hottest_value:.6g} {args.temperature_units} at "
            f"({coordinates[0]:.9g}, {coordinates[1]:.9g}, "
            f"{coordinates[2]:.9g}) m"
        )
        point = np.asarray(hottest_point, dtype=float).reshape(1, 3)
        plotter.add_mesh(
            pv.Sphere(radius=0.008, center=hottest_point), color="cyan",
            label="Global hotspot"
        )
        plotter.add_point_labels(
            point, [f"Max: {hottest_value:.1f} {args.temperature_units}"],
            font_size=11, point_size=0, text_color="cyan", shape_opacity=0.35
        )

    plotter.add_text(
        f"OpenFOAM temperature — t = {selected_time:g} s",
        position="upper_left", font_size=13
    )
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


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--format", choices=("native", "openfoam"), default="native",
        help="Input type; native preserves the original simulation.csv workflow"
    )
    parser.add_argument("-s", "--save", action="store_true")
    parser.add_argument("--sim", default="simulation.csv")
    parser.add_argument(
        "--rack",
        help="Geometry report; OpenFOAM mode defaults to <case>/geometry.txt"
    )
    parser.add_argument("--case", help="OpenFOAM case directory")
    parser.add_argument("--time", default="latest", help="OpenFOAM time or 'latest'")
    parser.add_argument(
        "--animate", action="store_true",
        help="Animate the selected range of written OpenFOAM times"
    )
    parser.add_argument(
        "--convergence-report", action="store_true",
        help="Save PNG/CSV temperature histories for written OpenFOAM times"
    )
    parser.add_argument("--start-time", type=float, help="First animation time")
    parser.add_argument("--end-time", type=float, help="Last animation time")
    parser.add_argument(
        "--slice-axis", choices=("x", "y", "z", "none"), default="y",
        help="OpenFOAM slice normal; 'none' renders region surfaces"
    )
    parser.add_argument(
        "--slice-position", type=float,
        help="Physical slice coordinate; defaults to each mesh region's midpoint"
    )
    parser.add_argument(
        "--temperature-units", choices=("C", "K"), default="C"
    )
    parser.add_argument("--opacity", type=float, default=0.9)
    parser.add_argument(
        "--alpha", type=float, default=0.38,
        help="Native heat-cell opacity from 0 (transparent) to 1 (opaque)"
    )
    parser.add_argument("--fps", type=int, default=15)
    parser.add_argument("--skip", type=int, default=1)
    parser.add_argument("--output", default="rack_temperature_animation.mp4")
    parser.add_argument("--stride", type=int, default=1, help="Plot every Nth cell")
    return parser


def main() -> None:
    args = build_argument_parser().parse_args()
    if args.format == "openfoam":
        run_openfoam(args)
    else:
        run_native(args)


if __name__ == "__main__":
    main()
