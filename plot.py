import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import itertools
import pandas as pd
import numpy as np
import re
from mpl_toolkits.mplot3d import art3d
import argparse

anim = False

parser = argparse.ArgumentParser()
parser.add_argument("-s", "--save", action="store_true")
args = parser.parse_args()

filename = "output.txt"

rack_dim = []
component_names = []
component_dims = []
component_coords = []
fan_names = []
fan_types = []
fan_diams = []
fan_centers = []
fan_velocity_dirs = []

try:
    with open(filename, "r") as f:
        lines = [line.strip() for line in f if line.strip()]
except FileNotFoundError:
    print(f"Error: File '{filename}' not found.")
    raise SystemExit

# =========================
# PARSE NEW output.txt FORMAT
# =========================

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

            if lines[j].startswith("coordinates:"):
                coord_line = lines[j]

            j += 1

        if dim_line is None or coord_line is None:
            raise ValueError(f"Could not find dimensions/coordinates for {name}")

        dims_text = dim_line.split(":", 1)[1]
        dims_text = dims_text.replace("m", "").strip()
        dims = [float(v.strip()) for v in dims_text.split("x")]

        coords_text = coord_line.split(":", 1)[1]
        coords_text = coords_text.replace("m", "").strip()
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

rack_dim = [rack_w, rack_d, rack_h]


# =========================
# PLOT RACK + COMPONENTS
# =========================

fig = plt.figure(figsize=(10, 6))
ax = fig.add_subplot(projection="3d")

rack_x, rack_y, rack_z = rack_dim

ax.set_box_aspect((rack_x, rack_y, rack_z))

ax.bar3d(
    0, 0, 0,
    rack_x, rack_y, rack_z,
    color="black",
    shade=True,
    edgecolor="black",
    alpha=0.2
)

legend_handles = [
    mpatches.Patch(
        color="black",
        alpha=0.4,
        label="Rack"
    )
]

colors = itertools.cycle(plt.rcParams["axes.prop_cycle"].by_key()["color"])

for i in range(len(component_coords)):
    color = next(colors)

    x, y, z = component_coords[i]

    # export order was height, width, depth
    h, w, d = component_dims[i]

    ax.bar3d(
        x, y, z,
        w, d, h,
        color=color,
        shade=True,
        edgecolor=color,
        alpha=0.5
    )

    legend_handles.append(
        mpatches.Patch(
            color=color,
            alpha=0.5,
            label=f"Component: {component_names[i]}"
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

ax.set_xlim(0, rack_x)
ax.set_ylim(0, rack_y)
ax.set_zlim(0, rack_z)

ax.set_xlabel("Width (m)")
ax.set_ylabel("Depth (m)")
ax.set_zlabel("Height (m)")
ax.set_title("Sim Rack Model Graph")

ax.legend(
    handles=legend_handles,
    loc="upper left",
    bbox_to_anchor=(0.9, 1.0),   # just outside the right edge
    borderaxespad=0.0
)
plt.subplots_adjust(right=0.95)   # leave space for legend
plt.show()