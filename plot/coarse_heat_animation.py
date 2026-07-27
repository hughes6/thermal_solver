"""Animate the coarse/small-mesh thermal field as slices plus a 3-D cutaway.

The default input is the multistage warm-start output:

    python plot/coarse_heat_animation.py
    python plot/coarse_heat_animation.py --snapshot
    python plot/coarse_heat_animation.py --save
    python plot/coarse_heat_animation.py --save --output coarse_heat.gif

The three slice panels show temperature and in-plane velocity.  The 3-D panel
shows all solid cells plus fluid cells hotter than the selected percentile.
"""
from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation, PillowWriter
from matplotlib.colors import Normalize
import numpy as np
import pandas as pd

from coarse_heat_io import read_spacing, temperature_limits, validate_columns
from heat_animation import draw_box_edges, parse_rack_file


def load_simulation(path: str) -> tuple[pd.DataFrame, tuple[float, float, float]]:
    spacing = read_spacing(path)
    frame = pd.read_csv(path, skiprows=[1])
    validate_columns(frame.columns)
    for key in ("step", "x", "y", "z"):
        frame[key] = frame[key].astype(int)
    return frame, spacing


def physical_centers(values: np.ndarray, spacing: float) -> np.ndarray:
    return (values.astype(float) + 0.5) * spacing


