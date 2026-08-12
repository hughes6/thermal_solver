"""Graph OpenFOAM boundary-flow reversal and hot-air re-ingestion indicators."""

from __future__ import annotations

import argparse
import csv
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.openfoam_field_delta import resolve_time_directory
from tools.validate_openfoam_case import latest_result_paths, patch_values


def solver_postprocess_command(case: Path) -> str:
    """Return a copyable command that loads T and phi before reporting."""
    resolved = case.expanduser().resolve()
    text = resolved.as_posix()
    if resolved.drive:
        drive = resolved.drive.rstrip(":").lower()
        text = f"/mnt/{drive}/{text.split(':', 1)[1].lstrip('/')}"
        prefix = "wsl "
    else:
        prefix = ""
    return (
        f"{prefix}openfoam2606 semiFrozenChtMultiRegionFoam -postProcess "
        f"-case '{text}' -latestTime"
    )


def directional_patch_sample(fluxes, temperatures):
    """Split a patch into face-resolved inflow and outflow traffic."""
    if len(temperatures) == 1:
        temperatures = temperatures * len(fluxes)
    if len(fluxes) != len(temperatures):
        raise ValueError("Patch temperature and flux arrays have different lengths")
    inward = [(-flux, temperature) for flux, temperature
              in zip(fluxes, temperatures) if flux < 0.0]
    outward = [(flux, temperature) for flux, temperature
               in zip(fluxes, temperatures) if flux > 0.0]
    inward_mass = sum(mass for mass, _ in inward)
    outward_mass = sum(mass for mass, _ in outward)
    inward_temperature = (
        sum(mass * temperature for mass, temperature in inward) / inward_mass
        if inward_mass else float("nan")
    )
    outward_temperature = (
        sum(mass * temperature for mass, temperature in outward) / outward_mass
        if outward_mass else float("nan")
    )
    gross = inward_mass + outward_mass
    return (sum(fluxes), inward_mass, outward_mass, inward_temperature,
            outward_temperature,
            min(inward_mass, outward_mass) / gross if gross else 0.0)


def latest_face_resolved_samples(case: Path, patches):
    """Read latest complete boundary faces so bidirectional flow is not hidden."""
    time_s, result_paths = latest_result_paths(case)
    if any(not (path / "fluid" / field).is_file()
           for path in result_paths for field in ("phi", "T")):
        return time_s, {}
    samples = {}
    for patch in patches:
        try:
            fluxes = []
            temperatures = []
            for result_path in result_paths:
                rank_fluxes = patch_values(result_path / "fluid" / "phi", patch)
                rank_temperatures = patch_values(
                    result_path / "fluid" / "T", patch)
                if len(rank_temperatures) == 1:
                    rank_temperatures *= len(rank_fluxes)
                fluxes.extend(rank_fluxes)
                temperatures.extend(rank_temperatures)
            samples[patch] = directional_patch_sample(fluxes, temperatures)
        except ValueError:
            continue
    return time_s, samples


def boundary_patch_names(case: Path) -> list[str]:
    """Return external patch names from the reconstructed fluid mesh."""
    path = case / "constant" / "fluid" / "polyMesh" / "boundary"
    if not path.is_file():
        raise ValueError(f"Fluid boundary dictionary not found: {path}")
    text = path.read_bytes().decode("latin-1", errors="replace")
    return re.findall(
        r"(?m)^\s*([A-Za-z_][A-Za-z0-9_]*)\s*\n\s*\{\s*\n"
        r"\s*type\s+patch\s*;",
        text,
    )


