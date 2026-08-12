#!/usr/bin/env python3
"""Run source-attributed exhaust tracers on a fixed OpenFOAM airflow solution."""

from __future__ import annotations

import argparse
import csv
import re
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path


ZONE_RE = re.compile(r"^ZONE_AVERAGE,([^,\r\n]+),([^,\r\n]+)$", re.MULTILINE)
CHANGE_RE = re.compile(r"^Final max change:\s*(\S+)", re.MULTILINE)
MASS_RE = re.compile(r"^ZONE_MASS_INLET,([^,\r\n]+),([^,\r\n]+),([^,\r\n]+)$", re.MULTILINE)


@dataclass(frozen=True)
class Device:
    component_id: int
    component: str
    intake_zone: str
    exhaust_zone: str


def load_devices(path: Path) -> list[Device]:
    paired: dict[int, dict[str, str]] = {}
    names: dict[int, str] = {}
    with path.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            component_id = int(row["component_id"])
            names[component_id] = row["component"]
            paired.setdefault(component_id, {})[row["kind"]] = row["zone"]
    devices = []
    for component_id in sorted(paired):
        zones = paired[component_id]
        if "intake" in zones and "exhaust" in zones:
            devices.append(Device(component_id, names[component_id], zones["intake"], zones["exhaust"]))
    if not devices:
        raise ValueError(f"No paired intake/exhaust devices in {path}")
    return devices


def numeric_times(case: Path) -> list[tuple[float, Path]]:
    result = []
    for child in case.iterdir():
        if child.is_dir():
            try:
                result.append((float(child.name), child))
            except ValueError:
                pass
    return sorted(result)


def parse_solver_output(text: str, tolerance: float) -> tuple[dict[str, float], dict[str, tuple[float, float]], float]:
    averages = {name: float(value) for name, value in ZONE_RE.findall(text)}
    mass_inlets = {name: (float(value), float(flow)) for name, value, flow in MASS_RE.findall(text)}
    match = CHANGE_RE.search(text)
    if not match:
        raise ValueError("Tracer solver did not report final convergence")
    change = float(match.group(1))
    if change > tolerance:
        raise ValueError(f"Tracer did not converge: max change {change:g} > {tolerance:g}")
    return averages, mass_inlets, change


def copy_case(source: Path, output: Path, time_dir: Path) -> None:
    if output.exists():
        raise FileExistsError(f"Refusing to overwrite existing output: {output}")
    (output / "constant" / "fluid").mkdir(parents=True)
    (output / time_dir.name / "fluid").mkdir(parents=True)
    (output / "system" / "fluid").mkdir(parents=True)
    shutil.copytree(source / "constant" / "fluid" / "polyMesh", output / "constant" / "fluid" / "polyMesh")
    for field in ("rho", "phi", "nut"):
        shutil.copy2(time_dir / "fluid" / field, output / time_dir.name / "fluid" / field)
    shutil.copy2(source / "system" / "controlDict", output / "system" / "controlDict")
    shutil.copy2(source / "system" / "fluid" / "fvSchemes", output / "system" / "fluid" / "fvSchemes")
    solution = (source / "system" / "fluid" / "fvSolution").read_text(encoding="utf-8")
    solution = solution.replace("(U|h|k|omega)", "(U|h|k|omega|tracer.*)")
    (output / "system" / "fluid" / "fvSolution").write_text(solution, encoding="utf-8")
    shutil.copy2(source / "internal_airflow_devices.csv", output / "internal_airflow_devices.csv")


def safe_field_name(component_id: int) -> str:
    return f"tracer_source_{component_id}"


def write_reports(output: Path, devices: list[Device], matrix: dict[tuple[int, int], float], intake_flows: dict[int, float]) -> None:
    csv_path = output / "exhaust_recirculation_matrix.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["source_exhaust", "source_component", "target_intake", "target_component", "mass_weighted_tracer_fraction", "percent", "target_incoming_mass_flow_kg_s"])
        for source in devices:
            for target in devices:
                value = matrix[source.component_id, target.component_id]
                writer.writerow([source.exhaust_zone, source.component, target.intake_zone, target.component, f"{value:.9g}", f"{100*value:.6g}", f"{intake_flows[target.component_id]:.9g}"])
    lines = ["# Exhaust-to-intake recirculation attribution", "", "Values are incoming-mass-flux-weighted passive-tracer fractions across each intake cell-zone boundary.", "", "| Exhaust source \\ Intake | " + " | ".join(d.component for d in devices) + " |", "|---|" + "---:|" * len(devices)]
    for source in devices:
        values = [f"{100*matrix[source.component_id, target.component_id]:.4f}%" for target in devices]
        lines.append(f"| {source.component} | " + " | ".join(values) + " |")
    (output / "exhaust_recirculation_matrix.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_case", type=Path)
    parser.add_argument("output_case", type=Path)
    parser.add_argument("--solver", default="steadyExhaustTracerFoam")
    parser.add_argument("--schmidt", type=float, default=0.7)
    parser.add_argument("--iterations", type=int, default=500)
    parser.add_argument("--tolerance", type=float, default=1e-9)
    args = parser.parse_args()
    source = args.source_case.resolve()
    output = args.output_case.resolve()
    devices = load_devices(source / "internal_airflow_devices.csv")
    times = [(value, path) for value, path in numeric_times(source) if all((path / "fluid" / field).is_file() for field in ("rho", "phi", "nut"))]
    if not times:
        raise SystemExit("No reconstructed time contains fluid/rho, fluid/phi, and fluid/nut")
    _, latest = times[-1]
    copy_case(source, output, latest)
    matrix: dict[tuple[int, int], float] = {}
    intake_flows: dict[int, float] = {}
    for source_device in devices:
        command = [args.solver, "-case", str(output), "-region", "fluid", "-latestTime", "-source-zone", source_device.exhaust_zone, "-field", safe_field_name(source_device.component_id), "-schmidt", str(args.schmidt), "-iterations", str(args.iterations), "-tolerance", str(args.tolerance)]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (output / f"log.tracer_source_{source_device.component_id}").write_text(result.stdout, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"Tracer solve failed for {source_device.component}: see {output / f'log.tracer_source_{source_device.component_id}'}")
        _, mass_inlets, _ = parse_solver_output(result.stdout, args.tolerance)
        for target in devices:
            if target.intake_zone not in mass_inlets:
                raise ValueError(f"Missing intake zone {target.intake_zone} in solver output")
            fraction, mass_flow = mass_inlets[target.intake_zone]
            if mass_flow <= 0:
                raise ValueError(f"Intake zone {target.intake_zone} has no incoming mass flow")
            matrix[source_device.component_id, target.component_id] = fraction
            previous = intake_flows.setdefault(target.component_id, mass_flow)
            if abs(previous - mass_flow) > max(1e-10, 1e-6*mass_flow):
                raise ValueError(f"Inconsistent incoming mass flow for {target.intake_zone}")
    write_reports(output, devices, matrix, intake_flows)
    print(output / "exhaust_recirculation_matrix.csv")
    print(output / "exhaust_recirculation_matrix.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
