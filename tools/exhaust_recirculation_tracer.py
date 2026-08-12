#!/usr/bin/env python3
"""Run source-attributed exhaust tracers on a fixed OpenFOAM airflow solution."""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path


ZONE_RE = re.compile(r"^ZONE_AVERAGE,([^,\r\n]+),([^,\r\n]+)$", re.MULTILINE)
CHANGE_RE = re.compile(r"^Final max change:\s*(\S+)", re.MULTILINE)
MASS_RE = re.compile(r"^ZONE_MASS_INLET,([^,\r\n]+),([^,\r\n]+),([^,\r\n]+)$", re.MULTILINE)
MESH_RE = re.compile(r"^MESH_SIZE,(\d+),(\d+)$", re.MULTILINE)


@dataclass(frozen=True)
class Device:
    component_id: int
    component: str
    intake_zones: tuple[str, ...]
    exhaust_zones: tuple[str, ...]


def load_devices(path: Path) -> list[Device]:
    paired: dict[int, dict[str, list[str]]] = {}
    names: dict[int, str] = {}
    with path.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            component_id = int(row["component_id"])
            names[component_id] = row["component"]
            paired.setdefault(component_id, {}).setdefault(
                row["kind"], []).append(row["zone"])
    devices = []
    for component_id in sorted(paired):
        zones = paired[component_id]
        if "intake" in zones:
            devices.append(Device(
                component_id, names[component_id],
                tuple(zones["intake"]), tuple(zones.get("exhaust", ()))))
    if not devices or not any(device.exhaust_zones for device in devices):
        raise ValueError(f"No exhaust source with component intakes in {path}")
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


def select_time(times: list[tuple[float, Path]], requested: str | None) -> Path:
    valid = [(value, path) for value, path in times if all(
        (path / "fluid" / field).is_file() for field in ("rho", "phi", "nut"))]
    if not valid:
        raise ValueError("No reconstructed time contains fluid/rho, fluid/phi, and fluid/nut")
    if requested is None:
        return valid[-1][1]
    exact = [path for _, path in valid if path.name == requested]
    if exact:
        return exact[0]
    try:
        target = float(requested)
    except ValueError as error:
        raise ValueError(f"Unknown reconstructed time: {requested}") from error
    scale = max(1.0, abs(target))
    matches = [path for value, path in valid if abs(value - target) <= 1e-9*scale]
    if len(matches) != 1:
        available = ", ".join(path.name for _, path in valid)
        raise ValueError(f"Time {requested} did not uniquely match. Available: {available}")
    return matches[0]


def add_tracer_solver(solution: str) -> str:
    match = re.search(r"\bsolvers\s*\{", solution)
    if not match:
        raise ValueError("fvSolution has no solvers dictionary")
    tracer = '''
    "tracer.*"
    {
        solver          PBiCGStab;
        preconditioner  DILU;
        tolerance       1e-12;
        relTol          0;
    }
'''
    return solution[:match.end()] + tracer + solution[match.end():]


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


def copy_case(source: Path, output: Path, time_dir: Path, metadata: Path) -> None:
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
    solution = add_tracer_solver(
        (source / "system" / "fluid" / "fvSolution").read_text(encoding="utf-8"))
    (output / "system" / "fluid" / "fvSolution").write_text(solution, encoding="utf-8")
    shutil.copy2(metadata, output / "internal_airflow_devices.csv")


def safe_field_name(component_id: int) -> str:
    return f"tracer_source_{component_id}"


