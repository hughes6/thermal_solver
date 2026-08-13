#!/usr/bin/env python3
"""Report volume-weighted solid temperatures and exported heat allocation."""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import shlex
import subprocess
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path

try:
    from tools.validate_openfoam_case import internal_values
except ModuleNotFoundError:
    from validate_openfoam_case import internal_values


AVERAGE_RE = re.compile(
    r"volAverage\([^)]*\) of T\s*=\s*([-+\d.eE]+)")


@dataclass(frozen=True)
class HeatSource:
    name: str
    component_region: str
    solver_region: str
    watts: float
    selected_volume_m3: float
    volumetric_power_w_m3: float


@dataclass(frozen=True)
class ComponentResult:
    case: str
    time_s: float
    region: str
    cells: int
    volume_weighted_average_temperature_k: float
    cell_weighted_average_temperature_k: float
    minimum_temperature_k: float
    maximum_temperature_k: float
    applied_power_w: float
    selected_source_volume_m3: float
    volumetric_power_w_m3: float
    heat_source_names: str
    heat_solver_regions: str


CONTROL_DICT = """FoamFile
{
    format ascii;
    class dictionary;
    object controlDict;
}
application postProcess;
startFrom latestTime;
stopAt endTime;
endTime 1;
deltaT 1;
writeControl timeStep;
writeInterval 1;
writePrecision 12;
functions
{
    componentTemperatureAverage
    {
        type volFieldValue;
        libs (fieldFunctionObjects);
        writeControl timeStep;
        writeInterval 1;
        writeFields false;
        writeToFile false;
        log true;
        region __REGION__;
        operation volAverage;
        fields (T);
    }
}
"""


def wsl_path(path: Path) -> str:
    resolved = path.resolve()
    if os.name != "nt":
        return resolved.as_posix()
    drive = resolved.drive.rstrip(":").lower()
    suffix = resolved.as_posix().split(":", 1)[1].lstrip("/")
    return f"/mnt/{drive}/{suffix}"


def solid_regions(case: Path) -> list[str]:
    text = (case / "constant" / "regionProperties").read_text(
        encoding="utf-8", errors="replace")
    match = re.search(r"\bsolid\s*\(([^)]*)\)", text, re.DOTALL)
    if not match:
        raise ValueError("regionProperties contains no solid regions")
    return re.findall(r"[A-Za-z0-9_]+", match.group(1))


def heat_sources(case: Path) -> list[HeatSource]:
    text = (case / "constant" / "openfoamExportProperties").read_text(
        encoding="utf-8", errors="replace")
    section = re.search(r"\bheatSources\s*\((.*)\)\s*;", text, re.DOTALL)
    if not section:
        return []
    result = []
    for block in re.findall(r"\{([^{}]*)\}", section.group(1), re.DOTALL):
        def word(name: str) -> str:
            match = re.search(rf"\b{name}\s+([^;\s]+)\s*;", block)
            if not match:
                raise ValueError(f"Heat-source block lacks {name}")
            return match.group(1)

        result.append(HeatSource(
            word("name"), word("componentRegion"), word("solverRegion"),
            float(word("watts")), float(word("selectedVolume")),
            float(word("volumetricPower"))))
    return result


def reconstructed_time(case: Path, regions: list[str], requested: str | None) -> tuple[float, Path]:
    candidates = []
    for child in case.iterdir():
        if not child.is_dir():
            continue
        try:
            value = float(child.name)
        except ValueError:
            continue
        if all((child / region / "T").is_file() for region in regions):
            candidates.append((value, child))
    if not candidates:
        raise ValueError(f"No reconstructed time contains every solid T field in {case}")
    if requested is None:
        return max(candidates, key=lambda item: item[0])
    target = float(requested)
    tolerance = 1e-9 * max(1.0, abs(target))
    matches = [item for item in candidates if abs(item[0] - target) <= tolerance]
    if len(matches) != 1:
        raise ValueError(f"Time {requested} did not uniquely match a complete result")
    return matches[0]


