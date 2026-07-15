import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.animation import FuncAnimation
import pandas as pd
from mpl_toolkits.mplot3d import art3d
import numpy as np
import argparse
import itertools
import re


parser = argparse.ArgumentParser()
parser.add_argument("-s", "--save", action="store_true")
parser.add_argument("--sim", default="simulation.csv")
parser.add_argument("--rack", default="output.txt")
parser.add_argument("--fps", type=int, default=15)
parser.add_argument("--skip", type=int, default=1)
args = parser.parse_args()


def parse_rack_file(filename):
    with open(filename) as f:
        lines = [line.strip() for line in f if line.strip()]

    rack_h = rack_w = rack_d = None
    component_names = []
    component_dims = []
    component_coords = []
    fan_names = []
    fan_types = []
    fan_diams = []
    fan_centers = []
    fan_velocity_dirs = []

    i = 0
    while i < len(lines):
        line = lines[i]

        if line.startswith("height:"):
            rack_h = float(line.split()[1])

        elif line.startswith("width:"):
            rack_w = float(line.split()[1])

        elif line.startswith("depth:"):
            rack_d = float(line.split()[1])

        elif line.startswith("Component "):
            name = line.split(":", 1)[1].strip()

            dim_line = None
            coord_line = None

            j = i + 1
            while j < len(lines):
                if lines[j].startswith("Component ") or lines[j].startswith("Fan "):
                    break

                if lines[j].startswith("dimensions:"):
                    dim_line = lines[j]

                elif lines[j].startswith("coordinates:"):
                    coord_line = lines[j]

                j += 1

            if dim_line is None or coord_line is None:
                raise ValueError(f"Missing dimensions/coordinates for {name}")

            dims_text = dim_line.split(":", 1)[1].replace("m", "").strip()
            dims = [float(v.strip()) for v in dims_text.split("x")]

            coords_text = coord_line.split(":", 1)[1].replace("m", "").strip()
            coords = [float(v.strip()) for v in coords_text.split()]

            component_names.append(name)
            component_dims.append(dims)
            component_coords.append(coords)

            i = j - 1
        elif line.startswith("Fan "):
            name = line.split(":", 1)[1].strip()

            type_line = None
            cfm_line = None
            diam_line = None 
            center_line = None
            dir_line = None 

            j = i + 1
            while j < len(lines):
                if re.match(r"^Fan\s+\d+:", lines[j]) or lines[j].startswith("Component "):
                    break

                if lines[j].startswith("type:"):
                    type_line = lines[j]
                elif lines[j].startswith("cfm:"):
                    cfm_line = lines[j]
                elif lines[j].startswith("diameter:"):
                    diam_line = lines[j]
                elif lines[j].startswith("center:"):
                    center_line = lines[j]
                elif lines[j].startswith("direction:"):
                    dir_line = lines[j]

                j += 1

            if type_line is None or diam_line is None or center_line is None or dir_line is None:
                raise ValueError(f"Could not find fan data for {name}")

            type_text = type_line.split(":", 1)[1].strip()

            diam_text = diam_line.split(":", 1)[1].replace("m", "").strip()
            diameter = float(diam_text)

            center_text = center_line.split(":", 1)[1].replace("m", "").strip()
            center = [float(v.strip()) for v in center_text.split()]

            dir_text = dir_line.split(":", 1)[1].strip()
            direction = [float(v.strip()) for v in dir_text.split()]

            fan_names.append(name)
            fan_types.append(type_text)
            fan_diams.append(diameter)
            fan_centers.append(center)
            fan_velocity_dirs.append(direction)

            i = j - 1

        i += 1

    if rack_w is None or rack_d is None or rack_h is None:
        raise ValueError("Could not parse rack dimensions")

    return [rack_w, rack_d, rack_h], component_names, component_dims, component_coords, fan_names, fan_types, fan_diams, fan_centers, fan_velocity_dirs


# =========================
# READ MESH SPACING
# =========================

with open(args.sim, "r") as f:
    header = f.readline().strip()
    spacing_line = f.readline().strip()

parts = spacing_line.split(",")

dx = float(parts[1])
dy = float(parts[3])
dz = float(parts[5])

print(f"Mesh spacing: dx={dx}, dy={dy}, dz={dz}")


# =========================
# READ SIM CSV
# =========================

df = pd.read_csv(args.sim, skiprows=[1])

print("Temperature range:", df["T"].min(), df["T"].max())
print("Initial max:", df[df["step"] == df["step"].min()]["T"].max())
print("Final max:", df[df["step"] == df["step"].max()]["T"].max())


# =========================
# READ RACK GEOMETRY
# =========================

try:
    rack_dim, component_names, component_dims, component_coords, fan_names, fan_types, fan_diams, fan_centers, fan_velocity_dirs = parse_rack_file(args.rack)

except FileNotFoundError:
    print(f"Warning: {args.rack} not found. Inferring rack size from mesh.")
    rack_dim = [
        (int(df["x"].max()) + 1) * dx,
        (int(df["y"].max()) + 1) * dy,
        (int(df["z"].max()) + 1) * dz,
    ]
    component_names = []
    component_dims = []
    component_coords = []
    fan_names = []
    fan_types = []
    fan_diams = []
    fan_centers = []
    fan_velocity_dirs = []

except Exception as e:
    print(f"Warning: could not parse {args.rack}: {e}")
    print("Inferring rack size from mesh.")
    rack_dim = [
        (int(df["x"].max()) + 1) * dx,
        (int(df["y"].max()) + 1) * dy,
        (int(df["z"].max()) + 1) * dz,
    ]
    component_names = []
    component_dims = []
    component_coords = []
    fan_names = []
    fan_types = []
    fan_diams = []
    fan_centers = []
    fan_velocity_dirs = []


