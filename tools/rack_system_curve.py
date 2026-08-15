"""Build a rack static-pressure/system-resistance curve for fan analysis.

OpenFOAM case points are inferred from the actual solved flow through every
``fanPressure`` patch and that patch's exported, density-corrected pressure
table. This gives the static pressure supplied by the parallel fan bank at the
CFD operating point without relying on arbitrary cell pressure extrema.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from plot.run_metadata import resolve_case
from tools.validate_openfoam_case import latest_result_paths, patch_values


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


@dataclass
class SystemPoint:
    source: str
    time_s: float | None
    flow_m3_s: float
    pressure_pa: float
    fan_count: int | None = None
    flow_spread_percent: float | None = None


def parse_point(text: str) -> tuple[float, float]:
    try:
        flow, pressure = text.split(",", 1)
        return float(flow), float(pressure)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "System points must be FLOW,PRESSURE, for example 1500,80"
        ) from exc


def named_dictionary(text: str, name: str) -> str:
    """Return one OpenFOAM named dictionary using balanced braces."""
    match = re.search(rf"(?m)^\s*{re.escape(name)}\s*\n\s*\{{", text)
    if not match:
        raise ValueError(f"OpenFOAM patch {name!r} was not found")
    start = text.find("{", match.start())
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1:index]
    raise ValueError(f"OpenFOAM patch {name!r} has an unterminated dictionary")


def fan_pressure_tables(path: Path) -> dict[str, list[tuple[float, float]]]:
    text = path.read_text(encoding="ascii", errors="replace")
    boundary = text[text.find("boundaryField"):]
    names = re.findall(
        r"(?m)^\s*([^\s{}]+)\s*\n\s*\{\s*\n\s*type\s+fanPressure\s*;",
        boundary,
    )
    result = {}
    for name in names:
        block = named_dictionary(boundary, name)
        table = re.search(
            r"\bvalues\s*\(\s*((?:\([^()]+\)\s*)+)\)\s*;", block,
            re.DOTALL,
        )
        if not table:
            raise ValueError(f"No pressure table found for fan patch {name}")
        points = [
            (float(flow), float(pressure))
            for flow, pressure in re.findall(
                r"\(\s*([\d.eE+-]+)\s+([\d.eE+-]+)\s*\)", table.group(1)
            )
        ]
        if len(points) < 2:
            raise ValueError(f"Fan patch {name} has fewer than two table points")
        result[name] = sorted(points)
    if not result:
        raise ValueError(f"No fanPressure patches found in {path}")
    return result


def interpolate_table(points: list[tuple[float, float]], flow: float) -> float:
    if flow <= points[0][0]:
        return points[0][1]
    if flow >= points[-1][0]:
        return points[-1][1]
    for (q0, p0), (q1, p1) in zip(points, points[1:]):
        if flow <= q1:
            fraction = (flow - q0) / (q1 - q0)
            return p0 + fraction * (p1 - p0)
    raise AssertionError("unreachable fan-table interpolation state")


def expand(values: list[float], count: int, field: str, patch: str) -> list[float]:
    if len(values) == 1:
        return values * count
    if len(values) != count:
        raise ValueError(
            f"Patch {patch} has {count} phi values but {len(values)} {field} values"
        )
    return values


def case_system_point(case: Path) -> SystemPoint:
    case = case.expanduser().resolve()
    table_path = case / "0" / "fluid" / "p_rgh"
    tables = fan_pressure_tables(table_path)
    time_s, result_paths = latest_result_paths(case)
    fan_results = []
    for patch, table in tables.items():
        mass_fluxes = []
        densities = []
        for result in result_paths:
            fluid = result / "fluid"
            rank_fluxes = patch_values(fluid / "phi", patch)
            rank_densities = expand(
                patch_values(fluid / "rho", patch), len(rank_fluxes), "rho", patch
            )
            mass_fluxes.extend(rank_fluxes)
            densities.extend(rank_densities)
        if any(density <= 0 or not math.isfinite(density) for density in densities):
            raise ValueError(f"Patch {patch} contains nonpositive/nonfinite density")
        flow = sum(phi / density for phi, density in zip(mass_fluxes, densities))
        if flow <= 0:
            raise ValueError(
                f"Fan patch {patch} has non-outward volumetric flow {flow:.9g} m3/s"
            )
        pressure = interpolate_table(table, flow)
        fan_results.append((patch, flow, pressure))
    total_flow = sum(flow for _, flow, _ in fan_results)
    if total_flow <= 0:
        raise ValueError(f"No positive fan flow was found in {case}")
    # Air-power weighting gives the equivalent pressure of parallel fans when
    # local inlet conditions make their individual operating points unequal.
    equivalent_pressure = sum(
        flow * pressure for _, flow, pressure in fan_results
    ) / total_flow
    mean_flow = total_flow / len(fan_results)
    spread = 100.0 * (
        max(flow for _, flow, _ in fan_results)
        - min(flow for _, flow, _ in fan_results)
    ) / mean_flow
    print(f"\n{case}")
    print(f"  checkpoint: {time_s:g} s")
    for patch, flow, pressure in fan_results:
        print(
            f"  {patch}: {flow:.8g} m3/s "
            f"({flow / FLOW_TO_M3S['cfm']:.3f} CFM), {pressure:.6g} Pa"
        )
    print(
        f"  rack point: {total_flow:.8g} m3/s "
        f"({total_flow / FLOW_TO_M3S['cfm']:.3f} CFM), "
        f"{equivalent_pressure:.6g} Pa"
    )
    return SystemPoint(
        source=str(case), time_s=time_s, flow_m3_s=total_flow,
        pressure_pa=equivalent_pressure, fan_count=len(fan_results),
        flow_spread_percent=spread,
    )


def fit_system_curve(points: list[SystemPoint]) -> tuple[float, float, str]:
    """Fit dP = b*Q + c*Q^2 through the physical zero-flow origin."""
    valid = [point for point in points if point.flow_m3_s > 0]
    if not valid:
        raise ValueError("At least one positive-flow system point is required")
    if any(point.pressure_pa < 0 for point in valid):
        raise ValueError("Rack system pressure must be nonnegative")
    if len(valid) == 1:
        q = valid[0].flow_m3_s
        return 0.0, valid[0].pressure_pa / (q * q), "single-point quadratic estimate"
    s2 = sum(point.flow_m3_s ** 2 for point in valid)
    s3 = sum(point.flow_m3_s ** 3 for point in valid)
    s4 = sum(point.flow_m3_s ** 4 for point in valid)
    y1 = sum(point.flow_m3_s * point.pressure_pa for point in valid)
    y2 = sum(point.flow_m3_s ** 2 * point.pressure_pa for point in valid)
    determinant = s2 * s4 - s3 * s3
    if abs(determinant) <= 1.0e-20 * max(1.0, s2 * s4):
        # Repeated/near-repeated flow points cannot distinguish linear and
        # quadratic terms; retain the physically useful Q^2 estimate.
        c = sum(point.pressure_pa * point.flow_m3_s ** 2 for point in valid) / s4
        return 0.0, c, "quadratic estimate (insufficient distinct flows for B+C fit)"
    b = (y1 * s4 - y2 * s3) / determinant
    c = (s2 * y2 - s3 * y1) / determinant
    return b, c, "least-squares B*Q+C*Q^2 fit"


def load_fan_curve(path: Path, name: str) -> tuple[float, float, float, float]:
    import tomllib

    data = tomllib.loads(path.read_text(encoding="utf-8"))
    matches = [curve for curve in data.get("fan_curve", []) if curve.get("name") == name]
    if len(matches) != 1:
        raise ValueError(f"Expected exactly one fan curve named {name!r} in {path}")
    curve = matches[0]
    return tuple(float(curve[key]) for key in ("a", "b", "c", "rho_rated"))


def bank_pressure(flow: float, curve, count: int, density: float | None) -> float:
    a, b, c, rho_rated = curve
    scale = 1.0 if density is None else density / rho_rated
    per_fan = flow / count
    return max(0.0, scale * (a - b * per_fan - c * per_fan * per_fan))


def operating_point(system_b: float, system_c: float, curve, count: int,
                    density: float | None) -> tuple[float, float]:
    def residual(flow):
        return bank_pressure(flow, curve, count, density) - (
            system_b * flow + system_c * flow * flow
        )
    a, b, c, _ = curve
    if a <= 0 or (b <= 0 and c <= 0):
        raise ValueError(
            "Fan curve must have positive shutoff pressure and a decreasing slope"
        )
    single_free = ((-b + math.sqrt(b * b + 4 * c * a)) / (2 * c)
                   if c > 0 else a / b)
    low, high = 0.0, count * single_free
    for _ in range(100):
        middle = (low + high) / 2
        if residual(middle) > 0:
            low = middle
        else:
            high = middle
    flow = (low + high) / 2
    return flow, system_b * flow + system_c * flow * flow


def write_csv(path: Path, points: list[SystemPoint]) -> None:
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=SystemPoint.__dataclass_fields__)
        writer.writeheader()
        writer.writerows(asdict(point) for point in points)


def write_svg(path: Path, points: list[SystemPoint], b: float, c: float,
              fan_curve=None, fan_count=1, density=None,
              operating=None) -> None:
    width, height = 900, 570
    left, right, top, bottom = 90, 35, 55, 75
    plot_w, plot_h = width - left - right, height - top - bottom
    max_q = max(point.flow_m3_s for point in points) * 1.25
    if operating:
        max_q = max(max_q, operating[0] * 1.2)
    samples = [max_q * index / 250 for index in range(251)]
    system = [(q, b * q + c * q * q) for q in samples]
    fan = ([(q, bank_pressure(q, fan_curve, fan_count, density)) for q in samples]
           if fan_curve else [])
    max_p = max([pressure for _, pressure in system + fan] +
                [point.pressure_pa for point in points] + [1.0]) * 1.1
    x = lambda q: left + plot_w * q / max_q
    y = lambda p: top + plot_h * (1 - p / max_p)
    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        '<style>text{font-family:Arial,sans-serif;fill:#222}.grid{stroke:#ddd}'
        '.axis{stroke:#222;stroke-width:1.5}.system{fill:none;stroke:#1769aa;stroke-width:3}'
        '.fan{fill:none;stroke:#d84315;stroke-width:3}.point{fill:#1769aa;stroke:white;stroke-width:2}'
        '.op{fill:#2e7d32;stroke:white;stroke-width:2}</style>',
        f'<text x="{width/2}" y="30" text-anchor="middle" font-size="21">Rack system static-pressure curve</text>',
    ]
    for index in range(6):
        fraction = index / 5
        px, py = left + plot_w * fraction, top + plot_h * (1 - fraction)
        lines += [
            f'<line class="grid" x1="{px}" y1="{top}" x2="{px}" y2="{top+plot_h}"/>',
            f'<text x="{px}" y="{top+plot_h+24}" text-anchor="middle" font-size="12">{max_q*fraction/FLOW_TO_M3S["cfm"]:.0f}</text>',
            f'<line class="grid" x1="{left}" y1="{py}" x2="{left+plot_w}" y2="{py}"/>',
            f'<text x="{left-10}" y="{py+4}" text-anchor="end" font-size="12">{max_p*fraction:.0f}</text>',
        ]
    lines += [
        f'<line class="axis" x1="{left}" y1="{top+plot_h}" x2="{left+plot_w}" y2="{top+plot_h}"/>',
        f'<line class="axis" x1="{left}" y1="{top}" x2="{left}" y2="{top+plot_h}"/>',
        f'<text x="{left+plot_w/2}" y="{height-18}" text-anchor="middle" font-size="15">Total rack flow (CFM)</text>',
        f'<text x="20" y="{top+plot_h/2}" text-anchor="middle" font-size="15" transform="rotate(-90 20 {top+plot_h/2})">Static pressure (Pa)</text>',
        '<polyline class="system" points="' + ' '.join(f'{x(q):.2f},{y(p):.2f}' for q, p in system) + '"/>',
    ]
    if fan:
        lines.append('<polyline class="fan" points="' + ' '.join(
            f'{x(q):.2f},{y(p):.2f}' for q, p in fan) + '"/>')
    for point in points:
        lines.append(f'<circle class="point" cx="{x(point.flow_m3_s):.2f}" cy="{y(point.pressure_pa):.2f}" r="6"/>')
    if operating:
        lines.append(f'<circle class="op" cx="{x(operating[0]):.2f}" cy="{y(operating[1]):.2f}" r="7"/>')
    lines += [
        f'<text x="{left+15}" y="{top+20}" font-size="13" fill="#1769aa">Rack system curve</text>',
        (f'<text x="{left+15}" y="{top+40}" font-size="13" fill="#d84315">{fan_count}-fan parallel curve</text>' if fan else ''),
        '</svg>',
    ]
    path.write_text("\n".join(lines), encoding="utf-8")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", type=Path, action="append", default=[],
                        help="completed OpenFOAM sweep case; repeat for each point")
    parser.add_argument("--point", type=parse_point, action="append", default=[],
                        help="manual FLOW,PRESSURE point; repeat as needed")
    parser.add_argument("--csv", type=Path,
                        help="CSV with flow and pressure columns")
    parser.add_argument("--flow-unit", choices=FLOW_TO_M3S, default="cfm")
    parser.add_argument("--pressure-unit", choices=PRESSURE_TO_PA, default="pa")
    parser.add_argument("--fan-library", type=Path,
                        default=Path("library/fan_curves/fan_curves.toml"))
    parser.add_argument("--fan-curve", help="fan curve to overlay")
    parser.add_argument("--fan-count", type=int, default=1,
                        help="identical fans in parallel")
    parser.add_argument("--operating-density", type=float,
                        help="air density for fan-curve scaling, kg/m3")
    parser.add_argument("--output", type=Path, default=Path("rack_system_curve"),
                        help="output basename for CSV, JSON, and SVG")
    return parser


def main() -> None:
    args = build_parser().parse_args()
    if args.fan_count < 1:
        raise SystemExit("--fan-count must be at least one")
    if args.operating_density is not None and args.operating_density <= 0:
        raise SystemExit("--operating-density must be positive")
    cases = list(args.case)
    if not cases and not args.point and not args.csv:
        try:
            cases = [resolve_case(None)]
        except (FileNotFoundError, ValueError) as exc:
            raise SystemExit(str(exc)) from exc
    points = []
    for case in cases:
        try:
            points.append(case_system_point(case))
        except (FileNotFoundError, ValueError) as exc:
            raise SystemExit(f"Could not extract rack point from {case}: {exc}") from exc
    for flow, pressure in args.point:
        points.append(SystemPoint(
            source="manual", time_s=None,
            flow_m3_s=flow * FLOW_TO_M3S[args.flow_unit],
            pressure_pa=pressure * PRESSURE_TO_PA[args.pressure_unit],
        ))
    if args.csv:
        with args.csv.open(newline="", encoding="utf-8-sig") as stream:
            rows = list(csv.DictReader(stream))
        if not rows or not {"flow", "pressure"} <= set(rows[0]):
            raise SystemExit("System-point CSV must contain flow and pressure columns")
        for row in rows:
            points.append(SystemPoint(
                source=str(args.csv), time_s=None,
                flow_m3_s=float(row["flow"]) * FLOW_TO_M3S[args.flow_unit],
                pressure_pa=float(row["pressure"]) * PRESSURE_TO_PA[args.pressure_unit],
            ))
    try:
        b, c, method = fit_system_curve(points)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    fan_curve = None
    operating = None
    if args.fan_curve:
        try:
            fan_curve = load_fan_curve(args.fan_library, args.fan_curve)
            operating = operating_point(
                b, c, fan_curve, args.fan_count, args.operating_density
            )
        except (OSError, ValueError) as exc:
            raise SystemExit(str(exc)) from exc

    base = args.output.with_suffix("")
    base.parent.mkdir(parents=True, exist_ok=True)
    csv_path, json_path, svg_path = (
        base.with_suffix(".csv"), base.with_suffix(".json"), base.with_suffix(".svg")
    )
    write_csv(csv_path, points)
    report = {
        "equation": "delta_p_pa = B*Q_m3_s + C*Q_m3_s^2",
        "fit_method": method,
        "B_pa_per_m3_s": b,
        "C_pa_per_m3_s2": c,
        "points": [asdict(point) for point in points],
    }
    if operating:
        report["fan_analysis"] = {
            "curve": args.fan_curve,
            "parallel_fan_count": args.fan_count,
            "operating_density_kg_m3": args.operating_density,
            "operating_flow_m3_s": operating[0],
            "operating_flow_cfm": operating[0] / FLOW_TO_M3S["cfm"],
            "operating_pressure_pa": operating[1],
            "per_fan_flow_cfm": operating[0] / args.fan_count / FLOW_TO_M3S["cfm"],
        }
    json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    write_svg(svg_path, points, b, c, fan_curve, args.fan_count,
              args.operating_density, operating)

    print("\nRack system curve")
    print(f"  method: {method}")
    print(f"  deltaP [Pa] = ({b:.9g}) Q + ({c:.9g}) Q^2, Q in m3/s")
    if len(points) == 1:
        print("  WARNING: one CFD point only estimates a Q^2 curve; run a flow sweep for validation.")
    if operating:
        print("\nParallel-fan operating intersection")
        print(f"  total flow: {operating[0]:.8g} m3/s ({operating[0]/FLOW_TO_M3S['cfm']:.3f} CFM)")
        print(f"  per fan:    {operating[0]/args.fan_count/FLOW_TO_M3S['cfm']:.3f} CFM")
        print(f"  pressure:   {operating[1]:.6g} Pa")
    print(f"\nSaved: {csv_path.resolve()}")
    print(f"Saved: {json_path.resolve()}")
    print(f"Saved: {svg_path.resolve()}")


if __name__ == "__main__":
    main()
