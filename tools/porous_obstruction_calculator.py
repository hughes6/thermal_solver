"""Quantify rack obstructions and generate copy-ready porous-region TOML."""

from __future__ import annotations

import argparse
import csv
import json
import math
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class PressurePoint:
    velocity: float
    pressure: float


def positive(value: str) -> float:
    number = float(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return number


def triplet(text: str) -> tuple[float, float, float]:
    try:
        values = tuple(float(item.strip()) for item in text.split(","))
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected X,Y,Z") from error
    if len(values) != 3:
        raise argparse.ArgumentTypeError("expected X,Y,Z")
    return values


def pair(text: str) -> tuple[float, float]:
    try:
        values = tuple(float(item.strip()) for item in text.split(","))
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected VALUE,VALUE") from error
    if len(values) != 2:
        raise argparse.ArgumentTypeError("expected VALUE,VALUE")
    return values


def cable(text: str) -> tuple[int, float, float]:
    values = text.split(",")
    if len(values) != 3:
        raise argparse.ArgumentTypeError("expected COUNT,DIAMETER_MM,LENGTH_M")
    try:
        count = int(values[0])
        diameter_mm = float(values[1])
        length_m = float(values[2])
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "expected COUNT,DIAMETER_MM,LENGTH_M") from error
    if count <= 0 or diameter_mm <= 0 or length_m <= 0:
        raise argparse.ArgumentTypeError("cable values must be positive")
    return count, diameter_mm, length_m


def geometry(size: tuple[float, float, float], direction: str) -> tuple[float, float, float]:
    width, depth, height = size
    if min(size) <= 0:
        raise ValueError("Every --size dimension must be positive.")
    if direction == "x":
        return depth * height, width, width * depth * height
    if direction == "y":
        return width * height, depth, width * depth * height
    return width * depth, height, width * depth * height


def cable_void_percent(
    cables: list[tuple[int, float, float]], zone_volume: float
) -> tuple[float, float]:
    solid_volume = sum(
        count * math.pi * (diameter_mm / 1000.0) ** 2 * length_m / 4.0
        for count, diameter_mm, length_m in cables
    )
    if solid_volume >= zone_volume:
        raise ValueError(
            "Cable solid volume equals or exceeds the porous-zone volume; "
            "check cable lengths, diameters, and zone dimensions."
        )
    return solid_volume, 100.0 * (1.0 - solid_volume / zone_volume)


