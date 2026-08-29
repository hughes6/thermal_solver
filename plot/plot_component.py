import argparse
import itertools
from pathlib import Path
from mpl_toolkits.mplot3d import art3d
import matplotlib.patches as mpatches
import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator


def component_region_color(region_kind, default_color):
    """Use one stable color for every component air volume."""
    return "tab:cyan" if region_kind.casefold() == "air" else default_color


def select_component_block(lines, component_name=None, component_index=None):
    """Return one exported component block selected by name or 1-based index."""
    starts = [
        index for index, line in enumerate(lines)
        if line.startswith("Component ")
    ]
    if component_index is not None:
        if component_index < 1 or component_index > len(starts):
            raise ValueError(
                f"--component-index {component_index} is outside the exported "
                f"component range 1..{len(starts)}"
            )
        matches = [starts[component_index - 1]]
    elif component_name:
        matches = [
            index for index in starts
            if component_name.casefold() in
            lines[index].split(":", 1)[1].strip().casefold()
        ]
        if len(matches) != 1:
            raise ValueError(
                f"--component {component_name!r} matched {len(matches)} "
                "components; use a unique name substring or --component-index."
            )
    else:
        return lines
    start = matches[0]
    end = next((index for index in starts if index > start), len(lines))
    return lines[start:end]


parser = argparse.ArgumentParser(description="Plot one exported component and its internal regions.")
parser.add_argument("-s", "--save", action="store_true", help="Save the plot as component_plot.png.")
parser.add_argument(
    "--input",
    type=Path,
    default=Path("output.txt"),
    help="exported geometry input (default: output.txt)",
)
selector = parser.add_mutually_exclusive_group()
selector.add_argument("--component", help="Component name (or unique substring) from a rack export.")
selector.add_argument("--component-index", type=int,
                      help="1-based component number from a rack export; useful for duplicate names.")
parser.add_argument("--output", type=Path, default=Path("component_plot.png"),
                    help="PNG path used with --save (default: component_plot.png).")
args = parser.parse_args()

filename = args.input


# =========================
# READ COMPONENT OUTPUT
# =========================
try:
    with filename.open("r", encoding="utf-8") as file:
        lines = [line.strip() for line in file if line.strip()]
except FileNotFoundError:
    raise SystemExit(f"Error: File '{filename}' not found.")

try:
    lines = select_component_block(lines, args.component, args.component_index)
except ValueError as error:
    raise SystemExit(f"Error: {error}") from error


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
        # Rack exports can contain many components. This plotter displays one
        # component, so stop after the first complete component block instead
        # of silently combining every component's local coordinate system.
        if component_name is not None:
            break
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
            "diameter" : None,
            "direction" : None
        }

        index += 1
        while index < len(lines) and not lines[index].startswith(
            ("Internal Region ", "Component ", "Fan ", "Vent ")
        ):
            region_line = lines[index]

            if region_line.startswith("type:"):
                region["type"] = region_line.split(":", 1)[1].strip()

            elif region_line.startswith("size:"):
                text = region_line.split(":", 1)[1].replace("m", "").strip()
                region["size"] = [float(value) for value in text.split()]

            elif region_line.startswith("local_position:"):
                text = region_line.split(":", 1)[1].replace("m", "").strip()
                region["local_position"] = [float(value) for value in text.split()]

            elif region_line.startswith("diameter"):
                text = region_line.split(":", 1)[1].replace("m", "").strip()
                region["diameter"] = [float(value) for value in text.split()]

            elif region_line.startswith("direction:"):
                text = region_line.split(":", 1)[1].replace("m", "").strip()
                region["direction"] = [float(value) for value in text.split()]

            index += 1

        missing = [key for key in ("type", "size", "local_position") if region[key] is None]
        if missing:
            missing_text = ", ".join(missing)
            raise ValueError(f"{region['name']} is missing: {missing_text}")

        internal_regions.append(region)
        continue

    index += 1


if component_name is None:
    raise ValueError(f"Could not find the component name in {filename}.")
if component_dims is None or len(component_dims) != 3:
    raise ValueError(f"Could not find valid component dimensions in {filename}.")
if component_coords is None or len(component_coords) != 3:
    raise ValueError(f"Could not find valid component coordinates in {filename}.")

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
    alpha=0.05,
)