def selected_result_paths(case: Path, requested: float) -> tuple[float, list[Path]]:
    processors = sorted(
        (path for path in case.glob("processor*") if path.is_dir()),
        key=lambda path: int(path.name[9:]),
    )
    if processors:
        paths = [resolve_time_directory(processor, str(requested))
                 for processor in processors]
        return float(paths[0].name), paths
    candidates = []
    for path in case.iterdir():
        if not path.is_dir():
            continue
        try:
            value = float(path.name)
        except ValueError:
            continue
        scale = max(1.0, abs(requested))
        if abs(value - requested) <= 1.0e-9 * scale:
            candidates.append((abs(value - requested), value, path))
    if not candidates:
        raise ValueError(f"No reconstructed checkpoint matches t={requested:g} s")
    _, value, path = min(candidates)
    return value, [path]


def selected_time_path(case: Path, requested: float) -> tuple[float, Path]:
    """Backward-compatible reconstructed checkpoint selector."""
    value, paths = selected_result_paths(case, requested)
    if len(paths) != 1:
        raise ValueError(
            f"Checkpoint t={requested:g} is decomposed across {len(paths)} ranks"
        )
    return value, paths[0]


def checkpoint_face_rows(case: Path, requested_times, ambient_k: float,
                         cp_air: float = 1005.0):
    """Read selected reconstructed face fields without report-history scans."""
    patches = boundary_patch_names(case)
    rows = []
    face_rows = []
    for requested in requested_times:
        time_s, result_paths = selected_result_paths(case, requested)
        if any(not (path / "fluid" / field).is_file()
               for path in result_paths for field in ("phi", "T")):
            raise ValueError(f"Checkpoint t={time_s:g} lacks reconstructed phi or T")
        inward_mass = outward_mass = inward_heat = outward_heat = 0.0
        bidirectional_mass = 0.0
        for patch in patches:
            fluxes = []
            temperatures = []
            for result_path in result_paths:
                rank_fluxes = patch_values(result_path / "fluid" / "phi", patch)
                rank_temperatures = patch_values(
                    result_path / "fluid" / "T", patch)
                if len(rank_temperatures) == 1:
                    rank_temperatures *= len(rank_fluxes)
                fluxes.extend(rank_fluxes)
                temperatures.extend(rank_temperatures)
            sample = directional_patch_sample(fluxes, temperatures)
            face_rows.append((time_s, patch, *sample))
            _, inward, outward, inward_t, outward_t, _ = sample
            if inward and inward_t == inward_t:
                inward_mass += inward
                inward_heat += inward * inward_t
            if outward and outward_t == outward_t:
                outward_mass += outward
                outward_heat += outward * outward_t
            bidirectional_mass += min(inward, outward)
        inward_t = inward_heat / inward_mass if inward_mass else float("nan")
        outward_t = outward_heat / outward_mass if outward_mass else float("nan")
        denominator = outward_t - ambient_k
        reingestion = (
            max(0.0, min(1.0, (inward_t - ambient_k) / denominator))
            if denominator > 1.0e-12 and inward_t == inward_t else float("nan")
        )
        sensible = cp_air * (
            outward_mass * (outward_t - ambient_k)
            - inward_mass * (inward_t - ambient_k))
        gross = inward_mass + outward_mass
        rows.append((time_s, inward_mass, outward_mass, inward_t, outward_t,
                     reingestion, sensible,
                     bidirectional_mass / gross if gross else 0.0))
    return rows, face_rows


def append_latest_checkpoint_row(
    case: Path, rows, ambient_k: float, cp_air: float
) -> bool:
    """Append a current direct-field endpoint when report history is stale."""
    try:
        latest_time, _ = latest_result_paths(case)
        if rows and latest_time <= rows[-1][0] + 1.0e-8 * max(
            1.0, abs(latest_time)
        ):
            return False
        direct_rows, _ = checkpoint_face_rows(
            case, [latest_time], ambient_k, cp_air
        )
    except (FileNotFoundError, ValueError):
        return False
    rows.extend(direct_rows)
    return bool(direct_rows)


