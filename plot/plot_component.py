import argparse
import itertools
from pathlib import Path

import matplotlib.patches as mpatches
import matplotlib.pyplot as plt


parser = argparse.ArgumentParser(description="Plot one exported component and its internal regions.")
parser.add_argument("-s", "--save", action="store_true", help="Save the plot as component_plot.png.")
args = parser.parse_args()

filename = Path("output.txt")


# =========================
# READ COMPONENT OUTPUT
# =========================
try:
    with filename.open("r", encoding="utf-8") as file:
        lines = [line.strip() for line in file if line.strip()]
except FileNotFoundError:
    raise SystemExit(f"Error: File '{filename}' not found.")


# =========================
# PARSE COMPONENT DATA
# =========================
component_name = None
component_dims = None
component_coords = None
internal_regions = []

index = 0
while index < len(lines):
    line = lines[index]

    if line.startswith("Component "):
        component_name = line.split(":", 1)[1].strip()

    elif line.startswith("dimensions:"):
        text = line.split(":", 1)[1].replace("m", "").strip()
        component_dims = [float(value.strip()) for value in text.split("x")]

    elif line.startswith("coordinates:"):
        text = line.split(":", 1)[1].replace("m", "").strip()
        component_coords = [float(value) for value in text.split()]

    elif line.startswith("Internal Region "):
        region_number = line.removeprefix("Internal Region ").rstrip(":")
        region = {
            "name": f"Internal Region {region_number}",
            "type": None,
            "size": None,
            "local_position": None,
        }

        index += 1
        while index < len(lines) and not lines[index].startswith("Internal Region "):
            region_line = lines[index]

            if region_line.startswith("type:"):
                region["type"] = region_line.split(":", 1)[1].strip()

            elif region_line.startswith("size:"):
                text = region_line.split(":", 1)[1].replace("m", "").strip()
                region["size"] = [float(value) for value in text.split()]

            elif region_line.startswith("local_position:"):
                text = region_line.split(":", 1)[1].replace("m", "").strip()
                region["local_position"] = [float(value) for value in text.split()]

            index += 1

        missing = [key for key in ("type", "size", "local_position") if region[key] is None]
        if missing:
            missing_text = ", ".join(missing)
            raise ValueError(f"{region['name']} is missing: {missing_text}")

        internal_regions.append(region)
        continue

    index += 1


if component_name is None:
    raise ValueError("Could not find the component name in output.txt.")
if component_dims is None or len(component_dims) != 3:
    raise ValueError("Could not find valid component dimensions in output.txt.")
if component_coords is None or len(component_coords) != 3:
    raise ValueError("Could not find valid component coordinates in output.txt.")

width, depth, height = component_dims
if width <= 0.0 or depth <= 0.0 or height <= 0.0:
    raise ValueError("Component dimensions must all be greater than zero.")


# =========================
# PLOT COMPONENT
# =========================
fig = plt.figure(figsize=(10, 7))
ax = fig.add_subplot(projection="3d")

ax.bar3d(
    0.0,
    0.0,
    0.0,
    width,
    depth,
    height,
    shade=True,
    edgecolor="black",
    alpha=0.15,
)

legend_handles = [
    mpatches.Patch(alpha=0.3, label=f"Component: {component_name}")
]

colors = itertools.cycle(plt.rcParams["axes.prop_cycle"].by_key()["color"])

for region in internal_regions:
    x, y, z = region["local_position"]
    region_width, region_depth, region_height = region["size"]
    color = next(colors)

    ax.bar3d(
        x,
        y,
        z,
        region_width,
        region_depth,
        region_height,
        shade=True,
        edgecolor=color,
        linewidth=1.2,
        alpha=0.65,
        color=color,
    )

    legend_handles.append(
        mpatches.Patch(
            color=color,
            alpha=0.65,
            label=f"{region['name']}: {region['type']}",
        )
    )

ax.set_xlim(0.0, width)
ax.set_ylim(0.0, depth)
ax.set_zlim(0.0, height)
ax.set_box_aspect((width, depth, height))

ax.set_xlabel("Width (m)")
ax.set_ylabel("Depth (m)")
ax.set_zlabel("Height (m)")
ax.set_title(f"Component Model: {component_name}")

ax.legend(
    handles=legend_handles,
    loc="upper left",
    bbox_to_anchor=(1.02, 1.0),
    borderaxespad=0.0,
)

fig.tight_layout()

if args.save:
    output_path = Path("component_plot.png")
    fig.savefig(output_path, dpi=200, bbox_inches="tight")
    print(f"Saved plot to '{output_path}'.")
else:
    plt.show()