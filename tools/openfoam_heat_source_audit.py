#!/usr/bin/env python3
"""Audit exported OpenFOAM heat zones and the sources that act on them."""

from __future__ import annotations

import argparse
import json
import re
import struct
from dataclasses import asdict, dataclass
from pathlib import Path

try:
    from tools.openfoam_component_report import heat_sources
except ModuleNotFoundError:
    from openfoam_component_report import heat_sources


@dataclass(frozen=True)
class AuditRow:
    case: str
    source: str
    component_region: str
    solver_region: str
    cells: int
    selected_volume_m3: float
    configured_power_w: float
    active_power_w: float
    status: str


def power_summary(rows: list[AuditRow]) -> dict[str, float | int]:
    fluid_rows = [row for row in rows if row.solver_region == "fluid"]
    solid_rows = [row for row in rows if row.solver_region != "fluid"]
    return {
        "total_sources": len(rows),
        "fluid_sources": len(fluid_rows),
        "solid_sources": len(solid_rows),
        "total_power_w": sum(row.active_power_w for row in rows),
        "fluid_power_w": sum(row.active_power_w for row in fluid_rows),
        "solid_power_w": sum(row.active_power_w for row in solid_rows),
    }


def dictionary_block(text: str, name: str) -> str:
    match = re.search(rf"(?m)^\s*{re.escape(name)}_energy\s*\{{", text)
    if not match:
        raise ValueError(f"fvOptions lacks {name}_energy")
    start = text.find("{", match.start())
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1:index]
    raise ValueError(f"fvOptions block {name}_energy is unterminated")


def entry(block: str, key: str) -> str:
    match = re.search(rf"\b{re.escape(key)}\s+([^;]+);", block)
    if not match:
        raise ValueError(f"Source block lacks {key}")
    return match.group(1).strip()


def active_power(block: str) -> float:
    match = re.search(
        r"\bsources\s*\{\s*h\s*\(\s*([-+\d.eE]+)\s+[-+\d.eE]+\s*\)\s*;?\s*\}",
        block, re.DOTALL)
    if not match:
        raise ValueError("Source block lacks a scalar h source")
    return float(match.group(1))


