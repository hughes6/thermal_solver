"""Animate temperature and velocity contours through the rack midplanes.

The figure contains the three central planes:
  XY: horizontal plane at mid-height
  XZ: front-to-back vertical plane at mid-depth
  YZ: side vertical plane at mid-width

Top row: temperature contours.
Bottom row: velocity-magnitude contours with in-plane velocity arrows.

Usage:
  python midplane_contours.py --sim simulation.csv
  python midplane_contours.py --sim simulation.csv --save
  python midplane_contours.py --sim simulation.csv --step -1
"""
from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import numpy as np
import pandas as pd



@dataclass
class InternalRegion:
    kind: str
    origin: tuple[float, float, float]
    size: tuple[float, float, float]


def _numbers(text: str) -> list[float]:
    return [float(v) for v in re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", text)]


def read_internal_regions(filename: str) -> list[InternalRegion]:
    lines = [line.strip() for line in Path(filename).read_text().splitlines() if line.strip()]
    regions: list[InternalRegion] = []
    i = 0
    while i < len(lines):
        if not re.match(r"^Internal Region\s+\d+:$", lines[i]):
            i += 1
            continue
        values: dict[str, str] = {}
        i += 1
        while i < len(lines) and not re.match(
            r"^(Internal Region\s+\d+:|Component\s+\d+:|Fan\s+\d+:|Vent\s+\d+:)", lines[i]
        ):
            if ":" in lines[i]:
                key, value = lines[i].split(":", 1)
                values[key.strip().lower()] = value.strip()
            i += 1
        if {"type", "size", "global_position"} <= values.keys():
            regions.append(InternalRegion(
                values["type"],
                tuple(_numbers(values["global_position"])[:3]),
                tuple(_numbers(values["size"])[:3]),
            ))
    return regions


def draw_region_intersections(ax, regions, fixed_axis, fixed_position, horizontal, vertical):
    axis_index = {"x": 0, "y": 1, "z": 2}
    fi, hi, vi = axis_index[fixed_axis], axis_index[horizontal], axis_index[vertical]
    used = set()
    for region in regions:
        lo = region.origin[fi]
        hi_fixed = lo + region.size[fi]
        if not (lo <= fixed_position <= hi_fixed):
            continue
        color = "deepskyblue" if region.kind.lower() == "air" else "orangered" if region.kind.lower() == "heatsource" else "limegreen"
        rect = plt.Rectangle(
            (region.origin[hi], region.origin[vi]),
            region.size[hi], region.size[vi],
            fill=False, edgecolor=color, linewidth=2.0, linestyle="--",
            label=f"Internal {region.kind}" if region.kind not in used else None,
        )
        ax.add_patch(rect)
        used.add(region.kind)


def read_spacing(filename: str) -> tuple[float, float, float]:
    with open(filename, "r", encoding="utf-8") as stream:
        stream.readline()
        parts = stream.readline().strip().split(",")
    if len(parts) < 6:
        raise ValueError("Second CSV line must contain dx,value,dy,value,dz,value")
    return float(parts[1]), float(parts[3]), float(parts[5])


def plane_array(frame: pd.DataFrame, fixed_axis: str, fixed_index: int,
                horizontal: str, vertical: str, value: str) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    plane = frame[frame[fixed_axis] == fixed_index]

    # pivot() requires exactly one row per grid location. A simulation CSV may
    # contain duplicate records when the initial state is logged more than once.
    # pivot_table(..., aggfunc="last") keeps the most recently written value for
    # that cell and prevents the animation from failing on duplicated rows.
    table = plane.pivot_table(
        index=vertical,
        columns=horizontal,
        values=value,
        aggfunc="last",
    ).sort_index().sort_index(axis=1)
    h_indices = table.columns.to_numpy(dtype=int)
    v_indices = table.index.to_numpy(dtype=int)
    return h_indices, v_indices, table.to_numpy(dtype=float)


def edges_from_centers(centers: np.ndarray, spacing: float) -> np.ndarray:
    if len(centers) == 1:
        return np.array([centers[0] - spacing / 2, centers[0] + spacing / 2])
    return np.concatenate(([centers[0] - spacing/2], (centers[:-1] + centers[1:])/2, [centers[-1] + spacing/2]))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sim", default="simulation.csv")
    parser.add_argument("--rack", default="output.txt", help="Geometry export containing internal regions")
    parser.add_argument("--fps", type=int, default=12)
    parser.add_argument("--skip", type=int, default=1, help="Use every Nth simulation step")
    parser.add_argument("--arrow-stride", type=int, default=1)
    parser.add_argument("--levels", type=int, default=24)
    parser.add_argument("--step", type=int, default=None, help="Show one step only; -1 means final step")
    parser.add_argument("--save", action="store_true")
    parser.add_argument("--output", default="midplane_temperature_velocity.mp4")
    args = parser.parse_args()

    dx, dy, dz = read_spacing(args.sim)
    df = pd.read_csv(args.sim, skiprows=[1])
    try:
        internal_regions = read_internal_regions(args.rack)
    except (FileNotFoundError, ValueError) as exc:
        print(f"Warning: {exc}; internal-region outlines will be omitted")
        internal_regions = []
    required = {"step", "time", "x", "y", "z", "T", "vx", "vy", "vz", "is_component"}
    missing = required - set(df.columns)
    if missing:
        raise SystemExit(f"Missing CSV columns: {sorted(missing)}")

    duplicate_mask = df.duplicated(subset=["step", "x", "y", "z"], keep=False)
    duplicate_count = int(duplicate_mask.sum())
    if duplicate_count:
        affected_steps = sorted(df.loc[duplicate_mask, "step"].unique().tolist())
        print(
            f"Warning: found {duplicate_count} duplicated cell rows in step(s) "
            f"{affected_steps}. Keeping the last record for each cell."
        )
        df = df.drop_duplicates(subset=["step", "x", "y", "z"], keep="last").copy()

    df["speed"] = np.sqrt(df["vx"]**2 + df["vy"]**2 + df["vz"]**2)
    steps = sorted(df["step"].unique())[::max(1, args.skip)]
    if args.step is not None:
        selected = steps[args.step] if args.step < 0 else args.step
        if selected not in set(df["step"].unique()):
            raise SystemExit(f"Step {selected} is not present in the CSV")
        steps = [selected]

    nx = int(df["x"].max()) + 1
    ny = int(df["y"].max()) + 1
    nz = int(df["z"].max()) + 1
    ix, iy, iz = nx // 2, ny // 2, nz // 2

    tmin, tmax = float(df["T"].min()), float(df["T"].max())
    smin, smax = 0.0, float(df["speed"].max())
    if np.isclose(tmin, tmax): tmax = tmin + 1.0
    if np.isclose(smin, smax): smax = smin + 1e-9

    t_levels = np.linspace(tmin, tmax, max(3, args.levels))
    s_levels = np.linspace(smin, smax, max(3, args.levels))

    planes = [
        dict(name="XY mid-height", fixed="z", index=iz, h="x", v="y", hs=dx, vs=dy,
             xlabel="Width, x (m)", ylabel="Depth, y (m)", u="vx", w="vy"),
        dict(name="XZ mid-depth", fixed="y", index=iy, h="x", v="z", hs=dx, vs=dz,
             xlabel="Width, x (m)", ylabel="Height, z (m)", u="vx", w="vz"),
        dict(name="YZ mid-width", fixed="x", index=ix, h="y", v="z", hs=dy, vs=dz,
             xlabel="Depth, y (m)", ylabel="Height, z (m)", u="vy", w="vz"),
    ]

    fig, axes = plt.subplots(2, 3, figsize=(15, 9), constrained_layout=True)
    temp_map = plt.cm.ScalarMappable(norm=plt.Normalize(tmin, tmax), cmap="inferno")
    speed_map = plt.cm.ScalarMappable(norm=plt.Normalize(smin, smax), cmap="viridis")
    fig.colorbar(temp_map, ax=axes[0, :], shrink=0.85, label="Temperature (°C)")
    fig.colorbar(speed_map, ax=axes[1, :], shrink=0.85, label="Velocity magnitude (m/s)")

    def draw(step):
        frame = df[df["step"] == step]
        time_value = float(frame["time"].iloc[0])

        for col, plane in enumerate(planes):
            temp_ax = axes[0, col]
            vel_ax = axes[1, col]
            temp_ax.clear(); vel_ax.clear()

            hi, vi, temp = plane_array(frame, plane["fixed"], plane["index"], plane["h"], plane["v"], "T")
            _, _, speed = plane_array(frame, plane["fixed"], plane["index"], plane["h"], plane["v"], "speed")
            _, _, mask = plane_array(frame, plane["fixed"], plane["index"], plane["h"], plane["v"], "is_component")
            _, _, u = plane_array(frame, plane["fixed"], plane["index"], plane["h"], plane["v"], plane["u"])
            _, _, w = plane_array(frame, plane["fixed"], plane["index"], plane["h"], plane["v"], plane["w"])

            hc = (hi + 0.5) * plane["hs"]
            vc = (vi + 0.5) * plane["vs"]
            he = edges_from_centers(hc, plane["hs"])
            ve = edges_from_centers(vc, plane["vs"])

            temp_ax.contourf(hc, vc, temp, levels=t_levels, cmap="inferno", extend="both")
            vel_ax.contourf(hc, vc, speed, levels=s_levels, cmap="viridis", extend="max")

            # Outline solid/component cells on both fields.
            if np.any(mask > 0.5) and np.any(mask < 0.5):
                temp_ax.contour(hc, vc, mask, levels=[0.5], colors="white", linewidths=1.4)
                vel_ax.contour(hc, vc, mask, levels=[0.5], colors="white", linewidths=1.4)

            stride = max(1, args.arrow_stride)
            H, V = np.meshgrid(hc, vc)
            fluid = mask < 0.5
            uq = np.where(fluid, u, np.nan)
            wq = np.where(fluid, w, np.nan)
            vel_ax.quiver(H[::stride, ::stride], V[::stride, ::stride],
                          uq[::stride, ::stride], wq[::stride, ::stride],
                          angles="xy", scale_units="xy", scale=None, width=0.003)

            for ax in (temp_ax, vel_ax):
                ax.set_xlim(he[0], he[-1]); ax.set_ylim(ve[0], ve[-1])
                ax.set_aspect("equal", adjustable="box")
                ax.set_xlabel(plane["xlabel"]); ax.set_ylabel(plane["ylabel"])

            fixed_position = (plane["index"] + 0.5) * {"x": dx, "y": dy, "z": dz}[plane["fixed"]]
            draw_region_intersections(temp_ax, internal_regions, plane["fixed"], fixed_position, plane["h"], plane["v"])
            draw_region_intersections(vel_ax, internal_regions, plane["fixed"], fixed_position, plane["h"], plane["v"])
            temp_ax.set_title(f"Temperature — {plane['name']}\n{plane['fixed']}={fixed_position:.4f} m")
            vel_ax.set_title(f"Speed and in-plane vectors — {plane['name']}")

        fig.suptitle(f"Rack midplane fields — step {step}, time {time_value:.3f} s", fontsize=15)
        return ()

    if args.step is not None:
        draw(steps[0])
        if args.save:
            output = Path(args.output)
            if output.suffix.lower() not in {".png", ".jpg", ".jpeg", ".pdf", ".svg"}:
                output = output.with_suffix(".png")
            fig.savefig(output, dpi=200)
            print(f"Saved: {output}")
        else:
            plt.show()
        return

    animation = FuncAnimation(fig, draw, frames=steps, interval=1000 / max(1, args.fps), blit=False)
    if args.save:
        animation.save(args.output, fps=args.fps, dpi=160)
        print(f"Saved: {args.output}")
    else:
        plt.show()


if __name__ == "__main__":
    main()