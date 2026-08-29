"""Render fixed-scale validation plots from OpenFOAM raw cutting-plane samples.

The script intentionally does not open or modify the OpenFOAM case.  Native
``postProcess`` sampling is performed separately with ``sampling_controlDict``;
this renderer consumes only the resulting compact ``*.raw`` text files.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import platform
import sys
import textwrap
from datetime import datetime, timezone
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.colors as mcolors
import matplotlib.patches as mpatches
import matplotlib.pyplot as plt
import matplotlib.tri as mtri
import numpy as np


CHECKPOINT_TIME = "1.6000000000000001"
RACK_BOUNDS_YZ = (0.0, 1.09347, 0.0, 1.778)
DELL_BOUNDS_YZ = (0.0, 0.817, 0.1778, 0.2208)
NI_BOUNDS_YZ = (0.8001, 1.0143, 1.55575, 1.73295)
DELL_CROP_YZ = (-0.015, 0.845, 0.153, 0.246)
NI_CROP_YZ = (0.772, 1.043, 1.527, 1.757)

# These are held identical across all corresponding global and local images.
SPEED_CLIM_MPS = (0.0, 14.5)
TEMPERATURE_CLIM_C = (19.9, 21.4)
PRESSURE_REFERENCE_PA = 82655.5
PRESSURE_CLIM_PA = (-170.0, 170.0)
MAX_TRIANGLE_EDGE_M = 0.047


def parse_args() -> argparse.Namespace:
    default_output = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=default_output)
    parser.add_argument(
        "--sampled-dir",
        type=Path,
        default=default_output / "sampled_data" / CHECKPOINT_TIME,
    )
    parser.add_argument(
        "--sampled-vtk-dir",
        type=Path,
        default=default_output / "sampled_vtk" / CHECKPOINT_TIME,
    )
    parser.add_argument(
        "--case",
        type=Path,
        default=Path(
            r"C:\Users\hconn\.codex\visualizations\2026\08\04"
            r"\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_cases"
            r"\new_model_updated_openfoam_export_test"
        ),
    )
    return parser.parse_args()


def load_raw(path: Path, components: int) -> tuple[np.ndarray, np.ndarray]:
    data = np.loadtxt(path, comments="#", dtype=float)
    if data.ndim != 2 or data.shape[1] != 3 + components:
        raise ValueError(
            f"{path} has shape {data.shape}, expected (*, {3 + components})"
        )
    if not np.isfinite(data).all():
        raise ValueError(f"{path} contains non-finite values")
    return data[:, :3], data[:, 3:]


def load_legacy_vtk_polydata(
    path: Path,
) -> tuple[np.ndarray, np.ndarray, dict[str, np.ndarray]]:
    """Read the small ASCII legacy VTK subset emitted by OpenFOAM surfaces."""
    tokens = path.read_text(encoding="utf-8").split()
    try:
        points_at = tokens.index("POINTS")
        polygons_at = tokens.index("POLYGONS")
        point_data_at = tokens.index("POINT_DATA")
    except ValueError as exc:
        raise ValueError(f"{path} is not an expected legacy VTK POLYDATA file") from exc

    point_count = int(tokens[points_at + 1])
    point_start = points_at + 3
    point_values = np.asarray(
        tokens[point_start:point_start + 3 * point_count], dtype=float
    )
    points = point_values.reshape(point_count, 3)

    polygon_count = int(tokens[polygons_at + 1])
    cursor = polygons_at + 3
    triangles: list[tuple[int, int, int]] = []
    for _ in range(polygon_count):
        vertex_count = int(tokens[cursor])
        cursor += 1
        vertices = [int(value) for value in tokens[cursor:cursor + vertex_count]]
        cursor += vertex_count
        for index in range(1, vertex_count - 1):
            triangles.append((vertices[0], vertices[index], vertices[index + 1]))

    tuple_count = int(tokens[point_data_at + 1])
    cursor = point_data_at + 2
    if tokens[cursor] != "FIELD":
        raise ValueError(f"{path}: expected FIELD arrays after POINT_DATA")
    array_count = int(tokens[cursor + 2])
    cursor += 3
    arrays: dict[str, np.ndarray] = {}
    for _ in range(array_count):
        name = tokens[cursor]
        component_count = int(tokens[cursor + 1])
        array_tuples = int(tokens[cursor + 2])
        cursor += 4  # Skip the VTK scalar type as well.
        if array_tuples != tuple_count:
            raise ValueError(f"{path}: {name} tuple count does not match POINT_DATA")
        count = component_count * array_tuples
        values = np.asarray(tokens[cursor:cursor + count], dtype=float)
        cursor += count
        arrays[name] = values.reshape(array_tuples, component_count)
    return points, np.asarray(triangles, dtype=np.int64), arrays


def deduplicate(
    coords: np.ndarray, values: np.ndarray
) -> tuple[np.ndarray, np.ndarray]:
    """Average values at coincident sampled points before triangulation."""
    rounded = np.round(coords, decimals=10)
    unique, inverse = np.unique(rounded, axis=0, return_inverse=True)
    values_2d = values[:, None] if values.ndim == 1 else values
    sums = np.zeros((len(unique), values_2d.shape[1]), dtype=float)
    counts = np.zeros(len(unique), dtype=float)
    np.add.at(sums, inverse, values_2d)
    np.add.at(counts, inverse, 1.0)
    averaged = sums / counts[:, None]
    if values.ndim == 1:
        averaged = averaged[:, 0]
    return unique, averaged


def crop_mask(coords_yz: np.ndarray, bounds: tuple[float, ...]) -> np.ndarray:
    y0, y1, z0, z1 = bounds
    return (
        (coords_yz[:, 0] >= y0)
        & (coords_yz[:, 0] <= y1)
        & (coords_yz[:, 1] >= z0)
        & (coords_yz[:, 1] <= z1)
    )


def triangulation(coords_yz: np.ndarray) -> mtri.Triangulation:
    tri = mtri.Triangulation(coords_yz[:, 0], coords_yz[:, 1])
    vertices = coords_yz[tri.triangles]
    edges = np.stack(
        (
            np.linalg.norm(vertices[:, 0] - vertices[:, 1], axis=1),
            np.linalg.norm(vertices[:, 1] - vertices[:, 2], axis=1),
            np.linalg.norm(vertices[:, 2] - vertices[:, 0], axis=1),
        ),
        axis=1,
    )
    tri.set_mask(np.max(edges, axis=1) > MAX_TRIANGLE_EDGE_M)
    return tri


def binned_directions(
    coords_yz: np.ndarray,
    velocity: np.ndarray,
    bounds: tuple[float, ...],
    bins: tuple[int, int],
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Return deterministic equal-length in-plane direction arrows."""
    y0, y1, z0, z1 = bounds
    y_edges = np.linspace(y0, y1, bins[0] + 1)
    z_edges = np.linspace(z0, z1, bins[1] + 1)
    yi = np.clip(np.digitize(coords_yz[:, 0], y_edges) - 1, 0, bins[0] - 1)
    zi = np.clip(np.digitize(coords_yz[:, 1], z_edges) - 1, 0, bins[1] - 1)
    key = yi * bins[1] + zi
    rows = []
    for value in np.unique(key):
        selected = key == value
        if not selected.any():
            continue
        selected_points = coords_yz[selected]
        centroid = np.mean(selected_points, axis=0)
        # Anchor every arrow on an actual sampled-fluid point instead of the
        # middle of a bin, which can lie inside a solid gap.
        point = selected_points[
            int(np.argmin(np.linalg.norm(selected_points - centroid, axis=1)))
        ]
        vector = np.mean(velocity[selected][:, [1, 2]], axis=0)
        magnitude = float(np.linalg.norm(vector))
        if magnitude < 0.03:
            continue
        rows.append((point[0], point[1], vector[0] / magnitude, vector[1] / magnitude))
    if not rows:
        return (np.array([]),) * 4
    result = np.asarray(rows, dtype=float)
    return result[:, 0], result[:, 1], result[:, 2], result[:, 3]