def fit_pressure_points(
    points: list[PressurePoint], length: float, viscosity: float, density: float
) -> dict[str, float]:
    if len(points) < 2:
        raise ValueError("At least two pressure points are required for a D/F fit.")
    if any(point.velocity <= 0 or point.pressure < 0 for point in points):
        raise ValueError(
            "Pressure fitting requires positive velocity and nonnegative "
            "pressure-loss magnitudes.")
    if viscosity <= 0 or density <= 0 or length <= 0:
        raise ValueError("Air properties and obstruction thickness must be positive.")
    x1 = [point.velocity for point in points]
    x2 = [point.velocity * abs(point.velocity) for point in points]
    y = [point.pressure / length for point in points]
    s11 = sum(value * value for value in x1)
    s22 = sum(value * value for value in x2)
    s12 = sum(a * b for a, b in zip(x1, x2))
    t1 = sum(a * value for a, value in zip(x1, y))
    t2 = sum(b * value for b, value in zip(x2, y))
    candidates: list[tuple[float, float]] = [(0.0, 0.0)]
    determinant = s11 * s22 - s12 * s12
    if determinant > 1.0e-20:
        a = (t1 * s22 - t2 * s12) / determinant
        b = (t2 * s11 - t1 * s12) / determinant
        if a >= 0 and b >= 0:
            candidates.append((a, b))
    if s11 > 0:
        candidates.append((max(0.0, t1 / s11), 0.0))
    if s22 > 0:
        candidates.append((0.0, max(0.0, t2 / s22)))

    def squared_error(candidate: tuple[float, float]) -> float:
        a, b = candidate
        return sum(
            (actual - a * linear - b * quadratic) ** 2
            for linear, quadratic, actual in zip(x1, x2, y)
        )

    linear_coefficient, quadratic_coefficient = min(
        candidates, key=squared_error)
    darcy = linear_coefficient / viscosity
    forchheimer = 2.0 * quadratic_coefficient / density
    predictions = [
        length * (
            viscosity * darcy * point.velocity
            + 0.5 * density * forchheimer
            * point.velocity * abs(point.velocity)
        )
        for point in points
    ]
    residuals = [predicted - point.pressure
                 for predicted, point in zip(predictions, points)]
    rmse = math.sqrt(sum(value * value for value in residuals) / len(points))
    mean_pressure = sum(abs(point.pressure) for point in points) / len(points)
    pressure_mean = sum(point.pressure for point in points) / len(points)
    total_variation = sum(
        (point.pressure - pressure_mean) ** 2 for point in points)
    residual_variation = sum(value * value for value in residuals)
    return {
        "darcy_coefficient_1_m2": darcy,
        "forchheimer_coefficient_1_m": forchheimer,
        "rmse_pa": rmse,
        "normalized_rmse_percent": (
            100.0 * rmse / mean_pressure if mean_pressure > 0 else 0.0),
        "maximum_absolute_error_pa": max(abs(value) for value in residuals),
        "r_squared": (1.0 - residual_variation / total_variation
                      if total_variation > 0 else
                      (1.0 if residual_variation == 0 else 0.0)),
    }


def pressure_prediction(
    velocity: float, length: float, viscosity: float, density: float,
    darcy: float, forchheimer: float,
) -> float:
    return length * (
        viscosity * darcy * velocity
        + 0.5 * density * forchheimer * velocity * abs(velocity))


def read_pressure_csv(path: Path, gross_area: float) -> list[PressurePoint]:
    points: list[PressurePoint] = []
    with path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        fields = set(reader.fieldnames or [])
        if "pressure_pa" not in fields:
            raise ValueError("Pressure CSV requires a pressure_pa column.")
        if "velocity_m_s" not in fields and "flow_m3_s" not in fields:
            raise ValueError(
                "Pressure CSV requires velocity_m_s or flow_m3_s.")
        for number, row in enumerate(reader, start=2):
            try:
                pressure = float(row["pressure_pa"])
                velocity = (float(row["velocity_m_s"])
                            if row.get("velocity_m_s") not in (None, "")
                            else float(row["flow_m3_s"]) / gross_area)
            except (TypeError, ValueError, KeyError) as error:
                raise ValueError(
                    f"Invalid pressure CSV numeric value on row {number}.") from error
            points.append(PressurePoint(velocity, pressure))
    return points


def toml_snippet(
    args: argparse.Namespace, porosity: float | None,
    darcy: float | None, forchheimer: float | None,
) -> str:
    escaped_name = args.name.replace("\\", "\\\\").replace('"', '\\"')
    lines = ["[[porous_regions]]", f'name = "{escaped_name}"']
    if darcy is not None and forchheimer is not None:
        lines.extend([
            f"darcy_coefficient = {darcy:.10g}",
            f"forchheimer_coefficient = {forchheimer:.10g}",
        ])
    elif porosity is not None and args.discharge_coefficient is not None:
        lines.extend([
            f"porosity_percent = {porosity:.10g}",
            f"discharge_coefficient = {args.discharge_coefficient:.10g}",
        ])
    else:
        raise ValueError(
            "TOML output requires pressure points, or porosity plus "
            "--discharge-coefficient."
        )
    if args.transverse_darcy_coefficient is not None:
        lines.append(
            "transverse_darcy_coefficient = "
            f"{args.transverse_darcy_coefficient:.10g}")
    if args.transverse_forchheimer_coefficient is not None:
        lines.append(
            "transverse_forchheimer_coefficient = "
            f"{args.transverse_forchheimer_coefficient:.10g}")
    labels = ("x", "y", "z")
    lines.extend(["", "[porous_regions.position]", 'units = "m"'])
    lines.extend(f"{label} = {value:.10g}"
                 for label, value in zip(labels, args.position))
    lines.extend(["", "[porous_regions.size]", 'units = "m"'])
    for label, value in zip(("width", "depth", "height"), args.size):
        lines.append(f"{label} = {value:.10g}")
    normal = {"x": (1.0, 0.0, 0.0), "y": (0.0, 1.0, 0.0),
              "z": (0.0, 0.0, 1.0)}[args.direction]
    lines.extend(["", "[porous_regions.direction]"])
    lines.extend(f"{label} = {value:.1f}"
                 for label, value in zip(labels, normal))
    return "\n".join(lines) + "\n"