def write_reports(output: Path, devices: list[Device], matrix: dict[tuple[int, int], float], intake_flows: dict[int, float], metadata: dict) -> None:
    sources = [device for device in devices if device.exhaust_zones]
    csv_path = output / "exhaust_recirculation_matrix.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["source_exhaust", "source_component", "target_intake", "target_component", "mass_weighted_tracer_fraction", "percent", "target_incoming_mass_flow_kg_s"])
        for source in sources:
            for target in devices:
                value = matrix[source.component_id, target.component_id]
                writer.writerow([
                    ";".join(source.exhaust_zones), source.component,
                    ";".join(target.intake_zones), target.component,
                    f"{value:.9g}", f"{100*value:.6g}",
                    f"{intake_flows[target.component_id]:.9g}"])
    lines = ["# Exhaust-to-intake recirculation attribution", "", f"Source: `{metadata['source_case']}` at `{metadata['source_time']}` s", f"Mesh: {metadata['mesh_cells']} cells, {metadata['mesh_faces']} faces; Sc_t = {metadata['turbulent_schmidt']}", "", "Values are incoming-mass-flux-weighted passive-tracer fractions across each intake cell-zone boundary.", "", "| Exhaust source \\ Intake | " + " | ".join(d.component for d in devices) + " |", "|---|" + "---:|" * len(devices)]
    for source in sources:
        values = [f"{100*matrix[source.component_id, target.component_id]:.4f}%" for target in devices]
        lines.append(f"| {source.component} | " + " | ".join(values) + " |")
    (output / "exhaust_recirculation_matrix.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (output / "exhaust_recirculation_metadata.json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_case", type=Path)
    parser.add_argument("output_case", type=Path)
    parser.add_argument("--solver", default="steadyExhaustTracerFoam")
    parser.add_argument("--device-metadata", type=Path,
                        help="device CSV override for a legacy source case")
    parser.add_argument("--time", help="reconstructed time name or numeric value (default latest)")
    parser.add_argument("--schmidt", type=float, default=0.7)
    parser.add_argument("--iterations", type=int, default=500)
    parser.add_argument("--tolerance", type=float, default=1e-9)
    args = parser.parse_args()
    source = args.source_case.resolve()
    output = args.output_case.resolve()
    metadata = (args.device_metadata.resolve() if args.device_metadata
                else source / "internal_airflow_devices.csv")
    if not metadata.is_file():
        raise SystemExit(
            f"Missing device metadata: {metadata}. Supply --device-metadata "
            "for a legacy exported case.")
    devices = load_devices(metadata)
    try:
        latest = select_time(numeric_times(source), args.time)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    copy_case(source, output, latest, metadata)
    matrix: dict[tuple[int, int], float] = {}
    intake_flows: dict[int, float] = {}
    solve_records = []
    mesh_size = None
    for source_device in (device for device in devices if device.exhaust_zones):
        source_zone_list = "(" + " ".join(source_device.exhaust_zones) + ")"
        command = [args.solver, "-case", str(output), "-region", "fluid",
                   "-latestTime", "-source-zones", source_zone_list,
                   "-field", safe_field_name(source_device.component_id),
                   "-schmidt", str(args.schmidt), "-iterations",
                   str(args.iterations), "-tolerance", str(args.tolerance)]
        started = time.perf_counter()
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        elapsed = time.perf_counter() - started
        (output / f"log.tracer_source_{source_device.component_id}").write_text(result.stdout, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"Tracer solve failed for {source_device.component}: see {output / f'log.tracer_source_{source_device.component_id}'}")
        _, mass_inlets, change = parse_solver_output(result.stdout, args.tolerance)
        mesh_match = MESH_RE.search(result.stdout)
        if not mesh_match:
            raise ValueError("Tracer solver did not report mesh size")
        current_mesh = (int(mesh_match.group(1)), int(mesh_match.group(2)))
        if mesh_size is not None and current_mesh != mesh_size:
            raise ValueError("Mesh size changed between tracer source solves")
        mesh_size = current_mesh
        solve_records.append({"component_id": source_device.component_id,
                              "component": source_device.component,
                              "exhaust_zones": list(source_device.exhaust_zones),
                              "elapsed_seconds": elapsed,
                              "final_max_change": change})
        for target in devices:
            missing = [zone for zone in target.intake_zones
                       if zone not in mass_inlets]
            if missing:
                raise ValueError(f"Missing intake zone {missing[0]} in solver output")
            samples = [mass_inlets[zone] for zone in target.intake_zones]
            mass_flow = sum(flow for _, flow in samples)
            if mass_flow <= 0:
                raise ValueError(f"Component {target.component} has no incoming mass flow")
            fraction = sum(value*flow for value, flow in samples)/mass_flow
            matrix[source_device.component_id, target.component_id] = fraction
            previous = intake_flows.setdefault(target.component_id, mass_flow)
            if abs(previous - mass_flow) > max(1e-10, 1e-6*mass_flow):
                raise ValueError(f"Inconsistent incoming mass flow for {target.component}")
    metadata_output = {
        "source_case": str(source),
        "source_time": latest.name,
        "device_metadata": str(metadata),
        "mesh_cells": mesh_size[0],
        "mesh_faces": mesh_size[1],
        "turbulent_schmidt": args.schmidt,
        "maximum_iterations": args.iterations,
        "field_change_tolerance": args.tolerance,
        "source_solves": solve_records,
    }
    write_reports(output, devices, matrix, intake_flows, metadata_output)
    print(output / "exhaust_recirculation_matrix.csv")
    print(output / "exhaust_recirculation_matrix.md")
    print(output / "exhaust_recirculation_metadata.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
