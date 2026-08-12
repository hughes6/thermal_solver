"""Numerically audit an exported OpenFOAM rack validation case."""

from __future__ import annotations

import argparse
import json
import math
import re
import struct
from dataclasses import asdict, dataclass
from pathlib import Path

try:
    from tools.openfoam_field_delta import (
        resolve_time_directory,
    )
except ModuleNotFoundError:  # Direct execution: python tools/validate_openfoam_case.py
    from openfoam_field_delta import resolve_time_directory


@dataclass
class ValidationResult:
    case: str
    time_s: float
    cells: int
    connected_fluid_regions: int
    expected_connected_fluid_regions: int
    inlet_mass_flow_kg_s: float
    outlet_mass_flow_kg_s: float
    mass_imbalance_fraction: float
    outlet_gross_mass_flow_kg_s: float
    outlet_reverse_flow_fraction: float
    inlet_temperature_k: float
    outlet_temperature_k: float
    expected_outlet_temperature_k: float
    fluent_temperature_k: float | None
    transported_power_w: float
    applied_power_w: float
    energy_error_fraction: float
    fluent_error_k: float | None
    solid_average_temperature_k: float | None
    solid_min_temperature_k: float | None
    solid_max_temperature_k: float | None
    pass_connectivity: bool
    pass_mass_balance: bool
    pass_energy_balance: bool
    pass_fluent_temperature: bool | None

    @property
    def passed(self) -> bool:
        checks = [
            self.pass_connectivity,
            self.pass_mass_balance,
            self.pass_energy_balance,
        ]
        if self.pass_fluent_temperature is not None:
            checks.append(self.pass_fluent_temperature)
        return all(checks)


def latest_time(case: Path) -> tuple[float, Path]:
    candidates = []
    for path in case.iterdir():
        if not path.is_dir():
            continue
        try:
            candidates.append((float(path.name), path))
        except ValueError:
            continue
    if not candidates:
        raise ValueError(f"No reconstructed result times found in {case}")
    return max(candidates, key=lambda item: item[0])


def latest_result_paths(case: Path) -> tuple[float, list[Path]]:
    """Return the newest complete result, preferring decomposed checkpoints."""
    processors = sorted(
        (path for path in case.glob("processor*") if path.is_dir()),
        key=lambda path: int(path.name[9:]),
    )
    if processors:
        common: set[float] | None = None
        for processor in processors:
            values = set()
            for child in processor.iterdir():
                if child.is_dir():
                    try:
                        values.add(float(child.name))
                    except ValueError:
                        pass
            common = values if common is None else common & values
        if not common:
            raise ValueError("No numeric checkpoint time is common to every rank")
        latest = max(common)
        paths = [
            resolve_time_directory(processor, str(latest))
            for processor in processors
        ]
        return latest, paths
    value, path = latest_time(case)
    return value, [path]


def physical_open_patches(case: Path) -> list[str]:
    """Read physical, non-wall openings from the undecomposed fluid mesh."""
    boundary = case / "constant" / "fluid" / "polyMesh" / "boundary"
    text = boundary.read_text(encoding="ascii", errors="replace")
    names = re.findall(
        r"(?m)^\s{4}([^\s{}]+)\s*\n\s*\{\s*\n\s*type\s+patch\s*;",
        text,
    )
    if not names:
        raise ValueError(f"No physical type=patch openings found in {boundary}")
    return names


def _list_payload(
    data: bytes, marker: bytes, width: int, *, is_binary: bool | None = None
) -> list[float | int]:
    start = data.find(marker)
    if start < 0:
        raise ValueError(f"Could not find {marker!r}")
    chunk = data[start:]
    match = re.search(
        rb"nonuniform\s+List<(?:scalar|label)>\s+(\d+)\s*\(", chunk
    )
    if not match:
        raise ValueError(f"Could not parse list following {marker!r}")
    count = int(match.group(1))
    position = start + match.end()

    # Binary OpenFOAM payload starts immediately after "(". Do not trim
    # bytes by their value: a valid IEEE-754 scalar can begin with 0x20,
    # 0x09, 0x0a, or 0x0d and would then be shifted/corrupted as if it were
    # textual whitespace. Determine the representation from the FoamFile
    # header instead of probing payload bytes.
    if is_binary is None:
        is_binary = re.search(rb"\bformat\s+binary\s*;", data[:start]) is not None
    if is_binary:
        format_code = "d" if width == 8 else "i"
        return list(struct.unpack_from(f"<{count}{format_code}", data, position))

    while data[position : position + 1] in b"\r\n \t":
        position += 1

    # ASCII fields have printable numeric text after the opening parenthesis.
    probe = data[position : position + min(32, len(data) - position)]
    if probe and all(byte in b"0123456789+-.eE \t\r\n" for byte in probe):
        end = data.find(b")", position)
        values = data[position:end].decode("ascii").split()
        return [float(value) if width == 8 else int(value) for value in values]

    raise ValueError(f"ASCII list following {marker!r} has non-numeric data")