rack_x, rack_y, rack_z = rack_dim

steps = sorted(df["step"].unique())
steps = steps[::max(1, args.skip)]

vmin = df["T"].min()
vmax = df["T"].max()

if abs(vmax - vmin) < 1e-9:
    vmax = vmin + 1.0


# =========================
# FIGURE SETUP
# =========================

fig = plt.figure(figsize=(11, 8))
ax = fig.add_subplot(111, projection="3d")
fig.patch.set_facecolor("#f5f5f5")


def draw_rack_geometry(ax):
    ax.bar3d(
        0, 0, 0,
        rack_x, rack_y, rack_z,
        color="black",
        edgecolor="black",
        alpha=0.08,
        shade=True
    )

    colors = itertools.cycle(plt.rcParams["axes.prop_cycle"].by_key()["color"])

    legend_handles = [
        mpatches.Patch(
            color="black",
            alpha=0.2,
            label="Rack"
        )
    ]

    for name, dim, coord in zip(component_names, component_dims, component_coords):
        color = next(colors)

        # C++ exports dimensions as: height x width x depth
        # matplotlib bar3d wants: dx, dy, dz = width, depth, height
        h, w, d = dim

        ax.bar3d(
            coord[0], coord[1], coord[2],
            w, d, h,
            color=color,
            edgecolor=color,
            alpha=0.25,
            shade=True
        )

        legend_handles.append(
            mpatches.Patch(
                color=color,
                alpha=0.85,
                label=f"Component: name"
            )
        )

    for i in range(len(fan_centers)):
        color = next(colors)

        x, y, z = fan_centers[i]
        vx, vy, vz = fan_velocity_dirs[i]
        d = fan_diams[i]
        r = d / 2.0

        n = fan_names[i]
        t = fan_types[i]
        legend_name = f"Fan: {n}, Type: {t}"

        # -------------------------
        # draw fan disk
        # -------------------------

        # mostly z-normal: fan lies in XY plane
        if abs(vz) >= abs(vx) and abs(vz) >= abs(vy):
            circle = plt.Circle((x, y), r, color=color, alpha=0.35)
            ax.add_patch(circle)
            art3d.pathpatch_2d_to_3d(circle, z=z, zdir="z")

        # mostly y-normal: fan lies in XZ plane
        elif abs(vy) >= abs(vx) and abs(vy) >= abs(vz):
            circle = plt.Circle((x, z), r, color=color, alpha=0.35)
            ax.add_patch(circle)
            art3d.pathpatch_2d_to_3d(circle, z=y, zdir="y")

        # mostly x-normal: fan lies in YZ plane
        else:
            circle = plt.Circle((y, z), r, color=color, alpha=0.35)
            ax.add_patch(circle)
            art3d.pathpatch_2d_to_3d(circle, z=x, zdir="x")

        # -------------------------
        # draw velocity arrow
        # -------------------------

        arrow_len = max(r * 1.5, 0.02)

        ax.quiver(
            x, y, z,
            vx * arrow_len,
            vy * arrow_len,
            vz * arrow_len,
            color=color,
            linewidth=2,
            arrow_length_ratio=0.25
        )

        legend_handles.append(
            mpatches.Patch(
                color=color,
                alpha=0.65,
                label=legend_name
            )
        )

    return legend_handles


def update(step):
    ax.clear()

    legend_handles = draw_rack_geometry(ax)

    frame = df[df["step"] == step]

    x = frame["x"].to_numpy() * dx + dx / 2.0
    y = frame["y"].to_numpy() * dy + dy / 2.0
    z = frame["z"].to_numpy() * dz + dz / 2.0

    T = frame["T"].to_numpy()
    is_component = frame["is_component"].to_numpy()

    sizes = np.where(is_component == 1, 100, 60)

    scatter = ax.scatter(
        x, y, z,
        c=T,
        s=sizes,
        cmap="inferno",
        vmin=vmin,
        vmax=vmax,
        marker="s",
        alpha=0.83,
        edgecolors="none"
    )

    time = frame["time"].iloc[0]

    ax.set_xlim(0, rack_x)
    ax.set_ylim(0, rack_y)
    ax.set_zlim(0, rack_z)
    ax.set_box_aspect((rack_x, rack_y, rack_z))

    ax.set_xlabel("Width (m)", labelpad=8)
    ax.set_ylabel("Depth (m)", labelpad=8)
    ax.set_zlabel("Height (m)", labelpad=10)

    ax.set_title(
        f"Rack Thermal Simulation\nstep = {step}, time = {time:.3f}",
        pad=18
    )

    ax.view_init(elev=24, azim=35 + 0.15 * step)
    ax.grid(True)

    ax.legend(
        handles=legend_handles,
        loc="upper left",
        bbox_to_anchor=(1.02, 1.0)
    )

    return scatter,


# =========================
# COLORBAR
# =========================

first_frame = df[df["step"] == steps[0]]

dummy = ax.scatter(
    first_frame["x"].to_numpy() * dx + dx / 2.0,
    first_frame["y"].to_numpy() * dy + dy / 2.0,
    first_frame["z"].to_numpy() * dz + dz / 2.0,
    c=first_frame["T"].to_numpy(),
    cmap="inferno",
    vmin=vmin,
    vmax=vmax
)

cbar = fig.colorbar(dummy, ax=ax, shrink=0.65, pad=0.12)
cbar.set_label("Temperature (°C)")


# =========================
# ANIMATION
# =========================

animation = FuncAnimation(
    fig,
    update,
    frames=steps,
    interval=1000 / args.fps,
    blit=False
)

if args.save:
    animation.save("rack_temperature_animation.mp4", fps=args.fps, dpi=180)
    print("Saved: rack_temperature_animation.mp4")
else:
    plt.show()