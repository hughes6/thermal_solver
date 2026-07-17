"""
Plots the velocity field (quiver) and local convection coefficient (h)
field from simulation.csv.

Requires the CSV to include vx, vy, vz, and h columns -- add these to
Solver::log_state() the same way qdot/k/rho/cp are already logged:

    logfile << ... << ',' << cell.get_vx() << ',' << cell.get_vy()
            << ',' << cell.get_vz() << ',' << cell.get_h() << '\n';

and add matching headers to the CSV header line.

Usage:
    python plot_flow_fields.py --sim simulation.csv --step 0
    python plot_flow_fields.py --sim simulation.csv --step -1   # last step
"""

import argparse
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401 (registers 3d proj)

parser = argparse.ArgumentParser()
parser.add_argument("--sim", default="simulation.csv")
parser.add_argument("--step", type=int, default=0,
                     help="Step index to plot. -1 = last available step.")
parser.add_argument("--stride", type=int, default=1,
                     help="Only plot every Nth cell's arrow (quiver gets "
                          "cluttered fast on dense meshes).")
args = parser.parse_args()

with open(args.sim, "r") as f:
    header = f.readline().strip()
    spacing_line = f.readline().strip()

parts = spacing_line.split(",")
dx, dy, dz = float(parts[1]), float(parts[3]), float(parts[5])

df = pd.read_csv(args.sim, skiprows=[1])

required = {"vx", "vy", "vz", "h"}
missing = required - set(df.columns)
if missing:
    raise SystemExit(
        f"simulation.csv is missing columns: {missing}. "
        "Add vx,vy,vz,h to Solver::log_state() first (see docstring)."
    )

steps = sorted(df["step"].unique())
step = steps[args.step] if args.step >= 0 else steps[args.step]
frame = df[df["step"] == step].iloc[::args.stride]

x = frame["x"].to_numpy() * dx + dx / 2.0
y = frame["y"].to_numpy() * dy + dy / 2.0
z = frame["z"].to_numpy() * dz + dz / 2.0

vx = frame["vx"].to_numpy()
vy = frame["vy"].to_numpy()
vz = frame["vz"].to_numpy()
h = frame["h"].to_numpy()
is_component = frame["is_component"].to_numpy()

speed = np.sqrt(vx**2 + vy**2 + vz**2)

# =========================
# Figure 1: velocity quiver
# =========================
fig1 = plt.figure(figsize=(10, 7))
ax1 = fig1.add_subplot(111, projection="3d")

ax1.scatter(
    x[is_component == 1], y[is_component == 1], z[is_component == 1],
    color="black", alpha=0.3, s=60, marker="s", label="Component"
)

# normalize arrow length for readability -- raw velocities in a rack
# duct can vary by orders of magnitude between near-fan and stagnant
# regions, which makes a literal-scale quiver mostly invisible arrows.
max_speed = speed.max() if speed.max() > 0 else 1.0
arrow_scale = 0.5 * dx / max_speed

# Axes3D.quiver does NOT accept a data array + cmap the way 2D quiver
# does -- it only takes X,Y,Z,U,V,W positionally, plus keyword args.
# To color arrows by speed, map speed -> RGBA colors ourselves, then
# repeat each color 3x: matplotlib draws every 3D arrow as 3 line
# segments (the shaft, plus two short strokes for the arrowhead), and
# `colors` must have one entry per segment, in that repeated order.
norm = plt.Normalize(vmin=speed.min(), vmax=speed.max())
arrow_colors = plt.cm.viridis(norm(speed))
arrow_colors_repeated = np.repeat(arrow_colors, 3, axis=0)

q = ax1.quiver(
    x, y, z,
    vx * arrow_scale, vy * arrow_scale, vz * arrow_scale,
    colors=arrow_colors_repeated, length=1.0, normalize=False
)

ax1.set_xlabel("Width (m)")
ax1.set_ylabel("Depth (m)")
ax1.set_zlabel("Height (m)")
ax1.set_title(f"Velocity field, step={step}")
fig1.colorbar(plt.cm.ScalarMappable(cmap="viridis",
              norm=plt.Normalize(vmin=speed.min(), vmax=speed.max())),
              ax=ax1, shrink=0.6, label="|v| (m/s)")

# =========================
# Figure 2: h (convection coefficient) field
# =========================
fig2 = plt.figure(figsize=(10, 7))
ax2 = fig2.add_subplot(111, projection="3d")

sc = ax2.scatter(
    x, y, z, c=h, cmap="inferno", s=60, marker="s", alpha=0.85
)

ax2.set_xlabel("Width (m)")
ax2.set_ylabel("Depth (m)")
ax2.set_zlabel("Height (m)")
ax2.set_title(f"Local convection coefficient h, step={step}")
fig2.colorbar(sc, ax=ax2, shrink=0.6, label="h (W/m^2-K)")

plt.show()