def run_checkpoint_report(args, plt, case: Path, expected_heat_watts):
    try:
        rows, face_rows = checkpoint_face_rows(
            case, args.snapshot_times, args.ambient_temperature, args.cp_air)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    csv_path = args.output.with_suffix(".csv")
    with csv_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(("time_s", "intake_kg_s", "exhaust_kg_s", "intake_T_K",
                         "exhaust_T_K", "thermal_reingestion_index",
                         "net_sensible_heat_rejection_W",
                         "bidirectional_mass_fraction", "heat_rejection_fraction"))
        for row in rows:
            fraction = (row[6] / expected_heat_watts
                        if expected_heat_watts is not None else float("nan"))
            writer.writerow((*row, fraction))
    face_csv_path = args.output.with_name(args.output.stem + "_face_flow.csv")
    with face_csv_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(("time_s", "patch", "net_out_kg_s", "inward_kg_s",
                         "outward_kg_s", "inward_T_K", "outward_T_K",
                         "bidirectional_share_of_gross"))
        writer.writerows(face_rows)
    if plt is not None:
        times = [row[0] for row in rows]
        fig, axes = plt.subplots(4, 1, figsize=(11, 13), sharex=True)
        patches = sorted({row[1] for row in face_rows})
        for patch in patches:
            samples = [row for row in face_rows if row[1] == patch]
            axes[0].plot([row[0] for row in samples],
                         [row[2] for row in samples], marker="o", label=patch)
        axes[0].axhline(0.0, color="black", linewidth=0.8)
        axes[0].set_ylabel("Net flow (kg/s)\n+out / -in")
        axes[0].set_title("Selected-checkpoint boundary direction")
        axes[0].legend(fontsize=8, ncol=2)
        axes[1].plot(times, [row[3] for row in rows], marker="o", label="Intake")
        axes[1].plot(times, [row[4] for row in rows], marker="o", label="Exhaust")
        axes[1].axhline(args.ambient_temperature, color="black", linestyle="--",
                        label="Ambient")
        axes[1].set_ylabel("Temperature (K)")
        axes[1].legend(fontsize=8)
        axes[2].plot(times, [row[5] for row in rows], marker="o",
                     label="Thermal re-ingestion")
        axes[2].plot(times, [row[7] for row in rows], marker="o",
                     label="Bidirectional mass fraction")
        axes[2].set_ylabel("Fraction")
        axes[2].legend(fontsize=8)
        axes[3].plot(times, [row[6] for row in rows], marker="o",
                     color="darkorange", label="Net sensible heat")
        if expected_heat_watts is not None:
            axes[3].axhline(expected_heat_watts, color="black", linestyle="--",
                            label=f"Applied heat: {expected_heat_watts:g} W")
        axes[3].set_ylabel("Heat rejection (W)")
        axes[3].set_xlabel("Simulation time (s)")
        axes[3].legend(fontsize=8)
        for axis in axes:
            axis.grid(True, alpha=0.3)
        fig.tight_layout()
        if args.save:
            fig.savefig(args.output, dpi=180)
            print(f"Saved: {args.output}")
        else:
            plt.show()
    print(f"Saved: {csv_path}")
    print(f"Saved: {face_csv_path}")
    print(f"Latest thermal re-ingestion index: {rows[-1][5]:.6g}")
    print(f"Latest bidirectional mass fraction: {rows[-1][7]:.6g}")
    print(f"Latest net sensible heat rejection: {rows[-1][6]:.6g} W")


def read_report(root: Path) -> dict[float, float]:
    samples: dict[float, float] = {}
    for path in root.glob("*/*FieldValue*.dat"):
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            columns = line.replace("(", " ").replace(")", " ").split()
            if not columns or columns[0].startswith("#"):
                continue
            try:
                samples[float(columns[0])] = float(columns[1])
            except (ValueError, IndexError):
                continue
    return samples


