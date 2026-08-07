"""Fit Thermal Sim fan-curve coefficients from manufacturer data points.

The project uses dP(Q) = a - b*Q - c*Q^2, with Q in m^3/s and dP in Pa.
This utility accepts common datasheet units and prints a copy-ready TOML block.
It uses only the Python standard library.
"""

from __future__ import annotations

import argparse
import csv
import html
import math
from pathlib import Path


FLOW_TO_M3S = {
    "m3/s": 1.0,
    "cfm": 0.00047194745,
    "l/s": 0.001,
    "m3/h": 1.0 / 3600.0,
}

PRESSURE_TO_PA = {
    "pa": 1.0,
    "kpa": 1000.0,
    "inh2o": 249.08891,
    "mmh2o": 9.80665,
}


def solve_3x3(matrix: list[list[float]], rhs: list[float]) -> list[float]:
    augmented = [row[:] + [value] for row, value in zip(matrix, rhs)]
    for column in range(3):
        pivot = max(range(column, 3), key=lambda row: abs(augmented[row][column]))
        if abs(augmented[pivot][column]) < 1e-14:
            raise ValueError(
                "Fan points do not define a unique quadratic; use at least "
                "three distinct flow values."
            )
        augmented[column], augmented[pivot] = augmented[pivot], augmented[column]
        divisor = augmented[column][column]
        augmented[column] = [value / divisor for value in augmented[column]]
        for row in range(3):
            if row == column:
                continue
            factor = augmented[row][column]
            augmented[row] = [
                value - factor * pivot_value
                for value, pivot_value in zip(augmented[row], augmented[column])
            ]
    return [augmented[row][3] for row in range(3)]


def fit_curve(points_si: list[tuple[float, float]]) -> tuple[float, float, float]:
    if len(points_si) < 3:
        raise ValueError("At least three fan-curve points are required.")
    flows = [point[0] for point in points_si]
    if min(flows) < 0:
        raise ValueError("Flow values must be non-negative.")
    q_scale = max(flows)
    if q_scale <= 0:
        raise ValueError("At least one flow value must be greater than zero.")

    # Scaling Q before the least-squares solve avoids an ill-conditioned
    # matrix because fan flows in m^3/s are commonly much smaller than one.
    rows = [(1.0, q / q_scale, (q / q_scale) ** 2) for q in flows]
    normal = [[sum(row[i] * row[j] for row in rows) for j in range(3)]
              for i in range(3)]
    rhs = [sum(row[i] * pressure for row, (_, pressure) in zip(rows, points_si))
           for i in range(3)]
    intercept, linear, quadratic = solve_3x3(normal, rhs)
    return intercept, -linear / q_scale, -quadratic / (q_scale * q_scale)


def parse_point(value: str) -> tuple[float, float]:
    try:
        flow, pressure = value.split(",", 1)
        return float(flow), float(pressure)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "Points must use FLOW,PRESSURE, for example 120,0.45"
        ) from error


def read_csv_points(path: Path) -> list[tuple[float, float]]:
    with path.open(newline="", encoding="utf-8-sig") as stream:
        rows = list(csv.DictReader(stream))
    if not rows or "flow" not in rows[0] or "pressure" not in rows[0]:
        raise ValueError("CSV must contain flow and pressure columns.")
    return [(float(row["flow"]), float(row["pressure"])) for row in rows]


def read_rpm_load_csv(path: Path) -> list[tuple[float, float]]:
    with path.open(newline="", encoding="utf-8-sig") as stream:
        rows = list(csv.DictReader(stream))
    if not rows or "load_percent" not in rows[0] or "rpm" not in rows[0]:
        raise ValueError("RPM CSV must contain load_percent and rpm columns.")
    return [(float(row["load_percent"]), float(row["rpm"])) for row in rows]


def rpm_at_load(points: list[tuple[float, float]], load_percent: float) -> float:
    if not points:
        raise ValueError("At least one RPM/load point is required.")
    if not 0.0 <= load_percent <= 100.0:
        raise ValueError("Target load percentage must be between 0 and 100.")
    ordered = sorted(points)
    if any(not 0.0 <= load <= 100.0 or rpm <= 0.0
           for load, rpm in ordered):
        raise ValueError("Loads must be 0-100 percent and RPM must be positive.")
    if any(ordered[index][0] == ordered[index - 1][0]
           for index in range(1, len(ordered))):
        raise ValueError("RPM schedule load percentages must be distinct.")
    if load_percent <= ordered[0][0]:
        return ordered[0][1]
    if load_percent >= ordered[-1][0]:
        return ordered[-1][1]
    for (load0, rpm0), (load1, rpm1) in zip(ordered, ordered[1:]):
        if load_percent <= load1:
            fraction = (load_percent - load0) / (load1 - load0)
            return rpm0 + fraction * (rpm1 - rpm0)
    raise AssertionError("unreachable RPM interpolation state")


