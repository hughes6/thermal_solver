#!/usr/bin/env python3
"""Audit prepared OpenFOAM solid masses and lumped heat capacities."""

from __future__ import annotations

import argparse
import re
from collections import defaultdict
from pathlib import Path

FLOAT_PATTERN = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?"


def _number(text: str, pattern: str, label: str) -> float:
    match = re.search(pattern, text, re.MULTILINE)
    if not match:
        raise ValueError(f"missing {label}")
    return float(match.group(1))


def mesh_volumes(log_text: str) -> dict[str, float]:
    result: dict[str, float] = {}
    for block in re.split(r"(?=Mesh stats )", log_text):
        name = re.search(r"^Mesh stats (.+)$", block, re.MULTILINE)
        volume = re.search(rf"Total volume = ({FLOAT_PATTERN})", block)
        if name and volume:
            result[name.group(1).strip()] = float(volume.group(1))
    return result


def heat_by_region(properties_text: str) -> dict[str, float]:
    result: dict[str, float] = defaultdict(float)
    for block in re.findall(r"\{([^{}]+)\}", properties_text, re.DOTALL):
        region = re.search(r"\bcomponentRegion\s+(\S+)\s*;", block)
        watts = re.search(rf"\bwatts\s+({FLOAT_PATTERN})\s*;", block)
        if region and watts:
            result[region.group(1)] += float(watts.group(1))
    return dict(result)


def audit_case(case: Path) -> list[dict[str, float | str]]:
    log_path = case / "checkMesh.prepare.log"
    properties_path = case / "constant" / "openfoamExportProperties"
    if not log_path.is_file():
        raise FileNotFoundError(f"missing prepared mesh report: {log_path}")
    if not properties_path.is_file():
        raise FileNotFoundError(f"missing export properties: {properties_path}")

    volumes = mesh_volumes(log_path.read_text(errors="replace"))
    watts = heat_by_region(properties_path.read_text(errors="replace"))
    rows: list[dict[str, float | str]] = []
    for thermo_path in sorted((case / "constant").glob("*/thermophysicalProperties")):
        region = thermo_path.parent.name
        if region == "fluid" or region not in volumes:
            continue
        text = thermo_path.read_text(errors="replace")
        rho = _number(text, rf"\brho\s+({FLOAT_PATTERN})\s*;", "rho")
        cp = _number(text, rf"\bCp\s+({FLOAT_PATTERN})\s*;", "Cp")
        volume = volumes[region]
        mass = rho * volume
        capacity = mass * cp
        heat = watts.get(region, 0.0)
        rows.append({
            "region": region,
            "volume_m3": volume,
            "rho_kg_m3": rho,
            "cp_j_kg_k": cp,
            "mass_kg": mass,
            "capacity_j_k": capacity,
            "watts": heat,
            "adiabatic_k_per_hour": 3600.0 * heat / capacity if capacity else 0.0,
        })
    return rows


def markdown_report(rows: list[dict[str, float | str]]) -> str:
    lines = [
        "# OpenFOAM thermal-mass audit",
        "",
        "| Region | Volume (m3) | Mass (kg) | Capacity (J/K) | Power (W) | Adiabatic rise (K/h) |",
        "| --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for row in rows:
        lines.append(
            f"| {row['region']} | {row['volume_m3']:.9g} | "
            f"{row['mass_kg']:.6g} | {row['capacity_j_k']:.6g} | "
            f"{row['watts']:.6g} | {row['adiabatic_k_per_hour']:.6g} |"
        )
    lines.extend([
        f"| **TOTAL** | **{sum(float(row['volume_m3']) for row in rows):.9g}** | "
        f"**{sum(float(row['mass_kg']) for row in rows):.6g}** | "
        f"**{sum(float(row['capacity_j_k']) for row in rows):.6g}** | "
        f"**{sum(float(row['watts']) for row in rows):.6g}** | n/a |",
        "",
    ])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path)
    parser.add_argument("--markdown", type=Path,
                        help="also write the audit as a Markdown table")
    args = parser.parse_args()
    rows = audit_case(args.case)
    if not rows:
        raise SystemExit("no prepared solid regions found")
    print("region|volume_m3|mass_kg|capacity_J_K|watts|adiabatic_K_h")
    for row in rows:
        print(
            f"{row['region']}|{row['volume_m3']:.9g}|{row['mass_kg']:.6g}|"
            f"{row['capacity_j_k']:.6g}|{row['watts']:.6g}|"
            f"{row['adiabatic_k_per_hour']:.6g}"
        )
    print(
        "TOTAL|"
        f"{sum(float(row['volume_m3']) for row in rows):.9g}|"
        f"{sum(float(row['mass_kg']) for row in rows):.6g}|"
        f"{sum(float(row['capacity_j_k']) for row in rows):.6g}|"
        f"{sum(float(row['watts']) for row in rows):.6g}|"
        "n/a"
    )
    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text(markdown_report(rows), encoding="utf-8")
        print(f"Wrote Markdown audit: {args.markdown}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
