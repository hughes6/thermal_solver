"""Graph OpenFOAM boundary-flow reversal and hot-air re-ingestion indicators."""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


def read_report(root: Path) -> dict[float, float]:
    samples: dict[float, float] = {}
    for path in root.glob("*/surfaceFieldValue*.dat"):
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


def exported_heat_watts(case: Path) -> float | None:
    path = case / "constant" / "openfoamExportProperties"
    if not path.is_file():
        return None
    values = [float(value) for value in re.findall(
        r"\bwatts\s+([\d.eE+-]+);",
        path.read_text(encoding="utf-8", errors="replace"),
    )]
    return sum(values) if values else None


def combined_samples(histories, ambient_k: float, cp_air: float = 1005.0):
    all_times = sorted({
        time for fields in histories.values() for time in fields["flow"]
        if time in fields["temperature"]
    })
    rows = []
    for time in all_times:
        intake_mass = exhaust_mass = 0.0
        intake_t_sum = exhaust_t_sum = 0.0
        for fields in histories.values():
            if time not in fields["flow"] or time not in fields["temperature"]:
                continue
            flow = fields["flow"][time]
            temperature = fields["temperature"][time]
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
    parser.add_argument("--output", type=Path, default=Path("recirculation_report.png"))
    parser.add_argument("--save", action="store_true")
    args = parser.parse_args()

    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise SystemExit("Install Matplotlib with: python -m pip install matplotlib") from exc

    case = args.case.expanduser().resolve()
    expected_heat_watts = args.expected_heat_watts
    if expected_heat_watts is None:
        expected_heat_watts = exported_heat_watts(case)
    try:
        histories = boundary_histories(case)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    if not histories:
        raise SystemExit(
            "No paired *_mass_flow and *_mass_weighted_temperature reports found. "
            "Re-export and run the case with the current v2.2 exporter."
        )
    if args.cp_air <= 0.0:
        raise SystemExit("--cp-air must be positive")
    if expected_heat_watts is not None and expected_heat_watts <= 0.0:
        raise SystemExit("--expected-heat-watts must be positive")
    rows = combined_samples(histories, args.ambient_temperature, args.cp_air)
    if not rows:
        raise SystemExit("Boundary reports have no matching time samples")

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

    fig, axes = plt.subplots(4, 1, figsize=(11, 13), sharex=True)
    for patch, fields in sorted(histories.items()):
        times = sorted(fields["flow"])
        axes[0].plot(times, [fields["flow"][t] for t in times], label=patch)
        common = [t for t in times if t in fields["temperature"]]
        axes[1].plot(common, [fields["temperature"][t] for t in common], label=patch)
    axes[0].axhline(0.0, color="black", linewidth=0.8)
    axes[0].set_ylabel("Signed mass flow (kg/s)\n+out / -in")
    axes[0].set_title("Boundary flow direction and reversal")
    axes[0].legend(fontsize=8, ncol=2)
    axes[1].axhline(args.ambient_temperature, color="black", linestyle="--",
                    label="Ambient")
    axes[1].set_ylabel("Mass-weighted T (K)")
    axes[1].set_title("Hot-air ingestion at nominal intakes")
    axes[1].legend(fontsize=8, ncol=2)
    axes[2].plot([row[0] for row in rows], [row[5] for row in rows], color="crimson")
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
    print(f"Latest thermal re-ingestion index: {rows[-1][5]:.6g}")
    print(f"Latest net sensible heat rejection: {rows[-1][6]:.6g} W")
    if expected_heat_watts is not None:
        print("Latest heat-rejection fraction: "
              f"{rows[-1][6] / expected_heat_watts:.6%}")
    print("Note: this temperature index indicates hot intake air but does not identify "
          "which exhaust produced it; source attribution requires a passive tracer.")


if __name__ == "__main__":
    main()