def add_reference_geometry(ax: plt.Axes, local: str | None) -> None:
    rack_y0, rack_y1, rack_z0, rack_z1 = RACK_BOUNDS_YZ
    ax.add_patch(
        mpatches.Rectangle(
            (rack_y0, rack_z0), rack_y1 - rack_y0, rack_z1 - rack_z0,
            fill=False, edgecolor="#182230", linewidth=1.5, zorder=9,
        )
    )
    components = (
        ("Dell R470", DELL_BOUNDS_YZ, "#00e5ff"),
        ("NI PXIe", NI_BOUNDS_YZ, "#ffea00"),
    )
    for name, bounds, color in components:
        y0, y1, z0, z1 = bounds
        ax.add_patch(
            mpatches.Rectangle(
                (y0, z0), y1 - y0, z1 - z0,
                fill=False, edgecolor=color, linewidth=1.6,
                linestyle="--", zorder=10,
            )
        )
        if local is None or local.lower() in name.lower():
            ax.text(
                y0 + 0.008, z1 + (0.004 if local else 0.008), name,
                color="#111827", fontsize=8 if local is None else 10,
                weight="bold", zorder=11,
                bbox={"facecolor": "white", "alpha": 0.72, "edgecolor": "none", "pad": 1.5},
            )


def render_scalar(
    *,
    output: Path,
    coords_yz: np.ndarray,
    values: np.ndarray,
    bounds: tuple[float, ...],
    title: str,
    subtitle: str,
    colorbar_label: str,
    clim: tuple[float, float],
    cmap: str,
    figure_size: tuple[float, float],
    local: str | None = None,
    velocity: np.ndarray | None = None,
    vector_bins: tuple[int, int] = (18, 28),
    arrow_length: float = 0.035,
    triangles: np.ndarray | None = None,
) -> None:
    selected = crop_mask(coords_yz, bounds)
    if np.count_nonzero(selected) < 3:
        raise ValueError(f"{output.name}: crop contains fewer than three points")
    if triangles is None:
        coords, scalar = deduplicate(coords_yz[selected], values[selected])
        tri = triangulation(coords)
    else:
        coords = coords_yz
        scalar = values
        tri = mtri.Triangulation(coords[:, 0], coords[:, 1], triangles=triangles)
        vertices = coords[triangles]
        y0, y1, z0, z1 = bounds
        outside = (
            (vertices[:, :, 0] < y0) | (vertices[:, :, 0] > y1)
            | (vertices[:, :, 1] < z0) | (vertices[:, :, 1] > z1)
        )
        tri.set_mask(np.any(outside, axis=1))

    fig, ax = plt.subplots(figsize=figure_size, constrained_layout=True)
    ax.set_facecolor("#dce5ec")
    norm = mcolors.Normalize(vmin=clim[0], vmax=clim[1], clip=True)
    image = ax.tripcolor(
        tri, scalar, shading="gouraud", cmap=cmap, norm=norm,
        rasterized=True, zorder=1,
    )
    colorbar = fig.colorbar(image, ax=ax, pad=0.02, shrink=0.86)
    colorbar.set_label(colorbar_label, fontsize=10)

    if velocity is not None:
        vector_coords = coords_yz[selected]
        vector_values = velocity[selected]
        y, z, dy, dz = binned_directions(
            vector_coords, vector_values, bounds, vector_bins
        )
        ax.quiver(
            y, z, dy * arrow_length, dz * arrow_length,
            angles="xy", scale_units="xy", scale=1,
            color="white", edgecolor="#111827", linewidth=0.35,
            width=0.004 if local else 0.003, headwidth=3.7,
            headlength=4.8, headaxislength=4.3, zorder=8,
        )

    add_reference_geometry(ax, local)
    y0, y1, z0, z1 = bounds
    ax.set_xlim(y0, y1)
    ax.set_ylim(z0, z1)
    ax.set_aspect("equal", adjustable="box")
    ax.set_xlabel("Rack depth y (m)")
    ax.set_ylabel("Rack height z (m)")
    ax.grid(color="white", alpha=0.18, linewidth=0.5)
    wrap_width = 105 if figure_size[0] >= 10 else 58
    if velocity is not None:
        subtitle += "; arrows = Uy/Uz direction; color = total |U|"
    wrapped_subtitle = textwrap.fill(subtitle, width=wrap_width)
    ax.set_title(
        f"{title}\n{wrapped_subtitle}", fontsize=11.5, weight="bold", pad=10
    )
    fig.savefig(
        output, dpi=190, facecolor="white",
        metadata={"Title": title, "Description": subtitle},
    )
    plt.close(fig)


