"""Fit Thermal Sim fan-curve coefficients from manufacturer data points.

The project uses dP(Q) = a - b*Q - c*Q^2, with Q in m^3/s and dP in Pa.
This utility accepts common datasheet units and prints a copy-ready TOML block.
It uses only the Python standard library.
"""

from __future__ import annotations

import argparse
import csv
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
    parser.add_argument("--pressure-unit", choices=PRESSURE_TO_PA, default="pa")
    parser.add_argument("--point", type=parse_point, action="append", default=[],
                        help="FLOW,PRESSURE; repeat for every datasheet point")
    parser.add_argument("--csv", type=Path,
                        help="CSV containing flow and pressure columns")
    return parser


def main() -> None:
    args = build_parser().parse_args()
    points = list(args.point)
    if args.csv:
        points.extend(read_csv_points(args.csv))
    if not points:
        points = interactive_points()

    points_si = [
        (flow * FLOW_TO_M3S[args.flow_unit],
         pressure * PRESSURE_TO_PA[args.pressure_unit])
        for flow, pressure in points
    ]
    a, b, c = fit_curve(points_si)
    predictions = [a - b * flow - c * flow * flow for flow, _ in points_si]
    residuals = [prediction - pressure
                 for prediction, (_, pressure) in zip(predictions, points_si)]
    rmse = math.sqrt(sum(value * value for value in residuals) / len(residuals))
    mean_pressure = sum(pressure for _, pressure in points_si) / len(points_si)
    total_variation = sum((pressure - mean_pressure) ** 2
                          for _, pressure in points_si)
    residual_variation = sum(value * value for value in residuals)
    r_squared = 1.0 - residual_variation / total_variation if total_variation else 1.0

    print("\nFit in Thermal Sim SI units:")
    print(f"  dP(Q) = {a:.9g} - ({b:.9g}) Q - ({c:.9g}) Q^2")
    print(f"  RMSE = {rmse:.6g} Pa; R^2 = {r_squared:.6g}")
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


if __name__ == "__main__":
    main()