legend_handles = [
    mpatches.Patch(alpha=0.05, label=f"Component: {component_name}")
]

colors = itertools.cycle(plt.rcParams["axes.prop_cycle"].by_key()["color"])

i = 1
for region in internal_regions:
    x, y, z = region["local_position"]
    region_width, region_depth, region_height = region["size"]
    r = region["diameter"][0] / 2
    vx, vy, vz = region["direction"]
    
    color = next(colors)
    a = 0.05 * 1
    if a > 0.9:
        a = 0.9

    # Air and solid-region positions are lower corners. Fan and vent
    # positions are centers, so shift rectangular footprints by half of
    # each non-normal dimension before passing them to bar3d().
    region_kind = region["type"].rsplit("/", 1)[-1]
    color = component_region_color(region_kind, color)
    is_centered_surface = region_kind in ("Fan", "Vent")

    plot_x = x - region_width / 2.0 if is_centered_surface else x
    plot_y = y - region_depth / 2.0 if is_centered_surface else y
    plot_z = z - region_height / 2.0 if is_centered_surface else z

    if region_width > 0.0 or region_depth > 0.0 or region_height > 0.0:
        ax.bar3d(
            plot_x,
            plot_y,
            plot_z,
            region_width,
            region_depth,
            region_height,
            shade=True,
            edgecolor=color,
            linewidth=1.2,
            alpha=a,
            color=color,
        )

    circle_rgb = plt.matplotlib.colors.to_rgb(color)
    face_rgba = (*circle_rgb, 0.45)
    edge_rgba = (*circle_rgb, 1.0)

    # Small offset prevents the component surface from covering the circle.
    offset = 1e-5

    # Mostly z-normal: circle lies in XY plane.
    if abs(vz) >= abs(vx) and abs(vz) >= abs(vy) and r > 0:
        circle = plt.Circle(
            (x, y),
            r,
            facecolor=face_rgba,
            edgecolor=edge_rgba,
            linewidth=3.0,
        )
        ax.add_patch(circle)
        art3d.pathpatch_2d_to_3d(
            circle,
            z=z + offset * (1 if vz >= 0 else -1),
            zdir="z",
        )

    # Mostly y-normal: circle lies in XZ plane.
    elif abs(vy) >= abs(vx) and abs(vy) >= abs(vz) and r > 0:
        circle = plt.Circle(
            (x, z),
            r,
            facecolor=face_rgba,
            edgecolor=edge_rgba,
            linewidth=3.0,
        )
        ax.add_patch(circle)
        art3d.pathpatch_2d_to_3d(
            circle,
            z=y + offset * (1 if vy >= 0 else -1),
            zdir="y",
        )

    # Mostly x-normal: circle lies in YZ plane.
    elif r > 0:
        circle = plt.Circle(
            (y, z),
            r,
            facecolor=face_rgba,
            edgecolor=edge_rgba,
            linewidth=3.0,
        )
        ax.add_patch(circle)
        art3d.pathpatch_2d_to_3d(
            circle,
            z=x + offset * (1 if vx >= 0 else -1),
            zdir="x",
        )


    legend_handles.append(
        mpatches.Patch(
            color=color,
            alpha=0.65,
            label=f"{region['name']}: {region['type']}",
        )
    )
    i += 1

ax.set_xlim(0.0, width)
ax.set_ylim(0.0, depth)
ax.set_zlim(0.0, height)
ax.set_box_aspect((width, depth, height))

# Thin rack components can otherwise receive a tick at every centimetre,
# producing an unreadable stack of labels beside the external legend.
for axis in (ax.xaxis, ax.yaxis):
    axis.set_major_locator(MaxNLocator(nbins=6, min_n_ticks=3))
ax.zaxis.set_major_locator(MaxNLocator(nbins=3, min_n_ticks=3))

ax.set_xlabel("Width (m)")
ax.set_ylabel("Depth (m)")
ax.set_zlabel("Height (m)")
ax.set_title(f"Component Model: {component_name}")

ax.legend(
    handles=legend_handles,
    loc="upper left",
    bbox_to_anchor=(1.12, 1.0),
    borderaxespad=0.0,
)

fig.tight_layout()

if args.save:
    output_path = args.output
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path, dpi=200, bbox_inches="tight")
    print(f"Saved plot to '{output_path}'.")
else:
    plt.show()
