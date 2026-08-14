"""Plot an OpenFOAM boundary mass-flow report without changing its sign."""

from __future__ import annotations

import argparse
from pathlib import Path

from plot.run_metadata import resolve_case

from tools.validate_openfoam_case import latest_result_paths, patch_values


def report_directories(case: Path) -> list[Path]:
    post = case / "postProcessing"
    if not post.is_dir():
        return []
    return sorted(
        path for path in post.glob("**/*mass_flow") if path.is_dir()
    )


def select_report(case: Path, requested: str | None) -> Path:
    available = report_directories(case)
    if requested:
        normalized = requested.replace("\\", "/").strip("/")
        direct = case / "postProcessing" / Path(normalized)
        if direct.is_dir():
            return direct
        matches = [path for path in available if path.name == normalized]
        if len(matches) == 1:
            return matches[0]
        if len(matches) > 1:
            raise SystemExit(
                f"Report name {requested!r} is ambiguous; provide its path below "
                "postProcessing."
            )
    else:
        outlets = [path for path in available if "outlet" in path.name.lower()]
        if len(outlets) == 1:
            return outlets[0]

    listing = "\n  ".join(
        str(path.relative_to(case / "postProcessing")) for path in available
    ) or "(none)"
    raise SystemExit(
        "Could not select one outlet mass-flow report. Use --report with one "
        f"of:\n  {listing}"
    )


def read_samples(report: Path) -> tuple[list[float], list[float]]:
    samples: dict[float, float] = {}
    for path in report.glob("*/surfaceFieldValue*.dat"):
        with path.open("r", encoding="utf-8", errors="replace") as stream:
            for line in stream:
                columns = line.replace("(", " ").replace(")", " ").split()
                if not columns or columns[0].startswith("#"):
                    continue
                try:
                    samples[float(columns[0])] = float(columns[1])
                except (ValueError, IndexError):
                    continue
    if not samples:
        raise SystemExit(f"No readable flow samples found below {report}")
    times = sorted(samples)
    return times, [samples[time] for time in times]


def latest_direct_flow(case: Path, patch: str) -> tuple[float, float]:
    """Sum signed boundary phi over the latest complete result on every rank."""
    time_s, result_paths = latest_result_paths(case)
    flow = sum(
        sum(patch_values(result_path / "fluid" / "phi", patch))
        for result_path in result_paths
    )
    return time_s, flow


def append_newer_direct_sample(
    case: Path, patch: str, times: list[float], flows: list[float]
) -> bool:
    try:
        direct_time, direct_flow = latest_direct_flow(case, patch)
    except (FileNotFoundError, ValueError):
        return False
    tolerance = 1.0e-8 * max(1.0, abs(direct_time))
    if times and direct_time <= times[-1] + tolerance:
        return False
    times.append(direct_time)
    flows.append(direct_flow)
    return True


def main() -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise SystemExit(
            "This tool requires Matplotlib. Install it with:\n"
            "  python -m pip install matplotlib"
        ) from exc

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", type=Path,
                        help="OpenFOAM case; defaults to the last model_runner export")
    parser.add_argument(
        "--report",
        help=(
            "Report name or path below postProcessing, for example "
            "fluid/Validation_outlet_mass_flow"
        ),
    )
    parser.add_argument("--rho", type=float, default=1.225)
    parser.add_argument("--expected-kg-s", type=float)
    args = parser.parse_args()

    try:
        case = resolve_case(args.case)
    except (FileNotFoundError, ValueError) as exc:
        raise SystemExit(str(exc)) from exc
    report = select_report(case, args.report)
    times, mass_flow = read_samples(report)
    patch = report.name.removesuffix("_mass_flow")
    appended_direct = append_newer_direct_sample(case, patch, times, mass_flow)
    volume_flow = [value / args.rho for value in mass_flow]
    cfm = [value * 2118.880003 for value in volume_flow]

    print(f"Report:                    {report}")
    print(f"Latest result time:         {times[-1]:.6g} s")
    print(f"Signed mass flow:           {mass_flow[-1]:.8g} kg/s")
    print(f"Signed volume flow:         {volume_flow[-1]:.8g} m^3/s")
    print(f"Signed flow:                {cfm[-1]:.4f} CFM")
    print("Sign convention:            positive = out of domain")
    if appended_direct:
        print("Latest sample source:        direct all-rank OpenFOAM phi")

    fig, left = plt.subplots(figsize=(9, 5))
    left.plot(times, mass_flow, color="tab:blue", linewidth=2)
    if args.expected_kg_s is not None:
        left.axhline(
            args.expected_kg_s, color="black", linestyle="--", linewidth=1,
            label=f"Expected: {args.expected_kg_s:g} kg/s",
        )
        left.legend()
    left.axhline(0.0, color="gray", linewidth=0.8)
    left.set_xlabel("Simulation time (s)")
    left.set_ylabel("Signed mass flow (kg/s)", color="tab:blue")
    left.tick_params(axis="y", labelcolor="tab:blue")
    left.grid(True, alpha=0.3)

    right = left.twinx()
    right.plot(times, cfm, color="tab:orange", linewidth=1.5, alpha=0.8)
    right.set_ylabel("Signed flow (CFM)", color="tab:orange")
    right.tick_params(axis="y", labelcolor="tab:orange")
    plt.title(report.name.replace("_", " ").title())
    fig.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