def boundary_histories(case: Path) -> dict[str, dict[str, dict[float, float]]]:
    fluid = case / "postProcessing" / "fluid"
    result: dict[str, dict[str, dict[float, float]]] = {}
    suffixes = {
        "_mass_flow": "flow",
        "_mass_weighted_temperature": "temperature",
    }
    if not fluid.is_dir():
        raise ValueError(f"No fluid postProcessing directory found in {case}")
    for directory in fluid.iterdir():
        if not directory.is_dir():
            continue
        for suffix, field in suffixes.items():
            if directory.name.endswith(suffix):
                patch = directory.name[: -len(suffix)]
                result.setdefault(patch, {})[field] = read_report(directory)
    return {
        patch: fields for patch, fields in result.items()
        if fields.get("flow") and fields.get("temperature")
    }


def internal_device_temperature_rows(case: Path, ambient_k: float):
    """Pair adjacent internal intake/exhaust reports and quantify intake heat."""
    fluid = case / "postProcessing" / "fluid"
    metadata = {}
    metadata_path = case / "internal_airflow_devices.csv"
    if metadata_path.is_file():
        with metadata_path.open("r", newline="", encoding="utf-8") as stream:
            for row in csv.DictReader(stream):
                try:
                    row["component_id"] = int(row["component_id"])
                except (KeyError, TypeError, ValueError):
                    continue
                metadata[row.get("zone", "")] = row
    pattern = re.compile(
        r"^internal_(?P<name>.+)_(?P<id>[0-9]+)_temperature_average$"
    )
    devices = []
    if not fluid.is_dir():
        return []
    for directory in fluid.iterdir():
        match = pattern.match(directory.name) if directory.is_dir() else None
        if not match:
            continue
        name = match.group("name")
        normalized = name.lower()
        kind = (
            "exhaust" if "fan" in normalized or "exhaust" in normalized
            else "intake" if "intake" in normalized or "vent" in normalized
            else "unknown"
        )
        devices.append({
            "id": int(match.group("id")),
            "name": name,
            "zone": directory.name.removesuffix("_temperature_average"),
            "kind": kind,
            "temperature": read_report(directory),
        })
        device_metadata = metadata.get(devices[-1]["zone"])
        if device_metadata:
            devices[-1]["kind"] = device_metadata.get("kind", kind)
            devices[-1]["component_id"] = device_metadata["component_id"]
            devices[-1]["component"] = device_metadata.get("component", "")
    devices.sort(key=lambda device: device["id"])
    rows = []
    pairs = []
    if metadata:
        component_ids = sorted({
            device.get("component_id") for device in devices
            if device.get("component_id") is not None
        })
        for component_id in component_ids:
            component_devices = [
                device for device in devices
                if device.get("component_id") == component_id
            ]
            intakes = [d for d in component_devices if d["kind"] == "intake"]
            exhausts = [d for d in component_devices if d["kind"] == "exhaust"]
            if intakes and exhausts:
                pairs.append((intakes[0], exhausts[0]))
    else:
        pairs = list(zip(devices, devices[1:]))
    for intake, exhaust in pairs:
        if intake["kind"] != "intake" or exhaust["kind"] != "exhaust":
            continue
        common_times = sorted(
            set(intake["temperature"]) & set(exhaust["temperature"])
        )
        pair = intake.get("component") or (
            f"internal_pair_{intake['id']}_{exhaust['id']}"
        )
        for time in common_times:
            intake_t = intake["temperature"][time]
            exhaust_t = exhaust["temperature"][time]
            rise = exhaust_t - ambient_k
            index_value = (
                max(0.0, min(1.0, (intake_t - ambient_k) / rise))
                if rise > 1.0e-12 else float("nan")
            )
            rows.append((
                time, pair, intake["name"], exhaust["name"],
                intake_t, exhaust_t, index_value,
            ))
    return rows


