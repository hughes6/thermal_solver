#!/usr/bin/env python3
"""Audit OpenFOAM fan flows against their exported active and assisted domains.

Direction-only checks can pass even when a fan is being advected above the
first zero-pressure point in its supplied curve.  A legacy zero-pressure
plateau means such a fan is no longer adding pressure and is unsupported.  A
newer exported signed continuation is instead an explicit assisted-flow
branch: it is reported as a warning, while flows beyond the supplied table
remain a hard failure.

This audit reads the curves actually exported into ``fvOptions`` and
``0/fluid/p_rgh``.  It therefore checks the case that ran, rather than trying
to reconstruct fan identity from the source TOML files.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass, asdict
import math
from pathlib import Path
import re

try:  # Package import used by the test suite.
    from tools.openfoam_boundary_mass_balance_audit import opening_observations
    from tools.openfoam_fan_flow_audit import audit as audit_internal_flows
except ModuleNotFoundError:  # Direct ``python tools/...py`` invocation.
    from openfoam_boundary_mass_balance_audit import opening_observations
    from openfoam_fan_flow_audit import audit as audit_internal_flows


FLOAT = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
BLOCK_START_RE = re.compile(
    r"(?m)^\s*([A-Za-z_][A-Za-z0-9_]*)\s*\n\s*\{"
)
TABLE_POINT_RE = re.compile(
    rf"\(\s*({FLOAT})\s+({FLOAT})\s*\)"
)


@dataclass(frozen=True)
class FanCurve:
    name: str
    direction: str
    points: tuple[tuple[float, float], ...]
    source: str


@dataclass(frozen=True)
class FanOperatingPoint:
    scope: str
    fan_name: str
    time_s: float
    flow_m3_s: float | None
    flow_kg_s: float | None
    positive_pressure_limit_m3_s: float | None
    curve_domain_limit_m3_s: float | None
    utilization_fraction: float | None
    curve_domain_utilization_fraction: float | None
    status: str
    detail: str


def _matching_brace(text: str, opening: int) -> int:
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    raise ValueError("unterminated OpenFOAM dictionary block")


def named_blocks(text: str):
    """Yield named blocks, including useful nested blocks."""
    for match in BLOCK_START_RE.finditer(text):
        opening = text.find("{", match.start(), match.end())
        closing = _matching_brace(text, opening)
        yield match.group(1), text[opening + 1:closing]


def _top_level_only(block: str) -> str:
    """Mask nested dictionaries while retaining top-level line structure."""
    depth = 0
    characters = []
    for character in block:
        if character == "{":
            depth += 1
            characters.append(" ")
        elif character == "}":
            depth -= 1
            if depth < 0:
                raise ValueError("unbalanced nested OpenFOAM dictionary block")
            characters.append(" ")
        elif depth and character != "\n":
            characters.append(" ")
        else:
            characters.append(character)
    if depth:
        raise ValueError("unterminated nested OpenFOAM dictionary block")
    return "".join(characters)


def _fan_table(block: str, source: Path, fan_name: str):
    match = re.search(
        r"\bfanCurve\s*\{.*?\bvalues\s*\((.*?)\)\s*;",
        block,
        flags=re.DOTALL,
    )
    if not match:
        raise ValueError(f"No fanCurve table found for {fan_name} in {source}")
    points = tuple(
        (float(point.group(1)), float(point.group(2)))
        for point in TABLE_POINT_RE.finditer(match.group(1))
    )
    if len(points) < 2:
        raise ValueError(f"Fan curve for {fan_name} in {source} has <2 points")
    previous_q = -math.inf
    for q_value, pressure in points:
        if not math.isfinite(q_value) or not math.isfinite(pressure):
            raise ValueError(f"Nonfinite fan curve value for {fan_name}")
        if q_value <= previous_q:
            raise ValueError(f"Non-increasing fan curve flow for {fan_name}")
        previous_q = q_value
    return points


def read_exported_curves(path: Path, kind: str) -> dict[str, FanCurve]:
    """Read fan curves from an exported ``fvOptions`` or ``p_rgh`` file."""
    if kind not in {"internal", "boundary"}:
        raise ValueError("kind must be 'internal' or 'boundary'")
    required_type = "fanMomentumSource" if kind == "internal" else "fanPressure"
    result: dict[str, FanCurve] = {}
    text = path.read_text(encoding="utf-8", errors="replace")
    for name, block in named_blocks(text):
        top_level = _top_level_only(block)
        if not re.search(rf"\btype\s+{required_type}\s*;", top_level):
            continue
        if name in result:
            raise ValueError(f"Duplicate exported fan block {name} in {path}")
        if kind == "boundary":
            direction_match = re.search(
                r"\bdirection\s+(in|out)\s*;", top_level
            )
            if not direction_match:
                raise ValueError(f"No direction in boundary fan block {name}")
            direction = direction_match.group(1)
        else:
            direction = "forward"
        result[name] = FanCurve(
            name=name,
            direction=direction,
            points=_fan_table(block, path, name),
            source=str(path),
        )
    if not result:
        raise ValueError(f"No {kind} fan curves found in {path}")
    return result


def positive_pressure_limit(points: tuple[tuple[float, float], ...]) -> float | None:
    """Return the first positive-to-nonpositive pressure crossing."""
    previous_q, previous_p = points[0]
    for q_value, pressure in points[1:]:
        if previous_p > 0.0 and pressure <= 0.0:
            if pressure == 0.0:
                return q_value
            fraction = previous_p / (previous_p - pressure)
            return previous_q + fraction * (q_value - previous_q)
        previous_q, previous_p = q_value, pressure
    return None


def assisted_flow_limit(
    points: tuple[tuple[float, float], ...]
) -> float | None:
    """Return the terminal table flow when it contains a signed passive branch."""
    zero = positive_pressure_limit(points)
    if zero is None:
        return None
    scale = max(1.0, *(abs(pressure) for _, pressure in points))
    tolerance = 1.0e-12 * scale
    has_signed_branch = any(
        q_value > zero and pressure < -tolerance
        for q_value, pressure in points
    )
    if not has_signed_branch or points[-1][1] >= -tolerance:
        return None
    return points[-1][0]


def classify(
    scope: str,
    name: str,
    time_s: float,
    flow_m3_s: float | None,
    flow_kg_s: float | None,
    curve: FanCurve,
    warning_fraction: float,
    direction_ok: bool | None = True,
) -> FanOperatingPoint:
    limit = positive_pressure_limit(curve.points)
    curve_limit = assisted_flow_limit(curve.points)
    if flow_m3_s is None:
        return FanOperatingPoint(
            scope, name, time_s, None, flow_kg_s, limit, curve_limit,
            None, None,
            "FAIL_MISSING", "no runtime flow measurement at the audited time",
        )
    if not math.isfinite(flow_m3_s):
        return FanOperatingPoint(
            scope, name, time_s, flow_m3_s, flow_kg_s, limit, curve_limit,
            None, None,
            "FAIL_NONFINITE", "runtime flow is not finite",
        )
    if direction_ok is False or flow_m3_s <= 0.0:
        return FanOperatingPoint(
            scope, name, time_s, flow_m3_s, flow_kg_s, limit, curve_limit,
            None, None,
            "FAIL_DIRECTION", "runtime flow is stagnant or opposite the fan direction",
        )
    if limit is None or not math.isfinite(limit) or limit <= 0.0:
        return FanOperatingPoint(
            scope, name, time_s, flow_m3_s, flow_kg_s, limit, curve_limit,
            None, None,
            "FAIL_NO_LIMIT", "exported curve has no positive-to-zero pressure crossing",
        )
    utilization = flow_m3_s / limit
    curve_utilization = (
        flow_m3_s / curve_limit if curve_limit is not None else None
    )
    if utilization >= 1.0:
        if curve_limit is None:
            status = "FAIL_OUTSIDE_CURVE"
            detail = (
                "flow is at or beyond the first zero-pressure point and "
                "the exported curve has no signed assisted-flow branch"
            )
        elif flow_m3_s > curve_limit:
            status = "FAIL_OUTSIDE_CURVE"
            detail = "flow exceeds the exported signed curve domain"
        else:
            status = "WARN_ASSISTED_FLOW"
            detail = "flow is on the exported signed assisted-flow branch"
    elif utilization >= warning_fraction:
        status = "WARN_NEAR_LIMIT"
        detail = "flow is close to the first zero-pressure point"
    else:
        status = "PASS"
        detail = "flow is inside the positive-pressure curve domain"
    return FanOperatingPoint(
        scope, name, time_s, flow_m3_s, flow_kg_s, limit, curve_limit,
        utilization, curve_utilization,
        status, detail,
    )


def audit_internal(
    case: Path,
    warning_fraction: float,
) -> list[FanOperatingPoint]:
    curves = read_exported_curves(
        case / "constant" / "fluid" / "fvOptions", "internal"
    )
    times, fan_names, records = audit_internal_flows(case)
    latest_time = times[-1]
    flows = records[latest_time]
    names = list(curves)
    names.extend(sorted(name for name in fan_names if name not in curves))
    rows = []
    for name in names:
        curve = curves.get(name)
        if curve is None:
            rows.append(FanOperatingPoint(
                "internal", name, latest_time, flows.get(name), None,
                None, None, None, None, "FAIL_MISSING_CURVE",
                "runtime fan has no exported fanMomentumSource curve",
            ))
            continue
        rows.append(classify(
            "internal", name, latest_time, flows.get(name), None,
            curve, warning_fraction,
        ))
    return rows


def audit_boundary(
    case: Path,
    density_kg_m3: float,
    warning_fraction: float,
    expected_time: float | None = None,
) -> list[FanOperatingPoint]:
    if not math.isfinite(density_kg_m3) or density_kg_m3 <= 0.0:
        raise ValueError("density must be positive and finite")
    curves = read_exported_curves(case / "0" / "fluid" / "p_rgh", "boundary")
    observations = opening_observations(case)
    latest_time = (
        expected_time if expected_time is not None
        else max(
            (float(row["time_s"]) for row in observations),
            default=math.nan,
        )
    )
    latest: dict[str, float] = {}
    if math.isfinite(latest_time):
        tolerance = 1.0e-12 * max(1.0, abs(latest_time))
        for row in observations:
            if abs(float(row["time_s"]) - latest_time) > tolerance:
                continue
            name = str(row["opening"])
            if name in latest:
                raise ValueError(
                    f"Duplicate boundary flow measurement for {name} at t={latest_time:g}"
                )
            latest[name] = float(row["phi_kg_s"])
    rows = []
    for name, curve in curves.items():
        phi = latest.get(name)
        if phi is None:
            flow = None
            direction_ok = None
        else:
            flow = abs(phi) / density_kg_m3
            direction_ok = phi < 0.0 if curve.direction == "in" else phi > 0.0
        rows.append(classify(
            "boundary", name, latest_time, flow, phi,
            curve, warning_fraction, direction_ok,
        ))
    return rows


def audit_case(
    case: Path,
    density_kg_m3: float,
    warning_fraction: float = 0.9,
) -> list[FanOperatingPoint]:
    if not 0.0 < warning_fraction < 1.0:
        raise ValueError("warning_fraction must be between zero and one")
    internal = audit_internal(case, warning_fraction)
    checkpoint_time = internal[0].time_s
    return internal + audit_boundary(
        case, density_kg_m3, warning_fraction, expected_time=checkpoint_time
    )


def write_csv(path: Path, rows: list[FanOperatingPoint]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(asdict(rows[0])))
        writer.writeheader()
        writer.writerows(asdict(row) for row in rows)


def markdown(rows: list[FanOperatingPoint]) -> str:
    lines = [
        "| Scope | Fan | Time (s) | Flow (m³/s) | Free-delivery limit (m³/s) | Signed-branch limit (m³/s) | Utilization | Curve utilization | Status |",
        "|---|---|---:|---:|---:|---:|---:|---:|---|",
    ]
    for row in rows:
        flow = "—" if row.flow_m3_s is None else f"{row.flow_m3_s:.7g}"
        limit = (
            "—" if row.positive_pressure_limit_m3_s is None
            else f"{row.positive_pressure_limit_m3_s:.7g}"
        )
        utilization = (
            "—" if row.utilization_fraction is None
            else f"{100.0 * row.utilization_fraction:.2f}%"
        )
        curve_limit = (
            "—" if row.curve_domain_limit_m3_s is None
            else f"{row.curve_domain_limit_m3_s:.7g}"
        )
        curve_utilization = (
            "—" if row.curve_domain_utilization_fraction is None
            else f"{100.0 * row.curve_domain_utilization_fraction:.2f}%"
        )
        time = "—" if not math.isfinite(row.time_s) else f"{row.time_s:g}"
        lines.append(
            f"| {row.scope} | `{row.fan_name}` | {time} | {flow} | "
            f"{limit} | {curve_limit} | {utilization} | "
            f"{curve_utilization} | {row.status} |"
        )
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path)
    parser.add_argument("--density", type=float, required=True,
                        help="ambient density used to convert boundary mass flow")
    parser.add_argument("--warning-fraction", type=float, default=0.9)
    parser.add_argument("--csv", type=Path, help="write machine-readable results")
    parser.add_argument("--markdown", type=Path, help="write a Markdown table")
    parser.add_argument("--fail-on-warning", action="store_true")
    args = parser.parse_args(argv)

    rows = audit_case(args.case, args.density, args.warning_fraction)
    if args.csv:
        write_csv(args.csv, rows)
        print(f"Wrote {args.csv}")
    report = markdown(rows)
    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text(report, encoding="utf-8")
        print(f"Wrote {args.markdown}")
    else:
        print(report, end="")

    counts: dict[str, int] = {}
    for row in rows:
        counts[row.status] = counts.get(row.status, 0) + 1
    print(
        f"Audited {len(rows)} fans: "
        + ", ".join(f"{key}={counts[key]}" for key in sorted(counts))
    )
    failed = any(row.status.startswith("FAIL") for row in rows)
    warned = any(row.status.startswith("WARN") for row in rows)
    return 1 if failed or (args.fail_on_warning and warned) else 0


if __name__ == "__main__":
    raise SystemExit(main())