def scale_curve_for_rpm(a: float, b: float, c: float,
                        reference_rpm: float,
                        target_rpm: float) -> tuple[float, float, float]:
    if reference_rpm <= 0.0 or target_rpm <= 0.0:
        raise ValueError("Reference and target RPM must be positive.")
    ratio = target_rpm / reference_rpm
    # Fan affinity laws: flow scales with N and pressure with N^2.
    return a * ratio * ratio, b * ratio, c


def write_curve_plot(path: Path, points_si: list[tuple[float, float]],
                     a: float, b: float, c: float, flow_unit: str,
                     pressure_unit: str, title: str) -> None:
    """Write a dependency-free SVG plot in the user's selected units."""
    width, height = 800, 520
    left, right, top, bottom = 85, 25, 55, 70
    plot_width = width - left - right
    plot_height = height - top - bottom
    flow_factor = FLOW_TO_M3S[flow_unit]
    pressure_factor = PRESSURE_TO_PA[pressure_unit]
    point_values = [(q / flow_factor, p / pressure_factor)
                    for q, p in points_si]
    max_flow = max(q for q, _ in point_values)
    if max_flow <= 0.0:
        raise ValueError("Plot requires at least one positive flow value.")
    curve_values = []
    for index in range(201):
        flow = max_flow * index / 200.0
        flow_si = flow * flow_factor
        pressure = max(a - b * flow_si - c * flow_si * flow_si, 0.0)
        curve_values.append((flow, pressure / pressure_factor))
    max_pressure = max([p for _, p in point_values] +
                       [p for _, p in curve_values] + [1.0])
    max_flow *= 1.05
    max_pressure *= 1.08

    def x_pixel(flow: float) -> float:
        return left + plot_width * flow / max_flow

    def y_pixel(pressure: float) -> float:
        return top + plot_height * (1.0 - pressure / max_pressure)

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
        f'height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        '<style>text{font-family:Arial,sans-serif;fill:#222}'
        '.grid{stroke:#ddd;stroke-width:1}.axis{stroke:#222;stroke-width:1.5}'
        '.curve{fill:none;stroke:#1769aa;stroke-width:3}'
        '.point{fill:#d84315;stroke:white;stroke-width:1.5}</style>',
        f'<text x="{width/2}" y="30" text-anchor="middle" '
        f'font-size="20">{html.escape(title)}</text>',
    ]
    for index in range(6):
        fraction = index / 5.0
        x = left + plot_width * fraction
        y = top + plot_height * (1.0 - fraction)
        flow_label = max_flow * fraction
        pressure_label = max_pressure * fraction
        svg.extend([
            f'<line class="grid" x1="{x:.2f}" y1="{top}" '
            f'x2="{x:.2f}" y2="{top + plot_height}"/>',
            f'<text x="{x:.2f}" y="{top + plot_height + 24}" '
            f'text-anchor="middle" font-size="12">{flow_label:.4g}</text>',
            f'<line class="grid" x1="{left}" y1="{y:.2f}" '
            f'x2="{left + plot_width}" y2="{y:.2f}"/>',
            f'<text x="{left - 10}" y="{y + 4:.2f}" text-anchor="end" '
            f'font-size="12">{pressure_label:.4g}</text>',
        ])
    svg.extend([
        f'<line class="axis" x1="{left}" y1="{top + plot_height}" '
        f'x2="{left + plot_width}" y2="{top + plot_height}"/>',
        f'<line class="axis" x1="{left}" y1="{top}" '
        f'x2="{left}" y2="{top + plot_height}"/>',
        f'<text x="{left + plot_width/2}" y="{height - 18}" '
        f'text-anchor="middle" font-size="15">Flow ({html.escape(flow_unit)})</text>',
        f'<text x="20" y="{top + plot_height/2}" text-anchor="middle" '
        f'font-size="15" transform="rotate(-90 20 {top + plot_height/2})">'
        f'Pressure ({html.escape(pressure_unit)})</text>',
        '<polyline class="curve" points="' + ' '.join(
            f'{x_pixel(q):.2f},{y_pixel(p):.2f}' for q, p in curve_values) + '"/>',
    ])
    svg.extend(
        f'<circle class="point" cx="{x_pixel(q):.2f}" '
        f'cy="{y_pixel(p):.2f}" r="5"/>' for q, p in point_values
    )
    svg.append('</svg>')
    path.write_text("\n".join(svg) + "\n", encoding="utf-8")