def write_internal_device_csv(output: Path, rows, selected_times=None):
    """Write equipment intake/exhaust temperature indicators."""
    if selected_times:
        selected = []
        for row in rows:
            if any(abs(row[0] - requested) <=
                   1.0e-8 * max(1.0, abs(requested))
                   for requested in selected_times):
                selected.append(row)
        rows = selected
    path = output.with_name(output.stem + "_internal_air.csv")
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow((
            "time_s", "pair", "intake_device", "exhaust_device",
            "intake_T_K", "exhaust_T_K", "equipment_air_rise_index",
        ))
        writer.writerows(rows)
    return path, rows


def exported_heat_watts(case: Path) -> float | None:
    path = case / "constant" / "openfoamExportProperties"
    if not path.is_file():
        return None
    values = [float(value) for value in re.findall(
        r"\bwatts\s+([\d.eE+-]+);",
        path.read_text(encoding="utf-8", errors="replace"),
    )]
    return sum(values) if values else None


def boundary_flow_floors(histories, minimum_flow_fraction: float = 1.0e-4):
    """Return a per-time negligible-flow floor scaled to rack throughput."""
    all_times = sorted({
        time for fields in histories.values() for time in fields["flow"]
    })
    return {
        time: 0.5 * minimum_flow_fraction * sum(
            abs(fields["flow"].get(time, 0.0))
            for fields in histories.values()
        )
        for time in all_times
    }


