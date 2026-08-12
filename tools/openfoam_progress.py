#!/usr/bin/env python3
"""Report progress and ETA for a generated thermal-solver OpenFOAM case."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


TIME_RE = re.compile(r"^Time = ([0-9.eE+-]+)$")
EXECUTION_RE = re.compile(r"^ExecutionTime = ([0-9.eE+-]+) s")
CONTROL_TIME_RE = re.compile(
    r"^\s*(startTime|endTime)\s+([0-9.eE+-]+)\s*;"
)
CONTROL_STEP_RE = re.compile(
    r"^\s*(deltaT|writeInterval)\s+([0-9.eE+-]+)\s*;"
)
WRITE_CONTROL_RE = re.compile(r"^\s*writeControl\s+([A-Za-z]+)\s*;")
COURANT_RE = re.compile(
    r"Region: fluid Courant Number mean: ([0-9.eE+-]+) max: ([0-9.eE+-]+)"
)
CONTINUITY_RE = re.compile(
    r"time step continuity errors \(fluid\):.*cumulative = ([0-9.eE+-]+)"
)
FATAL_SIGNATURES = (
    "--> FOAM FATAL ERROR",
    "Foam::sigFpe::sigHandler",
    "Segmentation fault",
    "MPI_ABORT",
    "Killed",
)
STAGE_MARKER_RE = re.compile(
    r"^[A-Za-z].*: t=[0-9.eE+-]+ -> [0-9.eE+-]+$"
)


def read_samples(log_path: Path) -> list[tuple[float, float]]:
    samples: list[tuple[float, float]] = []
    current_time: float | None = None
    with log_path.open("r", encoding="utf-8", errors="ignore") as stream:
        for raw_line in stream:
            line = raw_line.strip()
            match = TIME_RE.match(line)
            if match:
                current_time = float(match.group(1))
                continue
            match = EXECUTION_RE.match(line)
            if match and current_time is not None:
                samples.append((current_time, float(match.group(1))))
    return samples


def read_health(log_path: Path) -> tuple[float | None, float | None, list[str]]:
    maximum_courant: float | None = None
    cumulative_continuity: float | None = None
    signatures: list[str] = []
    with log_path.open("r", encoding="utf-8", errors="ignore") as stream:
        for line in stream:
            if STAGE_MARKER_RE.match(line.strip()):
                maximum_courant = None
                cumulative_continuity = None
                signatures = []
                continue
            match = COURANT_RE.search(line)
            if match:
                maximum_courant = float(match.group(2))
            match = CONTINUITY_RE.search(line)
            if match:
                cumulative_continuity = float(match.group(1))
            for signature in FATAL_SIGNATURES:
                if signature in line and signature not in signatures:
                    signatures.append(signature)
    return maximum_courant, cumulative_continuity, signatures


def recent_slope(samples: list[tuple[float, float]], window: int) -> float:
    points = samples[-max(2, window) :]
    count = len(points)
    if count < 2:
        raise ValueError("need at least two completed timestep samples")
    sum_x = sum(point[0] for point in points)
    sum_y = sum(point[1] for point in points)
    sum_xx = sum(point[0] * point[0] for point in points)
    sum_xy = sum(point[0] * point[1] for point in points)
    denominator = count * sum_xx - sum_x * sum_x
    if denominator <= 0:
        raise ValueError("simulation time did not advance in the selected window")
    slope = (count * sum_xy - sum_x * sum_y) / denominator
    if slope <= 0:
        raise ValueError("calculated wall-time rate is not positive")
    return slope


def read_control_times(case_directory: Path) -> tuple[float, float]:
    control = case_directory / "system" / "controlDict"
    values: dict[str, float] = {}
    for line in control.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = CONTROL_TIME_RE.match(line)
        if match:
            values[match.group(1)] = float(match.group(2))
    missing = [name for name in ("startTime", "endTime") if name not in values]
    if missing:
        raise ValueError(f"{', '.join(missing)} was not found in {control}")
    return values["startTime"], values["endTime"]


def read_checkpoint_stride(case_directory: Path) -> float | None:
    control = case_directory / "system" / "controlDict"
    values: dict[str, float] = {}
    write_control: str | None = None
    for line in control.read_text(encoding="utf-8", errors="ignore").splitlines():
        control_match = WRITE_CONTROL_RE.match(line)
        if control_match and write_control is None:
            write_control = control_match.group(1)
        match = CONTROL_STEP_RE.match(line)
        if match and match.group(1) not in values:
            values[match.group(1)] = float(match.group(2))
    if "writeInterval" not in values:
        return None
    if write_control == "timeStep":
        if "deltaT" not in values:
            return None
        stride = values["deltaT"] * values["writeInterval"]
    elif write_control in ("runTime", "adjustableRunTime"):
        stride = values["writeInterval"]
    else:
        return None
    return stride if stride > 0 else None


def numeric_directories(path: Path) -> list[float]:
    values: list[float] = []
    if not path.is_dir():
        return values
    for child in path.iterdir():
        if not child.is_dir():
            continue
        try:
            values.append(float(child.name))
        except ValueError:
            pass
    return sorted(values)


def format_duration(seconds: float) -> str:
    seconds = max(0.0, seconds)
    hours, remainder = divmod(int(round(seconds)), 3600)
    minutes, seconds = divmod(remainder, 60)
    return f"{hours:d}h {minutes:02d}m {seconds:02d}s"


def choose_log(case_directory: Path, explicit: Path | None) -> Path:
    if explicit is not None:
        return explicit
    candidates = list(case_directory.glob("*.stdout.log"))
    if not candidates:
        raise FileNotFoundError(
            f"no *.stdout.log files found in {case_directory}; use --log"
        )
    return max(candidates, key=lambda path: path.stat().st_mtime)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path, help="OpenFOAM case directory")
    parser.add_argument("--log", type=Path, help="solver stdout log")
    parser.add_argument(
        "--window",
        type=int,
        default=100,
        help="recent completed timesteps used for the rate fit (default: 100)",
    )
    parser.add_argument("--end-time", type=float, help="override controlDict endTime")
    args = parser.parse_args()

    case_directory = args.case.resolve()
    log_path = choose_log(case_directory, args.log.resolve() if args.log else None)
    samples = read_samples(log_path)
    slope = recent_slope(samples, args.window)
    current_time, execution_time = samples[-1]
    start_time, configured_end_time = read_control_times(case_directory)
    end_time = args.end_time if args.end_time is not None else configured_end_time
    remaining_simulated = max(0.0, end_time - current_time)
    checkpoints = numeric_directories(case_directory / "processor0")
    checkpoint_stride = read_checkpoint_stride(case_directory)
    maximum_courant, cumulative_continuity, fatal_signatures = read_health(log_path)

    print(f"Case: {case_directory}")
    print(f"Log: {log_path}")
    print(f"Simulation: {current_time:.9g} / {end_time:.9g} s")
    stage_span = end_time - start_time
    if stage_span > 0:
        stage_fraction = min(1.0, max(0.0, (current_time - start_time) / stage_span))
        print(f"Stage progress: {100.0 * stage_fraction:.2f}%")
    print(f"Recent rate: {slope:.1f} wall s / simulated s")
    print(f"Solver execution time: {format_duration(execution_time)}")
    print(f"Estimated remaining wall time: {format_duration(remaining_simulated * slope)}")
    if maximum_courant is not None:
        print(f"Latest fluid max Courant: {maximum_courant:.6g}")
    if cumulative_continuity is not None:
        print(f"Latest cumulative continuity error: {cumulative_continuity:.6g}")
    print(
        "Fatal signatures: "
        + (", ".join(fatal_signatures) if fatal_signatures else "none")
    )
    if checkpoints:
        print(
            f"Processor checkpoints: {len(checkpoints)} "
            f"(latest {checkpoints[-1]:.9g} s)"
        )
        if checkpoint_stride is not None:
            next_checkpoint = min(end_time, checkpoints[-1] + checkpoint_stride)
            if next_checkpoint > current_time + 1e-12:
                print(
                    f"Next checkpoint: {next_checkpoint:.9g} s "
                    f"(ETA {format_duration((next_checkpoint-current_time)*slope)})"
                )
    else:
        print("Processor checkpoints: none")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