def cell_set(path: Path) -> list[int]:
    data = path.read_bytes()
    match = re.search(rb"\n\s*(\d+)\s*\n\s*\(\s*\n", data)
    if not match:
        raise ValueError(f"Cannot parse heat-source cell set {path}")
    expected = int(match.group(1).decode("ascii"))
    header = data[:match.start()].decode("ascii", errors="replace")
    payload_start = data[match.end():]
    # topoSet can preserve an ASCII label list while writing a binary-format
    # header, so inspect the payload instead of trusting only the header.
    ascii_payload = bool(re.match(rb"[-+]?\d", payload_start))
    if re.search(r"\bformat\s+binary\s*;", header) and not ascii_payload:
        width_match = re.search(r"\blabel=(32|64)\b", header)
        width = int(width_match.group(1)) if width_match else 32
        byte_count = expected * (width // 8)
        payload = data[match.end():match.end() + byte_count]
        if len(payload) != byte_count:
            raise ValueError(f"Binary cell set {path.name} is truncated")
        code = "i" if width == 32 else "q"
        labels = list(struct.unpack(f"<{expected}{code}", payload))
    else:
        closing = data.find(b")", match.end())
        if closing < 0:
            raise ValueError(f"ASCII cell set {path.name} is unterminated")
        labels = [int(value) for value in re.findall(
            rb"\d+", data[match.end():closing])]
    if len(labels) != expected:
        raise ValueError(
            f"{path.name} declares {expected} cells but contains {len(labels)}")
    if len(set(labels)) != len(labels):
        raise ValueError(f"{path.name} contains duplicate cell labels")
    if not labels:
        raise ValueError(f"{path.name} is empty")
    return labels


def analyze_case(case: Path) -> list[AuditRow]:
    case = case.resolve()
    sources = heat_sources(case)
    if not sources:
        raise ValueError(f"No exported heat sources in {case}")
    options_by_region: dict[str, str] = {}
    occupied: dict[str, dict[int, str]] = {}
    rows = []
    for source in sources:
        # Export writes solid source dictionaries into their future component
        # directories, but keeps all cell sets in the aggregate polyMesh until
        # splitMeshRegions runs.  Preparation moves each set into the matching
        # component mesh while intentionally preserving the export metadata.
        option_candidates = (
            source.component_region, source.solver_region)
        option_region = next((region for region in option_candidates
                              if (case / "constant" / region /
                                  "fvOptions").is_file()), None)
        if option_region is None:
            raise ValueError(
                f"No fvOptions found for heat source {source.name}")
        if option_region not in options_by_region:
            path = case / "constant" / option_region / "fvOptions"
            options_by_region[option_region] = path.read_text(
                encoding="utf-8", errors="replace")
        block = dictionary_block(options_by_region[option_region], source.name)
        expected = {
            "type": "scalarSemiImplicitSource",
            "active": "true",
            "selectionMode": "cellZone",
            "cellZone": source.name,
            "volumeMode": "absolute",
        }
        for key, value in expected.items():
            actual = entry(block, key)
            if actual != value:
                raise ValueError(
                    f"{source.name}: expected {key}={value}, found {actual}")
        power = active_power(block)
        tolerance = 1e-12 * max(1.0, abs(source.watts))
        if abs(power - source.watts) > tolerance:
            raise ValueError(
                f"{source.name}: metadata is {source.watts:g} W but active "
                f"fvOptions applies {power:g} W")
        set_candidates = (
            (source.component_region, case / "constant" /
             source.component_region / "polyMesh" / "sets" / source.name),
            (source.solver_region, case / "constant" /
             source.solver_region / "polyMesh" / "sets" / source.name),
            ("aggregate", case / "constant" / "polyMesh" /
             "sets" / source.name),
        )
        selected_set = next(((space, path) for space, path in set_candidates
                             if path.is_file()), None)
        if selected_set is None:
            raise ValueError(f"No cell set found for heat source {source.name}")
        set_space, set_path = selected_set
        labels = cell_set(set_path)
        region_cells = occupied.setdefault(set_space, {})
        overlaps = sorted(set(labels).intersection(region_cells))
        if overlaps:
            other = region_cells[overlaps[0]]
            raise ValueError(
                f"Heat zones {other} and {source.name} overlap in "
                f"{len(overlaps)} cell(s), including {overlaps[0]}")
        region_cells.update((label, source.name) for label in labels)
        rows.append(AuditRow(
            str(case), source.name, source.component_region, option_region,
            len(labels), source.selected_volume_m3, source.watts, power, "PASS"))
    return rows


def write_markdown(path: Path, rows: list[AuditRow]) -> None:
    summary = power_summary(rows)
    lines = [
        "# OpenFOAM heat-source audit", "",
        "Each PASS confirms a non-empty, internally unique heat cell set; no "
        "overlap with another heat source in the same solver region; and an "
        "active absolute-watt OpenFOAM source matching exported metadata.", "",
        f"Total: {summary['total_sources']} sources, "
        f"{summary['total_power_w']:.9g} W. Solid regions: "
        f"{summary['solid_sources']} sources, {summary['solid_power_w']:.9g} W. "
        f"Fluid region: {summary['fluid_sources']} sources, "
        f"{summary['fluid_power_w']:.9g} W.", "",
        "| Case | Source | Component | Cells | Volume (m^3) | Watts | Status |",
        "|---|---|---|---:|---:|---:|---|",
    ]
    for row in rows:
        lines.append(
            f"| {Path(row.case).name} | {row.source} | "
            f"{row.component_region} | {row.cells} | "
            f"{row.selected_volume_m3:.9g} | {row.active_power_w:.9g} | "
            f"{row.status} |")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cases", nargs="+", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--markdown", type=Path)
    args = parser.parse_args()
    rows = [row for case in args.cases for row in analyze_case(case)]
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps([asdict(row) for row in rows], indent=2) + "\n",
            encoding="utf-8")
    if args.markdown:
        write_markdown(args.markdown, rows)
    for row in rows:
        print(
            f"{Path(row.case).name} | {row.source} | {row.cells} cells | "
            f"{row.active_power_w:g} W | {row.status}")
    summary = power_summary(rows)
    print(
        f"TOTAL | {summary['total_sources']} sources | "
        f"{summary['total_power_w']:g} W | solid "
        f"{summary['solid_sources']} / {summary['solid_power_w']:g} W | fluid "
        f"{summary['fluid_sources']} / {summary['fluid_power_w']:g} W")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