def patch_values(field: Path, patch: str) -> list[float]:
    data = field.read_bytes()
    is_binary = re.search(rb"\bformat\s+binary\s*;", data) is not None
    boundary = data.find(b"boundaryField")
    marker = patch.encode()
    start = data.find(marker, boundary)
    if start < 0:
        raise ValueError(f"Patch {patch!r} not found in {field}")
    patch_data = data[start:]
    uniform = re.search(
        rb"\bvalue\s+uniform\s+([\d.eE+-]+)\s*;", patch_data[:512]
    )
    empty = re.search(
        rb"\bvalue\s+nonuniform\s+List<scalar>\s+0\s*;", patch_data[:512]
    )
    nonuniform = re.search(
        rb"\bvalue\s+nonuniform\s+List<scalar>\s+\d+\s*\(", patch_data
    )
    first_nonuniform = min(
        (match.start() for match in (empty, nonuniform) if match),
        default=None,
    )
    if uniform and (first_nonuniform is None or uniform.start() < first_nonuniform):
        return [float(uniform.group(1))]
    if empty and (not nonuniform or empty.start() < nonuniform.start()):
        return []
    return [
        float(value)
        for value in _list_payload(
            patch_data, b"value", 8, is_binary=is_binary
        )
    ]


def internal_values(field: Path) -> list[float]:
    data = field.read_bytes()
    uniform = re.search(rb"\binternalField\s+uniform\s+([\d.eE+-]+)\s*;", data)
    nonuniform = re.search(rb"\binternalField\s+nonuniform\b", data)
    if uniform and (not nonuniform or uniform.start() < nonuniform.start()):
        return [float(uniform.group(1))]
    if re.search(
        rb"\binternalField\s+nonuniform\s+List<scalar>\s+0\s*;", data
    ):
        return []
    return [float(value) for value in _list_payload(data, b"internalField", 8)]


def label_list(path: Path) -> list[int]:
    data = path.read_bytes()
    match = re.search(rb"\n(\d+)\s*\n\(", data)
    if not match:
        raise ValueError(f"Could not parse label list in {path}")
    count = int(match.group(1))
    position = match.end()
    if re.search(rb"\bformat\s+binary\s*;", data[: match.start()]):
        return list(struct.unpack_from(f"<{count}i", data, position))
    while data[position : position + 1] in b"\r\n \t":
        position += 1
    end = data.find(b")", position)
    return [int(value) for value in data[position:end].decode("ascii").split()]


def mesh_connectivity(poly_mesh: Path) -> tuple[int, int]:
    owner = label_list(poly_mesh / "owner")
    neighbour = label_list(poly_mesh / "neighbour")
    cells = max(owner) + 1
    adjacency = [[] for _ in range(cells)]
    for first, second in zip(owner, neighbour):
        adjacency[first].append(second)
        adjacency[second].append(first)

    seen: set[int] = set()
    regions = 0
    for cell in range(cells):
        if cell in seen:
            continue
        regions += 1
        stack = [cell]
        seen.add(cell)
        while stack:
            current = stack.pop()
            for adjacent in adjacency[current]:
                if adjacent not in seen:
                    seen.add(adjacent)
                    stack.append(adjacent)
    return cells, regions


def applied_power(case: Path) -> float:
    properties = (case / "constant" / "openfoamExportProperties").read_text(
        encoding="utf-8", errors="replace"
    )
    values = [float(value) for value in re.findall(r"\bwatts\s+([\d.eE+-]+);", properties)]
    if not values:
        raise ValueError("No heat-source wattage found in openfoamExportProperties")
    return sum(values)


def expected_fluid_regions(case: Path) -> int:
    properties_path = case / "constant" / "openfoamExportProperties"
    properties = properties_path.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"\bexpectedConnectedFluidRegions\s+(\d+)\s*;", properties)
    return int(match.group(1)) if match else 1


def signed_weighted_average(values: list[float], weights: list[float]) -> float:
    if len(values) == 1:
        return values[0]
    if len(values) != len(weights):
        raise ValueError("Field and flux arrays have different lengths")
    denominator = sum(weights)
    if abs(denominator) <= 1.0e-15:
        raise ValueError("Net patch mass flow is zero")
    return sum(value * weight for value, weight in zip(values, weights)) / denominator