def interactive_points() -> list[tuple[float, float]]:
    print("Enter fan points as FLOW,PRESSURE. Press Enter after the last point.")
    points: list[tuple[float, float]] = []
    while True:
        value = input(f"Point {len(points) + 1}: ").strip()
        if not value:
            break
        points.append(parse_point(value))
    return points


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Fit dP(Q) = a - bQ - cQ^2 for Thermal Sim."
    )
    parser.add_argument("--name", default="fitted_fan", help="TOML curve name")
    parser.add_argument("--rho-rated", type=float, default=1.2,
                        help="Datasheet air density in kg/m^3")
    parser.add_argument("--flow-unit", choices=FLOW_TO_M3S, default="cfm")
    parser.add_argument("--pressure-unit", choices=PRESSURE_TO_PA, default="inh2o")
    parser.add_argument("--point", type=parse_point, action="append", default=[],
                        help="FLOW,PRESSURE; repeat for every datasheet point")
    parser.add_argument("--csv", type=Path,
                        help="CSV containing flow and pressure columns")
    parser.add_argument("--rpm-load-point", type=parse_point, action="append",
                        default=[], metavar="LOAD_PERCENT,RPM",
                        help="fan speed schedule point; repeat as needed")
    parser.add_argument("--rpm-load-csv", type=Path,
                        help="CSV containing load_percent and rpm columns")
    parser.add_argument("--target-load-percent", type=float,
                        help="load at which to evaluate the RPM schedule")
    parser.add_argument("--reference-rpm", type=float,
                        help="RPM at which the supplied pressure-flow points were measured")
    parser.add_argument("--plot", nargs="?", const="", metavar="PATH",
                        help="write an SVG plot; default: <name>_fan_curve.svg")
    return parser


def main() -> None:
    args = build_parser().parse_args()
    points = list(args.point)
    if args.csv:
        points.extend(read_csv_points(args.csv))
    rpm_load_points = list(args.rpm_load_point)
    if args.rpm_load_csv:
        rpm_load_points.extend(read_rpm_load_csv(args.rpm_load_csv))

    target_rpm = None
    if rpm_load_points:
        if args.target_load_percent is None:
            raise SystemExit(
                "RPM/load data requires --target-load-percent.")
        target_rpm = rpm_at_load(rpm_load_points,args.target_load_percent)
        print("\nRPM/load schedule:")
        for load,rpm in sorted(rpm_load_points):
            print(f"  {load:.6g}% load: {rpm:.6g} RPM")
        print(f"  Selected at {args.target_load_percent:.6g}% load: "
              f"{target_rpm:.6g} RPM")

    if not points and not rpm_load_points:
        points = interactive_points()
    if not points:
        print("\nRPM versus load defines a controller schedule, not a pressure-flow "
              "fan curve. Add pressure-flow --point/--csv data and "
              "--reference-rpm to generate Thermal Sim a/b/c coefficients.")
        return

    points_si = [
        (flow * FLOW_TO_M3S[args.flow_unit],
         pressure * PRESSURE_TO_PA[args.pressure_unit])
        for flow, pressure in points
    ]
    a, b, c = fit_curve(points_si)
    plot_points_si = list(points_si)
    predictions = [a - b * flow - c * flow * flow for flow, _ in points_si]
    residuals = [prediction - pressure
                 for prediction, (_, pressure) in zip(predictions, points_si)]
    rmse = math.sqrt(sum(value * value for value in residuals) / len(residuals))
    mean_pressure = sum(pressure for _, pressure in points_si) / len(points_si)
    total_variation = sum((pressure - mean_pressure) ** 2
                          for _, pressure in points_si)
    residual_variation = sum(value * value for value in residuals)
    r_squared = 1.0 - residual_variation / total_variation if total_variation else 1.0

    if target_rpm is not None:
        if args.reference_rpm is None:
            raise SystemExit(
                "Combining RPM/load and pressure-flow data requires --reference-rpm.")
        a, b, c = scale_curve_for_rpm(
            a,b,c,args.reference_rpm,target_rpm)
        speed_ratio = target_rpm / args.reference_rpm
        plot_points_si = [
            (flow * speed_ratio,pressure * speed_ratio * speed_ratio)
            for flow,pressure in points_si
        ]
        print(f"\nScaled pressure-flow curve from {args.reference_rpm:.6g} "
              f"to {target_rpm:.6g} RPM using fan affinity laws.")

    print("\nFit in Thermal Sim SI units:")
    print(f"  dP(Q) = {a:.9g} - ({b:.9g}) Q - ({c:.9g}) Q^2")
    print(f"  source-data fit RMSE = {rmse:.6g} Pa; R^2 = {r_squared:.6g}")
    if b < 0 or c < 0:
        print("WARNING: The fitted curve is not monotonically decreasing over "
              "all Q; inspect the data and fitted operating range.")

    print("\nCopy into library/fan_curves/fan_curves.toml:\n")
    print("[[fan_curve]]")
    print(f'name = "{args.name}"')
    print(f"rho_rated = {args.rho_rated:.9g}")
    print(f"a = {a:.12g}")
    print(f"b = {b:.12g}")
    print(f"c = {c:.12g}")

    if args.plot is not None:
        safe_name = "".join(
            character if character.isalnum() or character in "-_" else "_"
            for character in args.name
        ).strip("_") or "fitted_fan"
        plot_path = Path(args.plot) if args.plot else Path(
            f"{safe_name}_fan_curve.svg")
        write_curve_plot(plot_path,plot_points_si,a,b,c,args.flow_unit,
                         args.pressure_unit,args.name)
        print(f"\nPlot written to {plot_path}")


if __name__ == "__main__":
    main()
