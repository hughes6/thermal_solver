#!/usr/bin/env python3
"""Audit ambient-opening mass balance and optional device directions."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path
import re


def read_last_value(path: Path) -> tuple[float, float] | None:
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            columns = line.split()
            rows.append((float(columns[0]), float(columns[1])))
    return rows[-1] if rows else None


def opening_observations(
    case: Path,
) -> list[dict[str, float | str]]:
    """Return the last value written in every opening/time directory."""
    root = case / "postProcessing" / "fluid"
    objects = sorted(
        path for path in root.glob("*_mass_flow")
        if path.name != "ambient_net_mass_flow"
    )
    observations = []
    suffix = "_mass_flow"
    for obj in objects:
        for time_dir in sorted(obj.iterdir()):
            data = time_dir / "surfaceFieldValue.dat"
            if not data.is_file():
                continue
            value = read_last_value(data)
            if value is None:
                continue
            time, phi = value
            observations.append({
                "time_s": time,
                "time_directory": time_dir.name,
                "function_object": obj.name,
                "opening": obj.name.removesuffix(suffix),
                "phi_kg_s": phi,
            })
    return observations


def audit(case: Path) -> list[dict[str, float | int | str]]:
    by_time: dict[float, list[float]] = {}
    labels: dict[float, str] = {}
    for observation in opening_observations(case):
        time = float(observation["time_s"])
        phi = float(observation["phi_kg_s"])
        by_time.setdefault(time, []).append(phi)
        labels[time] = str(observation["time_directory"])

    result = []
    for time in sorted(by_time):
        values = by_time[time]
        positive = sum(value for value in values if value > 0.0)
        inflow = -sum(value for value in values if value < 0.0)
        net = positive - inflow
        scale = max(positive, inflow, 1.0e-15)
        result.append({
            "time_s": time,
            "time_directory": labels[time],
            "opening_count": len(values),
            "outflow_kg_s": positive,
            "inflow_kg_s": inflow,
            "net_kg_s": net,
            "mismatch_fraction": abs(net) / scale,
        })
    return result


def add_exchange_metrics(
    rows: list[dict[str, float | int | str]],
    density_kg_m3: float,
    fluid_volume_m3: float,
) -> list[dict[str, float | int | str]]:
    """Add runner-equivalent cumulative one-way air-exchange accounting."""
    if not math.isfinite(density_kg_m3) or density_kg_m3 <= 0.0:
        raise ValueError("Density must be positive and finite")
    if not math.isfinite(fluid_volume_m3) or fluid_volume_m3 <= 0.0:
        raise ValueError("Fluid volume must be positive and finite")
    target_mass = density_kg_m3 * fluid_volume_m3
    previous_time = 0.0
    previous_flow = 0.0
    accumulated_mass = 0.0
    enriched = []
    for source in rows:
        row = dict(source)
        time = float(row["time_s"])
        if time < previous_time:
            raise ValueError("Mass-balance rows must be ordered by time")
        one_way_flow = 0.5 * (
            float(row["outflow_kg_s"]) + float(row["inflow_kg_s"])
        )
        accumulated_mass += (
            0.5 * (previous_flow + one_way_flow) * (time - previous_time)
        )
        remaining_mass = max(0.0, target_mass - accumulated_mass)
        projected_time = (
            time + remaining_mass / one_way_flow
            if one_way_flow > 0.0 else math.inf
        )
        row.update({
            "one_way_mass_flow_kg_s": one_way_flow,
            "cumulative_exchanged_mass_kg": accumulated_mass,
            "cumulative_exchanged_volume_m3": accumulated_mass / density_kg_m3,
            "cumulative_exchange_fraction": accumulated_mass / target_mass,
            "projected_one_exchange_time_s": projected_time,
        })
        enriched.append(row)
        previous_time = time
        previous_flow = one_way_flow
    return enriched


def foam_word(name: str) -> str:
    """Apply the exporter's ASCII OpenFOAM-word normalization."""
    value = re.sub(r"[^A-Za-z0-9_]", "_", name).lstrip("_")
    if not value or value[0].isdigit():
        value = "region_" + value
    return value