def draw_slice(
    ax,
    frame: pd.DataFrame,
    fixed_axis: str,
    fixed_index: int,
    horizontal: str,
    vertical: str,
    velocity_horizontal: str,
    velocity_vertical: str,
    spacings: dict[str, float],
    norm: Normalize,
    cmap: str,
    quiver_stride: int,
) -> None:
    selected = frame[frame[fixed_axis] == fixed_index]
    horizontal_indices = np.sort(selected[horizontal].unique())
    vertical_indices = np.sort(selected[vertical].unique())

    temperature = (
        selected.pivot(index=vertical, columns=horizontal, values="T")
        .reindex(index=vertical_indices, columns=horizontal_indices)
        .to_numpy()
    )
    h_edges = np.arange(horizontal_indices.size + 1) * spacings[horizontal]
    v_edges = np.arange(vertical_indices.size + 1) * spacings[vertical]
    ax.pcolormesh(
        h_edges, v_edges, temperature,
        cmap=cmap, norm=norm, shading="flat", rasterized=True,
    )

    solids = selected[selected["is_component"].astype(bool)]
    if not solids.empty:
        ax.scatter(
            physical_centers(solids[horizontal].to_numpy(), spacings[horizontal]),
            physical_centers(solids[vertical].to_numpy(), spacings[vertical]),
            marker="s", s=8, facecolors="none", edgecolors="white",
            linewidths=0.35, alpha=0.55,
        )

    stride = max(1, quiver_stride)
    velocity = selected[
        (selected[horizontal] % stride == 0)
        & (selected[vertical] % stride == 0)
        & (~selected["is_component"].astype(bool))
    ]
    if not velocity.empty:
        u = velocity[velocity_horizontal].to_numpy(dtype=float)
        v = velocity[velocity_vertical].to_numpy(dtype=float)
        speed = np.hypot(u, v)
        nonzero = speed > 1e-10
        velocity = velocity.loc[nonzero]
        u, v, speed = u[nonzero], v[nonzero], speed[nonzero]
        if speed.size:
            # Normalize arrows so direction remains readable despite a few
            # high-speed fan cells. Color still communicates relative speed.
            u = u / speed
            v = v / speed
            ax.quiver(
                physical_centers(
                    velocity[horizontal].to_numpy(), spacings[horizontal]),
                physical_centers(
                    velocity[vertical].to_numpy(), spacings[vertical]),
                u, v, speed,
                cmap="winter", angles="xy", scale_units="xy",
                scale=12.0 / max(spacings[horizontal], spacings[vertical]),
                width=0.003, alpha=0.75,
            )

    coordinate = (fixed_index + 0.5) * spacings[fixed_axis]
    ax.set_title(f"{fixed_axis.upper()} midplane = {coordinate:.3f} m")
    ax.set_xlabel(f"{horizontal} (m)")
    ax.set_ylabel(f"{vertical} (m)")
    ax.set_aspect("equal", adjustable="box")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Animate a small/coarse thermal mesh with slice views.")
    parser.add_argument("--sim", default="coarse_simulation.csv")
    parser.add_argument("--rack", default="output.txt")
    parser.add_argument("--save", action="store_true",
                        help="Save an MP4 or GIF instead of opening a window")
    parser.add_argument("--snapshot", action="store_true",
                        help="Save the final displayed frame as a PNG")
    parser.add_argument("--output", default="coarse_temperature_animation.mp4")
    parser.add_argument("--snapshot-output", default="coarse_temperature.png")
    parser.add_argument("--fps", type=int, default=12)
    parser.add_argument("--skip", type=int, default=1,
                        help="Animate every Nth recorded timestep")
    parser.add_argument("--quiver-stride", type=int, default=2,
                        help="Plot one velocity arrow every N slice cells")
    parser.add_argument("--hot-percentile", type=float, default=75.0,
                        help="Fluid percentile shown in the 3-D cutaway")
    parser.add_argument("--ambient", type=float, default=None,
                        help="Force the lower temperature color limit")
    parser.add_argument("--elev", type=float, default=24.0)
    parser.add_argument("--azim", type=float, default=35.0)
    args = parser.parse_args()

    df, (dx, dy, dz) = load_simulation(args.sim)
    spacings = {"x": dx, "y": dy, "z": dz}
    steps = sorted(df["step"].unique())[::max(1, args.skip)]
    if not steps:
        raise ValueError(f"No timesteps found in {args.sim}")

    nx = int(df["x"].max()) + 1
    ny = int(df["y"].max()) + 1
    nz = int(df["z"].max()) + 1
    mid = {"x": nx // 2, "y": ny // 2, "z": nz // 2}
    extents = (nx * dx, ny * dy, nz * dz)
    tmin, tmax = temperature_limits(df["T"], args.ambient)
    norm = Normalize(tmin, tmax)
    cmap = "inferno"

    try:
        rack = parse_rack_file(args.rack)
    except (FileNotFoundError, ValueError) as exc:
        print(f"Warning: {exc}; using CSV mesh dimensions for rack outline")
        rack = None

    fig = plt.figure(figsize=(15, 9), constrained_layout=True)
    grid = fig.add_gridspec(2, 3, width_ratios=(1.0, 1.0, 1.25))
    ax_x = fig.add_subplot(grid[0, 0])
    ax_y = fig.add_subplot(grid[0, 1])
    ax_z = fig.add_subplot(grid[1, 0:2])
    ax_3d = fig.add_subplot(grid[:, 2], projection="3d")
    scalar = plt.cm.ScalarMappable(norm=norm, cmap=cmap)
    colorbar = fig.colorbar(scalar, ax=[ax_x, ax_y, ax_z, ax_3d],
                            shrink=0.76, pad=0.025)
    colorbar.set_label("Temperature (°C)")

    def update(step: int):
        for axis in (ax_x, ax_y, ax_z, ax_3d):
            axis.clear()
        frame = df[df["step"] == step]

        draw_slice(
            ax_x, frame, "x", mid["x"], "y", "z", "vy", "vz",
            spacings, norm, cmap, args.quiver_stride)
        draw_slice(
            ax_y, frame, "y", mid["y"], "x", "z", "vx", "vz",
            spacings, norm, cmap, args.quiver_stride)
        draw_slice(
            ax_z, frame, "z", mid["z"], "x", "y", "vx", "vy",
            spacings, norm, cmap, args.quiver_stride)

        solids = frame[frame["is_component"].astype(bool)]
        fluids = frame[~frame["is_component"].astype(bool)]
        hot_threshold = (
            float(np.percentile(fluids["T"], args.hot_percentile))
            if not fluids.empty else tmax
        )
        hot_fluid = fluids[fluids["T"] >= hot_threshold]

        for selected, marker, size, alpha in (
            (hot_fluid, "o", 13, 0.38),
            (solids, "s", 25, 0.82),
        ):
            if selected.empty:
                continue
            ax_3d.scatter(
                physical_centers(selected["x"].to_numpy(), dx),
                physical_centers(selected["y"].to_numpy(), dy),
                physical_centers(selected["z"].to_numpy(), dz),
                c=selected["T"], cmap=cmap, norm=norm,
                marker=marker, s=size, alpha=alpha,
                edgecolors="none", depthshade=False,
            )

        if rack is not None:
            draw_box_edges(
                ax_3d, (0.0, 0.0, 0.0),
                (rack.width, rack.depth, rack.height),
                color="black", linewidth=1.3)
            for component in rack.components:
                draw_box_edges(
                    ax_3d, component.origin,
                    (component.width, component.depth, component.height),
                    color="deepskyblue", linewidth=1.15, alpha=0.75)
        else:
            draw_box_edges(
                ax_3d, (0.0, 0.0, 0.0), extents,
                color="black", linewidth=1.3)

        ax_3d.set_xlim(0.0, extents[0])
        ax_3d.set_ylim(0.0, extents[1])
        ax_3d.set_zlim(0.0, extents[2])
        ax_3d.set_box_aspect(extents)
        ax_3d.set_xlabel("x (m)")
        ax_3d.set_ylabel("y (m)")
        ax_3d.set_zlabel("z (m)")
        ax_3d.set_title(
            f"3-D cutaway\nsolids + hottest {100-args.hot_percentile:.0f}% fluid")
        ax_3d.view_init(elev=args.elev, azim=args.azim)

        time_value = float(frame["time"].iloc[0])
        fig.suptitle(
            f"Coarse-mesh thermal field — step {step}, "
            f"time {time_value:.3f} s\n"
            f"T = {frame['T'].min():.3f} to {frame['T'].max():.3f} °C",
            fontsize=15,
        )
        return ()

    if args.snapshot:
        update(steps[-1])
        fig.savefig(args.snapshot_output, dpi=190, bbox_inches="tight")
        print(f"Saved: {args.snapshot_output}")
        return

    animation = FuncAnimation(
        fig, update, frames=steps,
        interval=1000 / max(1, args.fps), blit=False)
    if args.save:
        output = Path(args.output)
        if output.suffix.lower() == ".gif":
            animation.save(output, writer=PillowWriter(fps=args.fps), dpi=130)
        else:
            animation.save(output, fps=args.fps, dpi=150)
        print(f"Saved: {output}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