def _expanded_patch_values(values: list[float], count: int) -> list[float]:
    if len(values) == 1:
        return values * count
    if len(values) != count:
        raise ValueError("Patch temperature and flux arrays have different lengths")
    return values


def collect_patch_faces(
    result_paths: list[Path], patches: list[str]
) -> tuple[list[float], list[float]]:
    fluxes: list[float] = []
    temperatures: list[float] = []
    for result_path in result_paths:
        fluid = result_path / "fluid"
        for patch in patches:
            patch_fluxes = patch_values(fluid / "phi", patch)
            patch_temperatures = _expanded_patch_values(
                patch_values(fluid / "T", patch), len(patch_fluxes)
            )
            fluxes.extend(patch_fluxes)
            temperatures.extend(patch_temperatures)
    return fluxes, temperatures


def audit_case(
    case: Path,
    inlet: str | None,
    outlet: str | None,
    cp: float,
    fluent_temperature: float | None,
    energy_tolerance: float,
    mass_tolerance: float,
    fluent_tolerance_k: float,
    expected_connected_fluid_regions: int | None = None,
) -> ValidationResult:
    if expected_connected_fluid_regions is None:
        expected_connected_fluid_regions = expected_fluid_regions(case)
    time_s, result_paths = latest_result_paths(case)
    missing = [
        str(path / "fluid" / field)
        for path in result_paths
        for field in ("phi", "T")
        if not (path / "fluid" / field).is_file()
    ]
    if missing:
        raise ValueError(
            f"Latest complete time {time_s:g} is missing fluid fields: "
            + ", ".join(missing)
        )

    if (inlet is None) != (outlet is None):
        raise ValueError("Specify both inlet and outlet, or neither for automatic openings")
    if inlet is None:
        patches = physical_open_patches(case)
        all_phi, all_temperature = collect_patch_faces(result_paths, patches)
        inlet_phi = [value for value in all_phi if value < 0.0]
        inlet_temperature = [
            temperature for temperature, flux in zip(all_temperature, all_phi)
            if flux < 0.0
        ]
        outlet_phi = [value for value in all_phi if value > 0.0]
        outlet_temperature = [
            temperature for temperature, flux in zip(all_temperature, all_phi)
            if flux > 0.0
        ]
        if not inlet_phi or not outlet_phi:
            raise ValueError("Automatic openings contain no inlet or outlet flow")
    else:
        inlet_phi, inlet_temperature = collect_patch_faces(result_paths, [inlet])
        outlet_phi, outlet_temperature = collect_patch_faces(result_paths, [outlet])

    inlet_flow = sum(inlet_phi)
    outlet_flow = sum(outlet_phi)
    net_scale = max(abs(inlet_flow), abs(outlet_flow), 1.0e-15)
    mass_error = abs(inlet_flow + outlet_flow) / net_scale

    inlet_t = signed_weighted_average(inlet_temperature, inlet_phi)
    outlet_t = signed_weighted_average(outlet_temperature, outlet_phi)
    power = applied_power(case)
    transported = outlet_flow * cp * (outlet_t - inlet_t)
    energy_error = abs(transported - power) / max(abs(power), 1.0e-15)
    expected_outlet = inlet_t + power / (outlet_flow * cp)

    positive = sum(max(value, 0.0) for value in outlet_phi)
    reverse = sum(max(-value, 0.0) for value in outlet_phi)
    gross = positive + reverse
    reverse_fraction = reverse / gross if gross else 0.0

    cells, regions = mesh_connectivity(case / "constant" / "fluid" / "polyMesh")
    fluent_error = (
        abs(outlet_t - fluent_temperature)
        if fluent_temperature is not None
        else None
    )
    solid_values: list[float] = []
    for result_path in result_paths:
        for region in result_path.iterdir():
            solid_t = region / "T"
            if region.name != "fluid" and solid_t.is_file():
                solid_values.extend(internal_values(solid_t))

    return ValidationResult(
        case=str(case),
        time_s=time_s,
        cells=cells,
        connected_fluid_regions=regions,
        expected_connected_fluid_regions=expected_connected_fluid_regions,
        inlet_mass_flow_kg_s=inlet_flow,
        outlet_mass_flow_kg_s=outlet_flow,
        mass_imbalance_fraction=mass_error,
        outlet_gross_mass_flow_kg_s=gross,
        outlet_reverse_flow_fraction=reverse_fraction,
        inlet_temperature_k=inlet_t,
        outlet_temperature_k=outlet_t,
        expected_outlet_temperature_k=expected_outlet,
        fluent_temperature_k=fluent_temperature,
        transported_power_w=transported,
        applied_power_w=power,
        energy_error_fraction=energy_error,
        fluent_error_k=fluent_error,
        solid_average_temperature_k=(
            sum(solid_values) / len(solid_values) if solid_values else None
        ),
        solid_min_temperature_k=min(solid_values) if solid_values else None,
        solid_max_temperature_k=max(solid_values) if solid_values else None,
        pass_connectivity=regions == expected_connected_fluid_regions,
        pass_mass_balance=mass_error <= mass_tolerance,
        pass_energy_balance=energy_error <= energy_tolerance,
        pass_fluent_temperature=(
            fluent_error <= fluent_tolerance_k
            if fluent_error is not None
            else None
        ),
    )