def read_ambient_devices(path: Path) -> list[dict[str, str]]:
    """Read ambient device names and kinds from airflow_devices.txt."""
    devices = []
    in_ambient_section = False
    seen_objects: dict[str, str] = {}
    valid_kinds = {"intake fan", "exhaust fan", "passive vent"}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if line == "AMBIENT DEVICES":
            in_ambient_section = True
            continue
        if line == "INTERNAL DEVICES":
            break
        if not in_ambient_section or not line.startswith("- "):
            continue
        columns = line[2:].split(" | ")
        if len(columns) < 2:
            raise ValueError(f"Malformed ambient device line: {raw_line}")
        name, kind = columns[0].strip(), columns[1].strip().lower()
        if kind not in valid_kinds:
            raise ValueError(
                f"Unsupported ambient device kind {kind!r} for {name!r}"
            )
        function_object = foam_word(name) + "_mass_flow"
        if function_object in seen_objects:
            raise ValueError(
                "Ambient device names normalize to the same function object: "
                f"{seen_objects[function_object]!r} and {name!r}"
            )
        seen_objects[function_object] = name
        devices.append({
            "device_name": name,
            "device_kind": kind,
            "function_object": function_object,
            "opening": foam_word(name),
        })
    if not in_ambient_section:
        raise ValueError(f"No AMBIENT DEVICES section found in {path}")
    if not devices:
        raise ValueError(f"No ambient devices found in {path}")
    return devices


def _observed_direction(phi: float, tolerance: float) -> str:
    if phi > tolerance:
        return "outward"
    if phi < -tolerance:
        return "inward"
    return "stagnant"


def audit_openings(
    case: Path,
    devices: list[dict[str, str]] | None = None,
    direction_tolerance: float = 0.0,
) -> list[dict[str, float | str | None]]:
    """Report every opening at the latest common result time.

    Intake and exhaust fans are direction-gated only when ``devices`` is
    supplied. Passive vents and unclassified openings remain visible without
    imposing a direction requirement.
    """
    if direction_tolerance < 0.0:
        raise ValueError("Direction tolerance must be non-negative")
    observations = opening_observations(case)
    if not observations:
        return []
    latest_time = max(float(row["time_s"]) for row in observations)
    time_tolerance = 1.0e-12 * max(1.0, abs(latest_time))
    latest_by_object: dict[str, dict[str, float | str]] = {}
    for row in observations:
        if abs(float(row["time_s"]) - latest_time) > time_tolerance:
            continue
        key = str(row["function_object"])
        if key in latest_by_object:
            raise ValueError(
                f"Duplicate latest opening result for {key} at t={latest_time:g}"
            )
        latest_by_object[key] = row

    manifest = devices or []
    manifest_by_object = {row["function_object"]: row for row in manifest}
    ordered_objects = [row["function_object"] for row in manifest]
    ordered_objects.extend(
        key for key in sorted(latest_by_object) if key not in manifest_by_object
    )
    if not manifest:
        ordered_objects = sorted(latest_by_object)

    rows = []
    for function_object in ordered_objects:
        expected = manifest_by_object.get(function_object)
        observed = latest_by_object.get(function_object)
        kind = expected["device_kind"] if expected else "unclassified"
        phi = float(observed["phi_kg_s"]) if observed else None
        measurement_status = "MEASURED" if observed else "MISSING"
        observed_direction = (
            _observed_direction(phi, direction_tolerance)
            if phi is not None else "missing"
        )
        if kind == "intake fan":
            expected_direction = "inward"
            direction_status = (
                "PASS" if phi is not None and phi < -direction_tolerance
                else "MISSING" if phi is None else "FAIL"
            )
        elif kind == "exhaust fan":
            expected_direction = "outward"
            direction_status = (
                "PASS" if phi is not None and phi > direction_tolerance
                else "MISSING" if phi is None else "FAIL"
            )
        elif kind == "passive vent":
            expected_direction = "not gated"
            direction_status = "NOT_GATED"
        else:
            expected_direction = "unclassified"
            direction_status = "UNCLASSIFIED"
        rows.append({
            "time_s": latest_time,
            "time_directory": (
                str(observed["time_directory"]) if observed else ""
            ),
            "function_object": function_object,
            "opening": (
                expected["opening"] if expected
                else str(observed["opening"])
            ),
            "device_name": expected["device_name"] if expected else "",
            "device_kind": kind,
            "phi_kg_s": phi,
            "expected_direction": expected_direction,
            "observed_direction": observed_direction,
            "measurement_status": measurement_status,
            "direction_status": direction_status,
        })
    return rows


def direction_failures(
    rows: list[dict[str, float | str | None]],
) -> list[dict[str, float | str | None]]:
    """Return direction-gated fan rows that did not pass."""
    return [
        row for row in rows
        if row["device_kind"] in ("intake fan", "exhaust fan")
        and row["direction_status"] != "PASS"
    ]