def write_svg(path: Path, rows: list[dict[str, float]]) -> None:
    if not rows:
        return
    width, height, margin = 760, 480, 60
    max_x = max(row["velocity_m_s"] for row in rows) or 1.0
    max_y = max(max(row["measured_pressure_pa"], row["predicted_pressure_pa"])
                for row in rows) or 1.0
    def sx(value: float) -> float:
        return margin + value / max_x * (width - 2 * margin)
    def sy(value: float) -> float:
        return height - margin - value / max_y * (height - 2 * margin)
    measured = " ".join(
        f'{sx(row["velocity_m_s"]):.2f},{sy(row["measured_pressure_pa"]):.2f}'
        for row in rows)
    predicted = " ".join(
        f'{sx(row["velocity_m_s"]):.2f},{sy(row["predicted_pressure_pa"]):.2f}'
        for row in rows)
    circles = "\n".join(
        f'<circle cx="{sx(row["velocity_m_s"]):.2f}" '
        f'cy="{sy(row["measured_pressure_pa"]):.2f}" r="5" fill="#1f77b4"/>'
        for row in rows)
    path.write_text(f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">
<rect width="100%" height="100%" fill="white"/>
<line x1="{margin}" y1="{height-margin}" x2="{width-margin}" y2="{height-margin}" stroke="black"/>
<line x1="{margin}" y1="{margin}" x2="{margin}" y2="{height-margin}" stroke="black"/>
<polyline points="{predicted}" fill="none" stroke="#d62728" stroke-width="3"/>
<polyline points="{measured}" fill="none" stroke="#1f77b4" stroke-width="1" stroke-dasharray="4 4"/>
{circles}
<text x="{width/2}" y="{height-12}" text-anchor="middle">Superficial velocity (m/s)</text>
<text x="18" y="{height/2}" text-anchor="middle" transform="rotate(-90 18 {height/2})">Pressure drop (Pa)</text>
<text x="{width/2}" y="28" text-anchor="middle" font-size="18">Porous obstruction pressure fit</text>
</svg>\n''', encoding="utf-8")


def calculate(args: argparse.Namespace) -> dict[str, object]:
    gross_area, length, zone_volume = geometry(args.size, args.direction)
    specified = sum(value is not None for value in (
        args.porosity_percent, args.open_area_m2,
        args.hole_count if args.hole_diameter_mm is not None else None))
    if (args.hole_count is None) != (args.hole_diameter_mm is None):
        raise ValueError("--hole-count and --hole-diameter-mm must be used together.")
    if specified > 1:
        raise ValueError("Specify only one porosity source.")
    porosity: float | None = args.porosity_percent
    open_area: float | None = None
    if args.open_area_m2 is not None:
        open_area = args.open_area_m2
        porosity = 100.0 * open_area / gross_area
    elif args.hole_count is not None:
        open_area = (args.hole_count * math.pi
                     * (args.hole_diameter_mm / 1000.0) ** 2 / 4.0)
        porosity = 100.0 * open_area / gross_area
    if porosity is not None and not 0.0 < porosity <= 100.0:
        raise ValueError("Calculated/specified porosity must be in (0,100].")

    solid_volume = None
    void_percent = None
    warnings: list[str] = []
    if args.cable:
        solid_volume, void_percent = cable_void_percent(args.cable, zone_volume)
        warnings.append(
            "Cable volume void percentage is not automatically flow-normal "
            "open-area porosity; pressure-drop calibration is preferred.")
        if args.use_cable_void_as_porosity:
            if porosity is not None:
                raise ValueError(
                    "Do not combine --use-cable-void-as-porosity with another "
                    "porosity source.")
            porosity = void_percent
            warnings.append(
                "TOML porosity was estimated from cable volume void fraction; "
                "treat the result as uncalibrated and sweep uncertainty.")

    points = [PressurePoint(*values) for values in args.pressure_point]
    points.extend(PressurePoint(flow / gross_area, pressure)
                  for flow, pressure in args.flow_pressure_point)
    if args.pressure_csv is not None:
        points.extend(read_pressure_csv(args.pressure_csv, gross_area))
    fit = None
    darcy = forchheimer = None
    rows: list[dict[str, float]] = []
    if points:
        fit = fit_pressure_points(points, length, args.viscosity, args.density)
        darcy = fit["darcy_coefficient_1_m2"]
        forchheimer = fit["forchheimer_coefficient_1_m"]
        rows = [{
            "velocity_m_s": point.velocity,
            "measured_pressure_pa": point.pressure,
            "predicted_pressure_pa": pressure_prediction(
                point.velocity, length, args.viscosity, args.density,
                darcy, forchheimer),
        } for point in sorted(points, key=lambda value: value.velocity)]
    elif porosity is not None and args.discharge_coefficient is not None:
        phi = porosity / 100.0
        forchheimer = 1.0 / (
            args.discharge_coefficient ** 2 * phi ** 2 * length)
    elif args.discharge_coefficient is not None:
        raise ValueError("--discharge-coefficient requires a porosity source.")

    snippet = toml_snippet(args, porosity, darcy,
                           fit["forchheimer_coefficient_1_m"] if fit else None)
    return {
        "name": args.name,
        "inputs": {
            "position_m": list(args.position),
            "size_m": list(args.size),
            "direction": args.direction,
            "air_density_kg_m3": args.density,
            "air_dynamic_viscosity_pa_s": args.viscosity,
            "specified_porosity_percent": args.porosity_percent,
            "specified_open_area_m2": args.open_area_m2,
            "hole_count": args.hole_count,
            "hole_diameter_mm": args.hole_diameter_mm,
            "discharge_coefficient": args.discharge_coefficient,
            "cables": [list(values) for values in args.cable],
            "used_cable_void_as_porosity": args.use_cable_void_as_porosity,
            "pressure_csv": (str(args.pressure_csv)
                             if args.pressure_csv is not None else None),
        },
        "gross_area_m2": gross_area,
        "physical_thickness_m": length,
        "zone_volume_m3": zone_volume,
        "open_area_m2": open_area,
        "porosity_percent": porosity,
        "cable_solid_volume_m3": solid_volume,
        "cable_void_percent": void_percent,
        "derived_forchheimer_coefficient_1_m": forchheimer if not fit else None,
        "pressure_fit": fit,
        "pressure_points": rows,
        "warnings": warnings,
        "toml": snippet,
    }


def write_outputs(result: dict[str, object], output: Path) -> list[Path]:
    output.parent.mkdir(parents=True, exist_ok=True)
    paths = {
        "json": output.with_suffix(".json"),
        "md": output.with_suffix(".md"),
        "toml": output.with_suffix(".toml"),
        "csv": output.with_suffix(".csv"),
        "svg": output.with_suffix(".svg"),
    }
    paths["json"].write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    paths["toml"].write_text(str(result["toml"]), encoding="utf-8")
    rows = result["pressure_points"]
    if rows:
        with paths["csv"].open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        write_svg(paths["svg"], rows)
    fit = result["pressure_fit"]
    lines = [f'# Porous obstruction calculation: {result["name"]}', "",
             f'- Gross area: {result["gross_area_m2"]:.8g} m²',
             f'- Physical thickness: {result["physical_thickness_m"]:.8g} m',
             f'- Zone volume: {result["zone_volume_m3"]:.8g} m³']
    if result["porosity_percent"] is not None:
        lines.append(f'- Porosity/open area: {result["porosity_percent"]:.6g}%')
    if result["cable_void_percent"] is not None:
        lines.append(f'- Cable volume void fraction: {result["cable_void_percent"]:.6g}%')
    if fit:
        lines.extend([
            f'- Darcy coefficient: {fit["darcy_coefficient_1_m2"]:.8g} 1/m²',
            f'- Forchheimer coefficient: {fit["forchheimer_coefficient_1_m"]:.8g} 1/m',
            f'- Fit RMSE: {fit["rmse_pa"]:.6g} Pa '
            f'({fit["normalized_rmse_percent"]:.4g}%)',
            f'- Maximum fit error: {fit["maximum_absolute_error_pa"]:.6g} Pa',
            f'- Fit R²: {fit["r_squared"]:.8g}'])
    if result["warnings"]:
        lines.extend(["", "## Warnings", ""])
        lines.extend(f'- {warning}' for warning in result["warnings"])
    lines.extend(["", "## Copy-ready TOML", "", "```toml",
                  str(result["toml"]).rstrip(), "```", ""])
    paths["md"].write_text("\n".join(lines), encoding="utf-8")
    return [path for path in paths.values() if path.exists()]


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--name", default="Quantified porous obstruction")
    result.add_argument("--position", type=triplet, default=(0.0, 0.0, 0.0),
                        metavar="X,Y,Z", help="lower corner in metres")
    result.add_argument("--size", type=triplet, required=True,
                        metavar="WIDTH,DEPTH,HEIGHT", help="zone size in metres")
    result.add_argument("--direction", choices=("x", "y", "z"), required=True)
    result.add_argument("--porosity-percent", type=float)
    result.add_argument("--open-area-m2", type=positive)
    result.add_argument("--hole-count", type=int)
    result.add_argument("--hole-diameter-mm", type=positive)
    result.add_argument("--discharge-coefficient", type=positive)
    result.add_argument("--cable", action="append", type=cable, default=[],
                        metavar="COUNT,DIAMETER_MM,LENGTH_M")
    result.add_argument("--use-cable-void-as-porosity", action="store_true")
    result.add_argument("--pressure-point", action="append", type=pair, default=[],
                        metavar="VELOCITY_M_S,PRESSURE_PA")
    result.add_argument("--flow-pressure-point", action="append", type=pair,
                        default=[], metavar="FLOW_M3_S,PRESSURE_PA")
    result.add_argument("--pressure-csv", type=Path,
                        help="CSV with pressure_pa and velocity_m_s or flow_m3_s")
    result.add_argument("--density", type=positive, default=1.225)
    result.add_argument("--viscosity", type=positive, default=1.81e-5)
    result.add_argument("--transverse-darcy-coefficient", type=positive)
    result.add_argument("--transverse-forchheimer-coefficient", type=positive)
    result.add_argument("--output", type=Path, default=Path("porous_obstruction"),
                        help="output basename")
    return result


def main() -> None:
    args = parser().parse_args()
    if args.hole_count is not None and args.hole_count <= 0:
        raise SystemExit("--hole-count must be positive.")
    if args.discharge_coefficient is not None and args.discharge_coefficient > 1:
        raise SystemExit("--discharge-coefficient must be in (0,1].")
    try:
        result = calculate(args)
        paths = write_outputs(result, args.output)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    print(result["toml"], end="")
    for warning in result["warnings"]:
        print(f"WARNING: {warning}")
    print("Saved: " + ", ".join(str(path) for path in paths))


if __name__ == "__main__":
    main()
