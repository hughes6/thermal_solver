import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import itertools
import pandas as pd
import numpy as np
import re
from mpl_toolkits.mplot3d import art3d
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import argparse

anim = False

parser = argparse.ArgumentParser()
parser.add_argument("-s", "--save", action="store_true")
args = parser.parse_args()

filename = "output.txt"

component_names = []
component_dims = []
component_coords = []
internal_region_parent = []
internal_region_types = []
internal_region_sizes = []
internal_region_global_positions = []

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
        regions_for_component = []

        j = i + 1
        while j < len(lines):
            if lines[j].startswith(("Component ", "Fan ", "Vent ")):
                break
            if lines[j].startswith("dimensions:"):
                dim_line = lines[j]
            elif lines[j].startswith("coordinates:"):
                coord_line = lines[j]
            elif lines[j].startswith("Internal Region "):
                region_type = None
                region_size = None
                region_global = None
                k = j + 1
                while k < len(lines):
                    if lines[k].startswith("Internal Region ") or lines[k].startswith(("Component ", "Fan ", "Vent ")):
                        break
                    if lines[k].startswith("type:"):
                        region_type = lines[k].split(":", 1)[1].strip()
                    elif lines[k].startswith("size:"):
                        text = lines[k].split(":", 1)[1].replace("m", "").strip()
                        region_size = [float(v) for v in text.split()]
                    elif lines[k].startswith("global_position:"):
                        text = lines[k].split(":", 1)[1].replace("m", "").strip()
                        region_global = [float(v) for v in text.split()]
                    k += 1
                if region_type is not None and region_size is not None and region_global is not None:
                    regions_for_component.append((region_type, region_size, region_global))
                j = k
                continue
            j += 1

        if dim_line is None or coord_line is None:
            raise ValueError(f"Could not find dimensions/coordinates for {name}")

        dims_text = dim_line.split(":", 1)[1].replace("m", "").strip()
        dims = [float(v.strip()) for v in dims_text.split("x")]
        coords_text = coord_line.split(":", 1)[1].replace("m", "").strip()
        coords = [float(v.strip()) for v in coords_text.split()]

        component_names.append(name)
        component_dims.append(dims)
        component_coords.append(coords)
        for region_type, region_size, region_global in regions_for_component:
            internal_region_parent.append(name)
            internal_region_types.append(region_type)
            internal_region_sizes.append(region_size)
            internal_region_global_positions.append(region_global)

        i = j - 1

    elif line.startswith("Fan "):
        name = line.split(":", 1)[1].strip()

        type_line = None
        cfm_line = None
        diam_line = None
        center_line = None
        dir_line = None
        f_size_line = None
        f_shape_line = None

        j = i + 1
        while j < len(lines):
            if re.match(r"^Fan\s+\d+:", lines[j]) or lines[j].startswith(("Component ", "Vent")):
                break

            if lines[j].startswith("type:"):
                type_line = lines[j]
            elif lines[j].startswith("cfm:"):
                cfm_line = lines[j]
            elif lines[j].startswith("diameter:"):
                diam_line = lines[j]
            elif lines[j].startswith("f_center:"):
                center_line = lines[j]
            elif lines[j].startswith("f_direction:"):
                dir_line = lines[j]
            elif lines[j].startswith("shape"):
                f_shape_line = lines[j]
            elif lines[j].startswith("f_size"):
                f_size_line = lines[j]

            j += 1

        if type_line is None or diam_line is None or center_line is None or dir_line is None or f_shape_line is None or f_size_line is None:
            raise ValueError(f"Could not find fan data for {name}")

        type_text = type_line.split(":", 1)[1].strip()
        shape_text = f_shape_line.split(":", 1)[1].strip()

        diam_text = diam_line.split(":", 1)[1].replace("m", "").strip()
        diameter = float(diam_text)

        center_text = center_line.split(":", 1)[1].replace("m", "").strip()
        center = [float(v.strip()) for v in center_text.split()]

        f_size_text = f_size_line.split(":", 1)[1].replace("m", "").strip()
        size = [float(v.strip()) for v in f_size_text.split()]

        dir_text = dir_line.split(":", 1)[1].strip()
        direction = [float(v.strip()) for v in dir_text.split()]

        fan_names.append(name)
        fan_types.append(type_text)
        fan_diams.append(diameter)
        fan_centers.append(center)
        fan_velocity_dirs.append(direction)
        fan_sizes.append(size)
        fan_shapes.append(shape_text)

        i = j - 1

    elif line.startswith("Vent "):
        v_name = line.split(":", 1)[1].strip()

        far_line = None
        v_size_line = None
        v_center_line = None
        v_dir_line = None
        v_shape_line = None
        v_diam_line = None

        j = i + 1
        while j < len(lines):
            if re.match(r"^Vent\s+\d+:", lines[j]) or lines[j].startswith(("Component ", "Fan ")):
                break

            if lines[j].startswith(("Free area ratio:", "free_area_ratio:")):
                far_line = lines[j]
            elif lines[j].startswith(("v_size:", "size:")):
                v_size_line = lines[j]
            elif lines[j].startswith("v_center:"):
                v_center_line = lines[j]
            elif lines[j].startswith("v_direction:"):
                v_dir_line = lines[j]
            elif lines[j].startswith("shape:"):
                v_shape_line = lines[j]
            elif lines[j].startswith("diameter:"):
                v_diam_line = lines[j]

            j += 1

        if (far_line is None or v_size_line is None or v_center_line is None
                or v_dir_line is None or v_shape_line is None or v_diam_line is None):
            raise ValueError(f"Could not find vent data for {v_name}")

        far_text = far_line.split(":", 1)[1].strip()
        shape_text = v_shape_line.split(":", 1)[1].strip()

        diam_text = v_diam_line.split(":", 1)[1].replace("m", "").strip()
        diameter = float(diam_text)

        v_size_text = v_size_line.split(":", 1)[1].replace("m", "").strip()
        v_size = [float(v.strip()) for v in v_size_text.split()]

        v_center_text = v_center_line.split(":", 1)[1].replace("m", "").strip()
        v_center = [float(v.strip()) for v in v_center_text.split()]

        v_dir_text = v_dir_line.split(":", 1)[1].strip()
        v_direction = [float(v.strip()) for v in v_dir_text.split()]

        vent_names.append(v_name)
        vent_fars.append(far_text)
        vent_sizes.append(v_size)
        vent_centers.append(v_center)
        vent_directions.append(v_direction)
        vent_shapes.append(shape_text)
        vent_diams.append(diameter)

        i = j - 1

    i += 1