def describe(values: np.ndarray) -> dict[str, float | int]:
    values = np.asarray(values, dtype=float)
    return {
        "n": int(values.size),
        "minimum": float(np.min(values)),
        "p01": float(np.percentile(values, 1)),
        "median": float(np.median(values)),
        "mean": float(np.mean(values)),
        "p99": float(np.percentile(values, 99)),
        "p995": float(np.percentile(values, 99.5)),
        "maximum": float(np.max(values)),
    }


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_record(path: Path, base: Path | None = None) -> dict[str, object]:
    resolved = path.resolve()
    try:
        shown = str(resolved.relative_to(base.resolve())) if base else str(resolved)
    except ValueError:
        shown = str(resolved)
    stat = resolved.stat()
    return {
        "path": shown,
        "bytes": stat.st_size,
        "sha256": sha256(resolved),
        "last_write_utc": datetime.fromtimestamp(
            stat.st_mtime, timezone.utc
        ).isoformat(),
    }


def main() -> None:
    args = parse_args()
    output_dir = args.output_dir.resolve()
    sampled_dir = args.sampled_dir.resolve()
    sampled_vtk_dir = args.sampled_vtk_dir.resolve()
    case = args.case.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    global_xyz, global_triangles, global_fields = load_legacy_vtk_polydata(
        sampled_vtk_dir / "rackAndDellCenterX.vtk"
    )
    ni_xyz, ni_triangles, ni_fields = load_legacy_vtk_polydata(
        sampled_vtk_dir / "niCenterX.vtk"
    )
    for surface, fields in (
        ("rackAndDellCenterX", global_fields), ("niCenterX", ni_fields)
    ):
        missing = {"U", "T", "p_rgh"} - fields.keys()
        if missing:
            raise ValueError(f"{surface} is missing VTK arrays: {sorted(missing)}")

    global_u = global_fields["U"]
    global_t = global_fields["T"]
    global_p = global_fields["p_rgh"]
    ni_u = ni_fields["U"]
    ni_t = ni_fields["T"]
    ni_p = ni_fields["p_rgh"]
    global_yz_u = global_xyz[:, [1, 2]]
    global_yz_t = global_yz_u
    global_yz_p = global_yz_u
    ni_yz_u = ni_xyz[:, [1, 2]]
    ni_yz_t = ni_yz_u
    global_speed = np.linalg.norm(global_u, axis=1)
    ni_speed = np.linalg.norm(ni_u, axis=1)
    global_temperature_c = global_t[:, 0] - 273.15
    ni_temperature_c = ni_t[:, 0] - 273.15
    global_pressure_relative = global_p[:, 0] - PRESSURE_REFERENCE_PA

    plots = [
        {
            "filename": "global_speed_vectors_x0p291.png",
            "surface": "rackAndDellCenterX",
            "selection": "whole rack",
            "field": "|U| and in-plane U direction",
            "clim": SPEED_CLIM_MPS,
        },
        {
            "filename": "global_p_rgh_gauge_x0p291.png",
            "surface": "rackAndDellCenterX",
            "selection": "whole rack",
            "field": f"p_rgh - {PRESSURE_REFERENCE_PA:g} Pa",
            "clim": PRESSURE_CLIM_PA,
        },
        {
            "filename": "global_air_temperature_early_transient_x0p291.png",
            "surface": "rackAndDellCenterX",
            "selection": "whole rack",
            "field": "fluid T in degC",
            "clim": TEMPERATURE_CLIM_C,
        },
        {
            "filename": "dell_speed_vectors_x0p291.png",
            "surface": "rackAndDellCenterX",
            "selection": "Dell crop",
            "field": "|U| and in-plane U direction",
            "clim": SPEED_CLIM_MPS,
        },
        {
            "filename": "dell_air_temperature_early_transient_x0p291.png",
            "surface": "rackAndDellCenterX",
            "selection": "Dell crop",
            "field": "fluid T in degC",
            "clim": TEMPERATURE_CLIM_C,
        },
        {
            "filename": "ni_speed_vectors_x0p222.png",
            "surface": "niCenterX",
            "selection": "NI crop",
            "field": "|U| and in-plane U direction",
            "clim": SPEED_CLIM_MPS,
        },
        {
            "filename": "ni_air_temperature_early_transient_x0p222.png",
            "surface": "niCenterX",
            "selection": "NI crop",
            "field": "fluid T in degC",
            "clim": TEMPERATURE_CLIM_C,
        },
    ]

    common_time = "Saved checkpoint t=1.600 s — EARLY TRANSIENT, airflow not converged"
    render_scalar(
        output=output_dir / plots[0]["filename"],
        coords_yz=global_yz_u, values=global_speed, velocity=global_u,
        bounds=RACK_BOUNDS_YZ, title="Whole-rack fluid speed and direction",
        subtitle=f"x = 0.29115 m center plane; {common_time}",
        colorbar_label="Total speed |U| (m/s)", clim=SPEED_CLIM_MPS,
        cmap="turbo", figure_size=(8.5, 12.2), vector_bins=(14, 23),
        arrow_length=0.040, triangles=global_triangles,
    )
    render_scalar(
        output=output_dir / plots[1]["filename"],
        coords_yz=global_yz_p, values=global_pressure_relative,
        bounds=RACK_BOUNDS_YZ, title="Whole-rack p_rgh spatial pattern",
        subtitle=(f"x = 0.29115 m; reference {PRESSURE_REFERENCE_PA:g} Pa removed; "
                  f"{common_time}"),
        colorbar_label=f"p_rgh - {PRESSURE_REFERENCE_PA:g} Pa (Pa)",
        clim=PRESSURE_CLIM_PA, cmap="coolwarm", figure_size=(8.5, 12.2),
        triangles=global_triangles,
    )
    render_scalar(
        output=output_dir / plots[2]["filename"],
        coords_yz=global_yz_t, values=global_temperature_c,
        bounds=RACK_BOUNDS_YZ, title="Whole-rack fluid temperature",
        subtitle=f"x = 0.29115 m center plane; {common_time}",
        colorbar_label="Fluid temperature (degC)", clim=TEMPERATURE_CLIM_C,
        cmap="inferno", figure_size=(8.5, 12.2), triangles=global_triangles,
    )
    render_scalar(
        output=output_dir / plots[3]["filename"],
        coords_yz=global_yz_u, values=global_speed, velocity=global_u,
        bounds=DELL_CROP_YZ, title="Dell PowerEdge R470 local airflow",
        subtitle=f"x = 0.29115 m center plane; {common_time}",
        colorbar_label="Total speed |U| (m/s)", clim=SPEED_CLIM_MPS,
        cmap="turbo", figure_size=(15.0, 4.1), local="Dell",
        vector_bins=(24, 4), arrow_length=0.026, triangles=global_triangles,
    )
    render_scalar(
        output=output_dir / plots[4]["filename"],
        coords_yz=global_yz_t, values=global_temperature_c,
        bounds=DELL_CROP_YZ, title="Dell PowerEdge R470 local air temperature",
        subtitle=f"x = 0.29115 m center plane; {common_time}",
        colorbar_label="Fluid temperature (degC)", clim=TEMPERATURE_CLIM_C,
        cmap="inferno", figure_size=(15.0, 4.1), local="Dell",
        triangles=global_triangles,
    )
    render_scalar(
        output=output_dir / plots[5]["filename"],
        coords_yz=ni_yz_u, values=ni_speed, velocity=ni_u,
        bounds=NI_CROP_YZ, title="NI PXIe chassis local airflow",
        subtitle=f"x = 0.22225 m center plane; {common_time}",
        colorbar_label="Total speed |U| (m/s)", clim=SPEED_CLIM_MPS,
        cmap="turbo", figure_size=(8.3, 7.3), local="NI",
        vector_bins=(14, 12), arrow_length=0.014, triangles=ni_triangles,
    )
    render_scalar(
        output=output_dir / plots[6]["filename"],
        coords_yz=ni_yz_t, values=ni_temperature_c,
        bounds=NI_CROP_YZ, title="NI PXIe chassis local air temperature",
        subtitle=f"x = 0.22225 m center plane; {common_time}",
        colorbar_label="Fluid temperature (degC)", clim=TEMPERATURE_CLIM_C,
        cmap="inferno", figure_size=(8.3, 7.3), local="NI",
        triangles=ni_triangles,
    )

    selections = [
        ("whole_rack", "rackAndDellCenterX", RACK_BOUNDS_YZ,
         global_yz_u, global_speed, global_yz_t, global_temperature_c,
         global_yz_p, global_p[:, 0]),
        ("dell_crop", "rackAndDellCenterX", DELL_CROP_YZ,
         global_yz_u, global_speed, global_yz_t, global_temperature_c,
         global_yz_p, global_p[:, 0]),
        ("ni_crop", "niCenterX", NI_CROP_YZ,
         ni_yz_u, ni_speed, ni_yz_t, ni_temperature_c,
         ni_yz_u, ni_p[:, 0]),
    ]
    statistics_rows = []
    for name, surface, bounds, speed_coords, speed, temp_coords, temp, p_coords, pressure in selections:
        for field, coords, values in (
            ("speed_m_per_s", speed_coords, speed),
            ("temperature_degC", temp_coords, temp),
            ("p_rgh_Pa", p_coords, pressure),
            ("p_rgh_relative_Pa", p_coords, pressure - PRESSURE_REFERENCE_PA),
        ):
            stats = describe(values[crop_mask(coords, bounds)])
            statistics_rows.append(
                {"selection": name, "surface": surface, "field": field, **stats}
            )
    stats_path = output_dir / "field_statistics.csv"
    with stats_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(statistics_rows[0]))
        writer.writeheader()
        writer.writerows(statistics_rows)

    time_dir = case / CHECKPOINT_TIME / "fluid"
    source_files = [
        time_dir / "U", time_dir / "T", time_dir / "p_rgh",
        case / "constant" / "fluid" / "polyMesh" / "points",
        case / "constant" / "fluid" / "polyMesh" / "faces",
        case / "constant" / "fluid" / "polyMesh" / "owner",
        case / "constant" / "fluid" / "polyMesh" / "neighbour",
        case / "constant" / "fluid" / "polyMesh" / "boundary",
        case / "geometry.txt",
    ]
    local_evidence = [
        Path(__file__), output_dir / "README.md",
        output_dir / "sampling_controlDict",
        output_dir / "sampling_controlDict_vtk",
        output_dir / "postprocess_sampling.stdout.log",
        output_dir / "postprocess_sampling_vtk.stdout.log", stats_path,
        *sorted(sampled_dir.glob("*.raw")),
        *sorted(sampled_vtk_dir.glob("*.vtk")),
    ]
    for plot in plots:
        plot_path = output_dir / str(plot["filename"])
        plot["bytes"] = plot_path.stat().st_size
        plot["sha256"] = sha256(plot_path)

    manifest = {
        "schema": "thermal-sim-fixed-field-plots-v1",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "checkpoint": {
            "case": str(case),
            "time_directory": CHECKPOINT_TIME,
            "physical_time_s": 1.6,
            "region": "fluid",
            "status": "early transient; airflow convergence gates failed at this checkpoint",
        },
        "method": {
            "sampling": "OpenFOAM 2606 postProcess surfaces/cuttingPlane with cellPoint interpolation; ASCII legacy VTK preserves exact sampled faces and 12-digit raw samples are retained",
            "source_case_writes": "none; sampling ran through read-only links in a WSL /tmp case",
            "rendering": "bundled NumPy/Matplotlib runtime using OpenFOAM's exact sampled-plane polygon connectivity",
            "pressure_reference_Pa": PRESSURE_REFERENCE_PA,
            "fixed_scales": {
                "speed_m_per_s": SPEED_CLIM_MPS,
                "temperature_degC": TEMPERATURE_CLIM_C,
                "p_rgh_relative_Pa": PRESSURE_CLIM_PA,
            },
        },
        "runtime": {
            "python_executable": sys.executable,
            "python_version": platform.python_version(),
            "numpy_version": np.__version__,
            "matplotlib_version": matplotlib.__version__,
        },
        "source_files": [file_record(path) for path in source_files],
        "local_evidence_files": [
            file_record(path, output_dir) for path in local_evidence
        ],
        "plots": plots,
        "limitations": [
            "This is a 1.6 s early transient; it is not a converged thermal or airflow result.",
            "Only the fluid region is shown; solid components appear as gaps.",
            "The 2-D arrows show Uy and Uz direction on x-normal planes; color uses total 3-D speed.",
            "The unfinished NI separator walls are absent from this solved geometry, so NI mixing is not physically bounded.",
            "Cutting-plane interpolation and polygon rendering are visualization operations, not new solver data.",
        ],
    }
    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"Rendered {len(plots)} fixed-scale images into {output_dir}")
    print(f"Statistics: {stats_path}")
    print(f"Manifest: {manifest_path}")


if __name__ == "__main__":
    main()