def measurement_failures(
    rows: list[dict[str, float | str | None]],
) -> list[dict[str, float | str | None]]:
    """Return manifest devices missing at the audited latest time."""
    return [
        row for row in rows
        if row["device_kind"] != "unclassified"
        and row["measurement_status"] != "MEASURED"
    ]


def write_csv(path: Path, rows: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("case", type=Path)
    parser.add_argument("--csv", type=Path)
    parser.add_argument(
        "--openings-csv", type=Path,
        help="optional latest per-opening mass-flow and direction CSV",
    )
    parser.add_argument(
        "--airflow-devices", nargs="?", type=Path,
        const=Path("airflow_devices.txt"),
        help=("enable ambient fan direction gates using the given report; "
              "without a path, use CASE/airflow_devices.txt"),
    )
    parser.add_argument(
        "--direction-tolerance", type=float, default=0.0,
        help="minimum absolute phi [kg/s] required for a fan direction pass",
    )
    parser.add_argument(
        "--density", type=float,
        help="ambient density [kg/m3] for cumulative air-exchange accounting",
    )
    parser.add_argument(
        "--fluid-volume", type=float,
        help="connected fluid volume [m3] for cumulative air-exchange accounting",
    )
    parser.add_argument("--tolerance", type=float, default=0.01)
    args = parser.parse_args(argv)
    rows = audit(args.case)
    if not rows:
        raise SystemExit("No ambient-opening mass-flow results found")
    if (args.density is None) != (args.fluid_volume is None):
        parser.error("--density and --fluid-volume must be supplied together")
    if args.density is not None:
        try:
            rows = add_exchange_metrics(
                rows, args.density, args.fluid_volume
            )
        except ValueError as error:
            parser.error(str(error))
    if args.csv:
        write_csv(args.csv, rows)
    print("time_s,outflow_kg_s,inflow_kg_s,net_kg_s,mismatch_percent,status")
    for row in rows:
        mismatch = float(row["mismatch_fraction"])
        print(
            f'{row["time_s"]:.9g},{row["outflow_kg_s"]:.9g},'
            f'{row["inflow_kg_s"]:.9g},{row["net_kg_s"]:.9g},'
            f'{100*mismatch:.6g},{"PASS" if mismatch <= args.tolerance else "FAIL"}'
        )
    if args.density is not None:
        latest = rows[-1]
        print(
            "Cumulative air exchange: "
            f"volume={latest['cumulative_exchanged_volume_m3']:.9g} m3, "
            f"fraction={latest['cumulative_exchange_fraction']:.9g}, "
            "projectedOneExchangeTime="
            f"{latest['projected_one_exchange_time_s']:.9g} s"
        )
    mass_passed = float(rows[-1]["mismatch_fraction"]) <= args.tolerance
    devices = None
    if args.airflow_devices is not None:
        device_path = args.airflow_devices
        if not device_path.is_absolute():
            device_path = args.case / device_path
        try:
            devices = read_ambient_devices(device_path)
        except (OSError, ValueError) as error:
            raise SystemExit(str(error)) from error
    try:
        opening_rows = audit_openings(
            args.case, devices, args.direction_tolerance
        )
    except ValueError as error:
        raise SystemExit(str(error)) from error
    if args.openings_csv:
        if not opening_rows:
            raise SystemExit("No per-opening mass-flow results found")
        write_csv(args.openings_csv, opening_rows)

    device_passed = True
    if devices is not None:
        failures = direction_failures(opening_rows)
        missing = measurement_failures(opening_rows)
        gated = sum(
            row["device_kind"] in ("intake fan", "exhaust fan")
            for row in opening_rows
        )
        passive = sum(
            row["device_kind"] == "passive vent" for row in opening_rows
        )
        print(
            "ambient_device,direction_kind,phi_kg_s,observed_direction,status"
        )
        for row in opening_rows:
            if row["device_kind"] == "unclassified":
                continue
            phi = "" if row["phi_kg_s"] is None else f'{row["phi_kg_s"]:.9g}'
            print(
                f'{row["device_name"]},{row["device_kind"]},{phi},'
                f'{row["observed_direction"]},{row["direction_status"]}'
            )
        device_passed = not failures and not missing
        print(
            f"Ambient device direction audit: "
            f"{'PASS' if device_passed else 'FAIL'} "
            f"({gated - len(failures)}/{gated} fans passed; "
            f"{passive} passive vents reported, not direction-gated; "
            f"{len(missing)} manifest measurements missing)"
        )
    return 0 if mass_passed and device_passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