def parse_volume_average(output: str) -> float:
    match = AVERAGE_RE.search(output)
    if not match:
        raise ValueError("OpenFOAM did not report a volume-weighted T average")
    return float(match.group(1))


def volume_average(case: Path, region: str, time_name: str) -> float:
    with tempfile.TemporaryDirectory(prefix="thermal_component_report_") as directory:
        dictionary = Path(directory) / "controlDict"
        dictionary.write_text(
            CONTROL_DICT.replace("__REGION__", region), encoding="utf-8")
        command = (
            "source /usr/lib/openfoam/openfoam2606/etc/bashrc && "
            "export WM_PROJECT_USER_DIR=/tmp/thermal_sim_foam_user && "
            "cd /tmp && postProcess -case " + shlex.quote(wsl_path(case)) +
            " -region " + shlex.quote(region) +
            " -time " + shlex.quote(time_name) +
            " -field T -dict " + shlex.quote(wsl_path(dictionary)))
        launcher = ["wsl.exe", "bash", "-lc", command] if os.name == "nt" \
            else ["bash", "-lc", command]
        result = subprocess.run(
            launcher, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(
                f"OpenFOAM component average failed for {region}:\n{result.stdout}")
        return parse_volume_average(result.stdout)


def analyze_case(case: Path, requested_time: str | None = None) -> list[ComponentResult]:
    case = case.resolve()
    regions = solid_regions(case)
    value, result_path = reconstructed_time(case, regions, requested_time)
    sources = heat_sources(case)
    rows = []
    for region in regions:
        temperatures = internal_values(result_path / region / "T")
        if not temperatures:
            raise ValueError(f"No solid temperatures in {result_path / region / 'T'}")
        assigned = [source for source in sources
                    if source.component_region == region]
        watts = sum(source.watts for source in assigned)
        volume = sum(source.selected_volume_m3 for source in assigned)
        qvol = watts / volume if volume else 0.0
        rows.append(ComponentResult(
            str(case), value, region, len(temperatures),
            volume_average(case, region, result_path.name),
            sum(temperatures) / len(temperatures),
            min(temperatures), max(temperatures), watts, volume, qvol,
            ";".join(source.name for source in assigned),
            ";".join(sorted({source.solver_region for source in assigned}))))
    return rows


def write_csv(path: Path, rows: list[ComponentResult]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(asdict(rows[0])))
        writer.writeheader()
        writer.writerows(asdict(row) for row in rows)


def write_markdown(path: Path, rows: list[ComponentResult]) -> None:
    lines = [
        "# OpenFOAM component thermal report", "",
        "Temperatures use physical volume weighting for averages. The cell-"
        "weighted value is retained only to expose adaptive-mesh sampling bias.",
        "", "| Case | Region | Cells | Volume avg T (K) | Cell avg T (K) | "
        "Min T (K) | Max T (K) | Applied W | Source volume (m^3) |",
        "|---|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in rows:
        lines.append(
            f"| {Path(row.case).name} | {row.region} | {row.cells} | "
            f"{row.volume_weighted_average_temperature_k:.6f} | "
            f"{row.cell_weighted_average_temperature_k:.6f} | "
            f"{row.minimum_temperature_k:.6f} | "
            f"{row.maximum_temperature_k:.6f} | {row.applied_power_w:.6g} | "
            f"{row.selected_source_volume_m3:.9g} |")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cases", nargs="+", type=Path)
    parser.add_argument("--time")
    parser.add_argument("--csv", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--markdown", type=Path)
    args = parser.parse_args()
    rows = [row for case in args.cases
            for row in analyze_case(case, args.time)]
    if args.csv:
        write_csv(args.csv, rows)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps([asdict(row) for row in rows], indent=2) + "\n",
            encoding="utf-8")
    if args.markdown:
        write_markdown(args.markdown, rows)
    for row in rows:
        print(f"{Path(row.case).name} | {row.region} | "
              f"volume avg {row.volume_weighted_average_temperature_k:.6f} K | "
              f"max {row.maximum_temperature_k:.6f} K | "
              f"source {row.applied_power_w:.6g} W")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
