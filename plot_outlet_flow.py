"""Plot an OpenFOAM boundary mass-flow report without changing its sign."""

from __future__ import annotations

import argparse
from pathlib import Path


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


def main() -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise SystemExit(
            "This tool requires Matplotlib. Install it with:\n"
            "  python -m pip install matplotlib"
        ) from exc

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", type=Path, required=True)
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

    report = select_report(args.case.resolve(), args.report)
    times, mass_flow = read_samples(report)
    volume_flow = [value / args.rho for value in mass_flow]
    cfm = [value * 2118.880003 for value in volume_flow]

    print(f"Report:                    {report}")
    print(f"Latest result time:         {times[-1]:.6g} s")
    print(f"Signed mass flow:           {mass_flow[-1]:.8g} kg/s")
    print(f"Signed volume flow:         {volume_flow[-1]:.8g} m^3/s")
    print(f"Signed flow:                {cfm[-1]:.4f} CFM")
    print("Sign convention:            positive = out of domain")

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