rack_dim = [rack_w, rack_d, rack_h]


# =========================
# PLOT COMPONENTS
# =========================

fig = plt.figure(figsize=(10, 6))
ax = fig.add_subplot(projection="3d")


colors = itertools.cycle(plt.rcParams["axes.prop_cycle"].by_key()["color"])
color = next(colors)
x, y, z = component_coords
w, d, h = component_dims

ax.set_box_aspect((x, y, z))

ax.bar3d(
    0, 0, 0,
    x, y, z,
    color="black",
    shade=True,
    edgecolor="black",
    alpha=0.2
)

legend_handles = [
    mpatches.Patch(
        color="black",
        alpha=0.4,
        label="Compponent"
    )
]

start_alpha = 0.2
# Internal regions are added to the existing rack axes; no new plot or layout is created.
for i in range(len(internal_region_global_positions)):
    color = next(colors)
    x, y, z = internal_region_global_positions[i]
    sx, sy, sz = internal_region_sizes[i]
    region_type = internal_region_types[i]
    parent = internal_region_parent[i]
    alpha = start_alpha + (i * 0.1)
    if alpha >= 1:
        alpha = 0.95

    ax.bar3d(
        x, y, z,
        sx, sy, sz,
        color=color,
        shade=True,
        edgecolor=color,
        linewidth=1.5,
        alpha=alpha
    )
    legend_handles.append(
        mpatches.Patch(
            color=color,
            alpha=alpha,
            label=f"Internal region ({parent}): {region_type}"
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