def combined_samples(
    histories,
    ambient_k: float,
    cp_air: float = 1005.0,
    minimum_flow_fraction: float = 1.0e-4,
):
    all_times = sorted({
        time for fields in histories.values() for time in fields["flow"]
        if time in fields["temperature"]
    })
    floors = boundary_flow_floors(histories, minimum_flow_fraction)
    rows = []
    for time in all_times:
        intake_mass = exhaust_mass = 0.0
        intake_t_sum = exhaust_t_sum = 0.0
        for fields in histories.values():
            if time not in fields["flow"] or time not in fields["temperature"]:
                continue
            flow = fields["flow"][time]
            temperature = fields["temperature"][time]
            # A mass-weighted temperature is undefined as flow approaches
            # zero. OpenFOAM can report enormous signed values in that limit;
            # exclude them from both heat balance and re-ingestion metrics.
            if abs(flow) <= floors[time]:
                continue
            if flow < 0.0:
                intake_mass += -flow
                intake_t_sum += -flow * temperature
            elif flow > 0.0:
                exhaust_mass += flow
                exhaust_t_sum += flow * temperature
        intake_t = intake_t_sum / intake_mass if intake_mass else float("nan")
        exhaust_t = exhaust_t_sum / exhaust_mass if exhaust_mass else float("nan")
        denominator = exhaust_t - ambient_k
        index = (
            max(0.0, min(1.0, (intake_t - ambient_k) / denominator))
            if denominator > 1.0e-12 and intake_t == intake_t else float("nan")
        )
        sensible_heat = cp_air * (
            exhaust_mass * (exhaust_t - ambient_k)
            - intake_mass * (intake_t - ambient_k)
        )
        rows.append((time, intake_mass, exhaust_mass, intake_t, exhaust_t,
                     index, sensible_heat))
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", required=True, type=Path)
    parser.add_argument("--ambient-temperature", type=float, default=293.15)
    parser.add_argument("--cp-air", type=float, default=1005.0,
                        help="air specific heat in J/(kg K), default: 1005")
    parser.add_argument("--expected-heat-watts", type=float,
                        help="override exported applied heat for rejection comparison")
    parser.add_argument(
        "--minimum-flow-fraction", type=float, default=1.0e-4,
        help=("ignore boundary temperatures when absolute flow is at or below "
              "this fraction of total one-way rack throughput; default: 0.0001"),
    )
    parser.add_argument("--output", type=Path, default=Path("recirculation_report.png"))
    parser.add_argument(
        "--snapshot-times", nargs="+", type=float,
        help=("read only these reconstructed or rank-common decomposed "
              "checkpoints, bypassing potentially large function-object "
              "histories"),
    )
    parser.add_argument("--save", action="store_true")
    parser.add_argument(
        "--csv-only", action="store_true",
        help="write numerical CSV reports without importing Matplotlib",
    )
    args = parser.parse_args()

    plt = None
    if not args.csv_only:
        try:
            import matplotlib.pyplot as plt
        except ImportError as exc:
            raise SystemExit(
                "Install Matplotlib with: python -m pip install matplotlib, "
                "or use --csv-only"
            ) from exc

    case = args.case.expanduser().resolve()
    expected_heat_watts = args.expected_heat_watts
    if expected_heat_watts is None:
        expected_heat_watts = exported_heat_watts(case)
    if args.cp_air <= 0.0:
        raise SystemExit("--cp-air must be positive")
    if expected_heat_watts is not None and expected_heat_watts <= 0.0:
        raise SystemExit("--expected-heat-watts must be positive")
    if not 0.0 <= args.minimum_flow_fraction < 1.0:
        raise SystemExit("--minimum-flow-fraction must be in [0, 1)")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.snapshot_times:
        run_checkpoint_report(args, plt, case, expected_heat_watts)
        available_internal_rows = internal_device_temperature_rows(
            case, args.ambient_temperature
        )
        internal_path, internal_rows = write_internal_device_csv(
            args.output,
            available_internal_rows,
            args.snapshot_times,
        )
        print(f"Saved: {internal_path}")
        if internal_rows:
            print("Maximum selected equipment air-rise index: "
                  f"{max(row[6] for row in internal_rows):.6g}")
        elif (case / "internal_airflow_devices.csv").is_file():
            available_times = sorted({row[0] for row in available_internal_rows})
            latest_text = (
                f"; latest available internal report is t={available_times[-1]:g} s"
                if available_times else "; no internal temperature reports exist"
            )
            print(
                "WARNING: selected checkpoint boundary results are valid, but "
                "internal equipment temperatures are unavailable at the "
                f"requested time(s){latest_text}. The internal-air CSV contains "
                "only its header. Internal cell-zone temperatures require "
                "solver-backed post-processing; they are not inferred from "
                "boundary or neighboring cells.",
                file=sys.stderr,
            )
        return
    try:
        histories = boundary_histories(case)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    if not histories:
        raise SystemExit(
            "No paired *_mass_flow and *_mass_weighted_temperature reports found. "
            "If this is a short or manually controlled endpoint, generate the "
            "configured reports with solver-backed post-processing (generic "
            "postProcess does not load T and phi), then retry:\n  "
            + solver_postprocess_command(case)
        )
    rows = combined_samples(
        histories, args.ambient_temperature, args.cp_air,
        args.minimum_flow_fraction,
    )
    if not rows:
        raise SystemExit("Boundary reports have no matching time samples")
    appended_direct = append_latest_checkpoint_row(
        case, rows, args.ambient_temperature, args.cp_air
    )

    csv_path = args.output.with_suffix(".csv")
    with csv_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(("time_s", "intake_kg_s", "exhaust_kg_s", "intake_T_K",
                         "exhaust_T_K", "thermal_reingestion_index",
                         "net_sensible_heat_rejection_W",
                         "heat_rejection_fraction"))
        for row in rows:
            fraction = (row[6] / expected_heat_watts
                        if expected_heat_watts is not None else float("nan"))
            writer.writerow((*row, fraction))

    snapshot_time, face_samples = latest_face_resolved_samples(
        case, histories.keys())
    face_csv_path = args.output.with_name(
        args.output.stem + "_latest_face_flow.csv")
    with face_csv_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(("time_s", "patch", "net_out_kg_s", "inward_kg_s",
                         "outward_kg_s", "inward_T_K", "outward_T_K",
                         "bidirectional_share_of_gross"))
        for patch, sample in sorted(face_samples.items()):
            writer.writerow((snapshot_time, patch, *sample))

    internal_path, internal_rows = write_internal_device_csv(
        args.output,
        internal_device_temperature_rows(case, args.ambient_temperature),
    )

    flow_floors = boundary_flow_floors(
        histories, args.minimum_flow_fraction)
    ignored_temperature_samples = 0
    for fields in histories.values():
        for time in fields["flow"]:
            if (time in fields["temperature"] and
                    abs(fields["flow"][time]) <= flow_floors[time]):
                ignored_temperature_samples += 1
    if plt is not None:
        fig, axes = plt.subplots(4, 1, figsize=(11, 13), sharex=True)
        for patch, fields in sorted(histories.items()):
            times = sorted(fields["flow"])
            axes[0].plot(times, [fields["flow"][t] for t in times], label=patch)
            common = [t for t in times if t in fields["temperature"]]
            temperatures = [
                (float("nan") if abs(fields["flow"][time]) <= flow_floors[time]
                 else fields["temperature"][time])
                for time in common
            ]
            axes[1].plot(common, temperatures, label=patch)
        axes[0].axhline(0.0, color="black", linewidth=0.8)
        axes[0].set_ylabel("Signed mass flow (kg/s)\n+out / -in")
        axes[0].set_title("Boundary flow direction and reversal")
        axes[0].legend(fontsize=8, ncol=2)
        axes[1].axhline(args.ambient_temperature, color="black", linestyle="--",
                        label="Ambient")
        axes[1].set_ylabel("Mass-weighted T (K)")
        axes[1].set_title(
            "Boundary mass-weighted temperature (near-zero flow omitted)")
        axes[1].legend(fontsize=8, ncol=2)
        axes[2].plot([row[0] for row in rows], [row[5] for row in rows],
                     color="crimson")
        axes[2].set_ylim(-0.02, 1.02)
        axes[2].set_ylabel("Thermal re-ingestion index")
        axes[2].set_title("0 = ambient intake; 1 = exhaust-temperature intake")
        axes[3].plot([row[0] for row in rows], [row[6] for row in rows],
                     color="darkorange", label="Net boundary sensible heat")
        if expected_heat_watts is not None:
            axes[3].axhline(expected_heat_watts, color="black", linestyle="--",
                            label=f"Applied heat: {expected_heat_watts:g} W")
            axes[3].legend(fontsize=8)
        axes[3].set_ylabel("Heat rejection (W)")
        axes[3].set_xlabel("Simulation time (s)")
        axes[3].set_title("Net sensible heat rejected relative to ambient")
        for axis in axes:
            axis.grid(True, alpha=0.3)
        fig.tight_layout()
        if args.save:
            fig.savefig(args.output, dpi=180)
            print(f"Saved: {args.output}")
        else:
            plt.show()
    print(f"Saved: {csv_path}")
    print(f"Saved: {face_csv_path}")
    print(f"Saved: {internal_path}")
    print(f"Latest thermal re-ingestion index: {rows[-1][5]:.6g}")
    print(f"Latest net sensible heat rejection: {rows[-1][6]:.6g} W")
    if appended_direct:
        print("Latest external endpoint source: direct all-rank T and phi")
    print("Ignored undefined near-zero-flow boundary-temperature samples: "
          f"{ignored_temperature_samples}")
    if expected_heat_watts is not None:
        print("Latest heat-rejection fraction: "
              f"{rows[-1][6] / expected_heat_watts:.6%}")
    if internal_rows:
        latest_internal_time = max(row[0] for row in internal_rows)
        latest_internal = [
            row for row in internal_rows if row[0] == latest_internal_time
        ]
        print("Latest maximum equipment air-rise index: "
              f"{max(row[6] for row in latest_internal):.6g}")
    print("Note: this temperature index indicates hot intake air but does not identify "
          "which exhaust produced it; source attribution requires a passive tracer.")


if __name__ == "__main__":
    main()