def markdown(result: ValidationResult) -> str:
    status = "PASS" if result.passed else "FAIL"
    fluent_row = ""
    if result.fluent_temperature_k is not None:
        fluent_row = (
            f"| Fluent outlet comparison | {result.outlet_temperature_k:.3f} K "
            f"vs {result.fluent_temperature_k:.3f} K | "
            f"{result.fluent_error_k:.3f} K | "
            f"{'PASS' if result.pass_fluent_temperature else 'FAIL'} |\n"
        )
    return f"""# OpenFOAM validation report

Overall result: **{status}**

| Check | Result | Error/diagnostic | Status |
|---|---:|---:|---:|
| Fluid connectivity | {result.connected_fluid_regions} region(s), {result.cells} cells | expected {result.expected_connected_fluid_regions} | {'PASS' if result.pass_connectivity else 'FAIL'} |
| Mass conservation | inlet {result.inlet_mass_flow_kg_s:.8g} kg/s, outlet {result.outlet_mass_flow_kg_s:.8g} kg/s | {100*result.mass_imbalance_fraction:.5f}% | {'PASS' if result.pass_mass_balance else 'FAIL'} |
| Energy conservation | {result.transported_power_w:.4f} W transported vs {result.applied_power_w:.4f} W applied | {100*result.energy_error_fraction:.4f}% | {'PASS' if result.pass_energy_balance else 'FAIL'} |
{fluent_row}
## Temperatures and outlet behavior

- Result time: {result.time_s:g} s
- Inlet signed mass-weighted temperature: {result.inlet_temperature_k:.4f} K
- Outlet signed mass-weighted temperature: {result.outlet_temperature_k:.4f} K
- Analytical outlet temperature from Q/(m_dot Cp): {result.expected_outlet_temperature_k:.4f} K
- Solid average temperature: {result.solid_average_temperature_k if result.solid_average_temperature_k is not None else 'not available'} K
- Solid temperature range: {result.solid_min_temperature_k if result.solid_min_temperature_k is not None else 'not available'} to {result.solid_max_temperature_k if result.solid_max_temperature_k is not None else 'not available'} K
- Outlet gross bidirectional flow: {result.outlet_gross_mass_flow_kg_s:.6g} kg/s
- Reverse-flow share of gross outlet traffic: {100*result.outlet_reverse_flow_fraction:.2f}%

The signed mass-flux average is required when an outlet has simultaneous
forward and reverse flow. An absolute-flow average is not an energy balance.
"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path)
    parser.add_argument(
        "--inlet", help="inlet patch; omit with --outlet to classify all open faces"
    )
    parser.add_argument(
        "--outlet", help="outlet patch; omit with --inlet to classify all open faces"
    )
    parser.add_argument("--cp", type=float, default=1005.0)
    parser.add_argument("--fluent-temperature", type=float)
    parser.add_argument("--energy-tolerance", type=float, default=0.02)
    parser.add_argument("--mass-tolerance", type=float, default=0.01)
    parser.add_argument("--fluent-tolerance-k", type=float, default=1.0)
    parser.add_argument(
        "--expected-connected-fluid-regions",
        type=int,
        help=("expected mesh components, including intentional sealed air volumes; "
              "defaults to exported metadata or 1 for legacy cases"),
    )
    parser.add_argument("--json", type=Path)
    parser.add_argument("--markdown", type=Path)
    args = parser.parse_args()

    result = audit_case(
        args.case.resolve(), args.inlet, args.outlet, args.cp,
        args.fluent_temperature, args.energy_tolerance,
        args.mass_tolerance, args.fluent_tolerance_k,
        args.expected_connected_fluid_regions,
    )
    rendered = markdown(result)
    print(rendered)
    if args.json:
        args.json.write_text(json.dumps(asdict(result), indent=2) + "\n")
    if args.markdown:
        args.markdown.write_text(rendered)
    raise SystemExit(0 if result.passed else 1)


if __name__ == "__main__":
    main()
