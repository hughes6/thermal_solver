"""Animate the 3-D rack temperature field with exported rack geometry.

Usage:
  python heat_animation.py --sim simulation.csv --rack output.txt
  python heat_animation.py --sim simulation.csv --rack output.txt --save
"""
from __future__ import annotations

import argparse
import itertools
import re
from dataclasses import dataclass, field
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.animation import FuncAnimation
from mpl_toolkits.mplot3d import art3d
import numpy as np
import pandas as pd


@dataclass
class InternalRegionGeom:
    kind: str
    size: tuple[float, float, float]
    origin: tuple[float, float, float]


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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("-s", "--save", action="store_true")
    parser.add_argument("--sim", default="simulation.csv")
    parser.add_argument("--rack", default="output.txt")
    parser.add_argument("--fps", type=int, default=15)
    parser.add_argument("--skip", type=int, default=1)
    parser.add_argument("--output", default="rack_temperature_animation.mp4")
    parser.add_argument("--stride", type=int, default=1, help="Plot every Nth cell")
    args = parser.parse_args()

    dx, dy, dz = read_spacing(args.sim)
    df = pd.read_csv(args.sim, skiprows=[1])
    try:
        rack = parse_rack_file(args.rack)
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
                   marker="s", s=sizes, alpha=0.72, edgecolors="none")

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


if __name__ == "__main__":
    main()