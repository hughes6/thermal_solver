#!/usr/bin/env python3
"""Plot a true-size 2D slice of an adaptive field.csv mesh."""

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.collections import PatchCollection
from matplotlib.colors import Normalize
from matplotlib.patches import Rectangle
import numpy as np
import pandas as pd


def parse_args():
    parser = argparse.ArgumentParser(
        description="Plot adaptive cell boundaries and an optional field variable.")
    parser.add_argument("--file", default="simulation_output/field.csv")
    parser.add_argument("--axis", choices=("x", "y", "z"), default="z",
                        help="Slice normal axis.")
    parser.add_argument("--position", type=float,
                        help="Requested slice position in meters; nearest layer is used.")
    parser.add_argument("--variable", default="T",
                        help="Field column to color. Use 'none' for mesh lines only.")
    parser.add_argument("--step", type=int,
                        help="Timestep to plot; defaults to the latest.")
    parser.add_argument("--cmap", default="inferno")
    parser.add_argument("--vmin", type=float)
    parser.add_argument("--vmax", type=float)
    parser.add_argument("--output", help="Save the figure instead of opening a window.")
    return parser.parse_args()


def load_slice(args):
    path = Path(args.file)
    if not path.exists():
        raise FileNotFoundError(f"Field log not found: {path}")
    frame = pd.read_csv(path)
    required = {"step", "x", "y", "z", "dx", "dy", "dz"}
    missing = required.difference(frame.columns)
    if missing:
        raise ValueError(
            "Adaptive plotting requires a v1.8 field log with columns: "
            + ", ".join(sorted(missing)))

    step = int(frame["step"].max()) if args.step is None else args.step
    frame = frame.loc[frame["step"] == step].copy()
    if frame.empty:
        raise ValueError(f"No rows found for timestep {step}.")

    layers = np.sort(frame[args.axis].unique())
    target = layers[len(layers) // 2] if args.position is None else args.position
    selected = float(layers[np.argmin(np.abs(layers - target))])
    tolerance = max(1e-12, np.finfo(float).eps * max(1.0, abs(selected)) * 8)
    return frame.loc[np.abs(frame[args.axis] - selected) <= tolerance], step, selected


def plot(args):
    frame, step, position = load_slice(args)
    plane_axes = {
        "x": ("y", "z"),
        "y": ("x", "z"),
        "z": ("x", "y"),
    }
    horizontal, vertical = plane_axes[args.axis]

    rectangles = [
        Rectangle(
            (row[horizontal] - row["d" + horizontal] / 2,
             row[vertical] - row["d" + vertical] / 2),
            row["d" + horizontal],
            row["d" + vertical])
        for _, row in frame.iterrows()
    ]

    fig, ax = plt.subplots(figsize=(9, 7))
    variable = args.variable.lower()
    if variable == "none":
        collection = PatchCollection(
            rectangles, facecolor="none", edgecolor="black", linewidth=0.45)
    else:
        if args.variable not in frame.columns:
            raise ValueError(
                f"Column '{args.variable}' is not present. Available columns: "
                + ", ".join(frame.columns))
        values = frame[args.variable].to_numpy(dtype=float)
        finite = values[np.isfinite(values)]
        if finite.size == 0:
            raise ValueError(f"Column '{args.variable}' contains no finite values.")
        vmin = float(finite.min()) if args.vmin is None else args.vmin
        vmax = float(finite.max()) if args.vmax is None else args.vmax
        if vmax < vmin:
            vmin, vmax = vmax, vmin
        if np.isclose(vmin, vmax):
            padding = max(abs(vmin) * 1e-6, 1e-9)
            vmin, vmax = vmin - padding, vmax + padding
        collection = PatchCollection(
            rectangles, cmap=args.cmap, norm=Normalize(vmin=vmin, vmax=vmax),
            edgecolor=(0, 0, 0, 0.5), linewidth=0.35)
        collection.set_array(values)
        fig.colorbar(collection, ax=ax, label=args.variable)

    ax.add_collection(collection)
    ax.autoscale()
    ax.set_aspect("equal")
    ax.set_xlabel(f"{horizontal} (m)")
    ax.set_ylabel(f"{vertical} (m)")
    ax.set_title(
        f"Adaptive mesh: {args.axis}={position:.6g} m, step={step}")
    fig.tight_layout()

    if args.output:
        fig.savefig(args.output, dpi=180)
        print(f"Saved {args.output}")
    else:
        plt.show()


if __name__ == "__main__":
    plot(parse_args())
