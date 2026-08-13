#!/usr/bin/env python3
"""Report progress and ETA for a generated thermal-solver OpenFOAM case."""

from __future__ import annotations

import argparse
import math
import re
import shutil
import time
from pathlib import Path


TIME_RE = re.compile(r"^Time = ([0-9.eE+-]+)$")
EXECUTION_RE = re.compile(
    r"^ExecutionTime = ([0-9.eE+-]+) s(?:\s+ClockTime = ([0-9.eE+-]+) s)?"
)
CONTROL_TIME_RE = re.compile(
    r"^\s*(startTime|endTime)\s+([0-9.eE+-]+)\s*;"
)
CONTROL_STEP_RE = re.compile(
    r"^\s*(deltaT|maxCo|maxDeltaT|writeInterval)\s+([0-9.eE+-]+)\s*;"
)
WRITE_CONTROL_RE = re.compile(r"^\s*writeControl\s+([A-Za-z]+)\s*;")
THERMAL_ONLY_RE = re.compile(
    r"^\s*thermalOnlyFlow\s+(true|false)\s*;", re.IGNORECASE
)
COURANT_RE = re.compile(
    r"Region: fluid Courant Number mean: ([0-9.eE+-]+) max: ([0-9.eE+-]+)"
)
CONTINUITY_RE = re.compile(
    r"time step continuity errors \(fluid\):.*cumulative = ([0-9.eE+-]+)"
)
SOLID_REGION_RE = re.compile(r"^Solving for solid region (\S+)$")
FLUID_REGION_RE = re.compile(r"^Solving (?:thermal-only )?fluid region (\S+)$")
TEMPERATURE_RANGE_RE = re.compile(
    r"^Min/max T:([0-9.eE+-]+)\s+([0-9.eE+-]+)$"
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
RUN_REQUEST_RE = re.compile(
    r"\|\s+run_start\s+mode=(\S+)\s+processes=\d+\s+requestedEnd=([0-9.eE+-]+)"
)
INITIAL_EXCHANGE_RE = re.compile(
    r"\|\s+initial_air_exchange_advance\s+current=([0-9.eE+-]+)\s+"
    r"target=([0-9.eE+-]+)\s+requiredElapsed=([0-9.eE+-]+)"
)
INITIAL_EXCHANGE_FRACTION_RE = re.compile(
    r"\|\s+initial_air_exchange_advance\s+current=([0-9.eE+-]+)\s+"
    r"target=([0-9.eE+-]+)\s+completedFraction=([0-9.eE+-]+)"
)
THERMAL_METRICS_RE = re.compile(
    r"\|\s+thermal\s+time=([0-9.eE+-]+)\s+"
    r"maxInternalCellChange=([0-9.eE+-]+)\s+"
    r"maxComponentAverageChange=([0-9.eE+-]+)\s+"
    r"controllingPeakRegion=(\S+)\s+controllingAverageRegion=(\S+)\s+"
    r"elapsed=([0-9.eE+-]+)"
)
THERMAL_PEAK_LIMIT_RE = re.compile(
    r'if ! awk -v v="\$scaled_delta" -v limit="([0-9.eE+-]+)"'
)
THERMAL_AVERAGE_LIMIT_RE = re.compile(
    r'if ! awk -v v="\$scaled_average_delta" -v limit="([0-9.eE+-]+)"'
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
                logged_wall_time = match.group(2) or match.group(1)
                samples.append((current_time, float(logged_wall_time)))
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


def read_latest_temperature_ranges(log_path: Path) -> dict[str, tuple[float, float]]:
    """Read region temperature ranges from the latest completed timestep."""
    current_region: str | None = None
    current_ranges: dict[str, tuple[float, float]] = {}
    completed_ranges: dict[str, tuple[float, float]] = {}
    with log_path.open("r", encoding="utf-8", errors="ignore") as stream:
        for raw_line in stream:
            line = raw_line.strip()
            if TIME_RE.match(line):
                current_ranges = {}
                current_region = None
                continue
            match = SOLID_REGION_RE.match(line) or FLUID_REGION_RE.match(line)
            if match:
                current_region = match.group(1)
                continue
            match = TEMPERATURE_RANGE_RE.match(line)
            if match and current_region is not None:
                current_ranges[current_region] = (
                    float(match.group(1)), float(match.group(2))
                )
                continue
            if EXECUTION_RE.match(line) and current_ranges:
                completed_ranges = dict(current_ranges)
    return completed_ranges


def recent_slope(samples: list[tuple[float, float]], window: int) -> float:
    if len(samples) < 2:
        raise ValueError("need at least two completed timestep samples")
    segment_start = 0
    for index in range(1, len(samples)):
        if samples[index][1] < samples[index - 1][1]:
            segment_start = index
    points = samples[segment_start:][-max(2, window) :]
    count = len(points)
    if count < 2:
        raise ValueError(
            "need at least two completed timestep samples in the current "
            "solver clock segment"
        )
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


def recent_slope_or_none(
    samples: list[tuple[float, float]], window: int
) -> float | None:
    try:
        return recent_slope(samples, window)
    except ValueError:
        return None


def current_stage_samples(
    samples: list[tuple[float, float]], start_time: float
) -> list[tuple[float, float]]:
    tolerance = 1.0e-9 * max(1.0, abs(start_time))
    return [sample for sample in samples if sample[0] > start_time + tolerance]


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


def read_thermal_only_flow(case_directory: Path) -> bool:
    solution = case_directory / "system" / "fluid" / "fvSolution"
    if not solution.is_file():
        return False
    for line in solution.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = THERMAL_ONLY_RE.match(line)
        if match:
            return match.group(1).lower() == "true"
    return False


def courant_timestep_headroom(
    case_directory: Path,
    observed_maximum_courant: float,
    safety_fraction: float = 0.8,
) -> tuple[float, float, float, float] | None:
    """Estimate a safe diagnostic dt without changing the configured stage."""
    if observed_maximum_courant <= 0 or not 0 < safety_fraction <= 1:
        return None
    control = case_directory / "system" / "controlDict"
    values: dict[str, float] = {}
    for line in control.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = CONTROL_STEP_RE.match(line)
        if match and match.group(1) in ("deltaT", "maxCo", "maxDeltaT"):
            values[match.group(1)] = float(match.group(2))
    if any(name not in values for name in ("deltaT", "maxCo", "maxDeltaT")):
        return None
    current = values["deltaT"]
    if current <= 0 or values["maxCo"] <= 0:
        return None
    safe = current * values["maxCo"] / observed_maximum_courant * safety_fraction
    return current, values["maxDeltaT"], safe, safe / current


def read_latest_run_request(case_directory: Path) -> tuple[str, float] | None:
    summary = case_directory / "run_summary.log"
    if not summary.is_file():
        return None
    latest: tuple[str, float] | None = None
    for line in summary.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = RUN_REQUEST_RE.search(line)
        if match:
            latest = match.group(1), float(match.group(2))
    return latest


def read_latest_thermal_metrics(case_directory: Path):
    """Return latest normalized thermal rates and generated-runner limits."""
    summary = case_directory / "run_summary.log"
    if not summary.is_file():
        return None
    latest = None
    for line in summary.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = THERMAL_METRICS_RE.search(line)
        if match:
            latest = (
                float(match.group(1)), float(match.group(2)),
                float(match.group(3)), match.group(4), match.group(5),
                float(match.group(6)),
            )
    if latest is None:
        return None
    peak_limit = average_limit = None
    runner = case_directory / "run_parallel.sh"
    if runner.is_file():
        runner_text = runner.read_text(encoding="utf-8", errors="ignore")
        match = THERMAL_PEAK_LIMIT_RE.search(runner_text)
        if match:
            peak_limit = float(match.group(1))
        match = THERMAL_AVERAGE_LIMIT_RE.search(runner_text)
        if match:
            average_limit = float(match.group(1))
    return latest + (peak_limit, average_limit)


def read_initial_airflow_progress(
    case_directory: Path,
) -> tuple[float, float | None, float | None, float | None] | None:
    """Read the restart-safe initial-airflow observation and exchange target."""
    if (case_directory / ".initial_airflow_converged").is_file():
        return None
    pending = case_directory / ".initial_airflow_pending"
    if not pending.is_file():
        return None
    try:
        observation_start = float(
            pending.read_text(encoding="utf-8", errors="ignore").split()[0]
        )
    except (IndexError, ValueError, OSError):
        return None
    latest_target: float | None = None
    required_elapsed: float | None = None
    completed_fraction: float | None = None
    summary = case_directory / "run_summary.log"
    if summary.is_file():
        for line in summary.read_text(
            encoding="utf-8", errors="ignore"
        ).splitlines():
            match = INITIAL_EXCHANGE_RE.search(line)
            if match:
                latest_target = float(match.group(2))
                required_elapsed = float(match.group(3))
                completed_fraction = None
                continue
            match = INITIAL_EXCHANGE_FRACTION_RE.search(line)
            if match:
                latest_target = float(match.group(2))
                required_elapsed = None
                completed_fraction = float(match.group(3))
    return observation_start, latest_target, required_elapsed, completed_fraction


def format_initial_airflow_stage(
    progress: tuple[float, float | None, float | None, float | None],
    current_time: float,
) -> str:
    observation_start, exchange_target, required_elapsed, completed_fraction = progress
    tolerance = 1.0e-9 * max(1.0, abs(current_time))
    target_reached = (
        exchange_target is not None
        and current_time >= exchange_target - tolerance
    )
    if exchange_target is not None and required_elapsed is not None:
        detail = (
            "initial airflow physical exchange "
            f"(observation start {observation_start:.9g} s, required elapsed "
            f"{required_elapsed:.9g} s, target {exchange_target:.9g} s)"
        )
    elif exchange_target is not None and completed_fraction is not None:
        detail = (
            "initial airflow cumulative physical exchange "
            f"(observation start {observation_start:.9g} s, completed fraction "
            f"{completed_fraction:.6g}, next check {exchange_target:.9g} s)"
        )
    else:
        return (
            "Workflow stage: adaptive initial airflow observation "
            f"(started {observation_start:.9g} s)"
        )
    suffix = (
        "; exchange target reached, convergence recheck or continued settling pending"
        if target_reached else ""
    )
    return f"Workflow stage: {detail}{suffix}"


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


def processor_checkpoints(
    case_directory: Path,
    maximum_completed_time: float | None = None,
) -> tuple[list[float], int, bool, list[int], list[float]]:
    """Summarize decomposed checkpoints and consistency across MPI ranks."""
    processors = sorted(
        (path for path in case_directory.glob("processor*") if path.is_dir()),
        key=lambda path: (
            int(path.name[9:]) if path.name[9:].isdigit() else 10**9,
            path.name,
        ),
    )
    if not processors:
        return [], 0, True, [], []
    raw_time_sets = [numeric_directories(processor) for processor in processors]

    def eligible(value: float) -> bool:
        if maximum_completed_time is None:
            return True
        # OpenFOAM directory names can land a few ulps beyond a nominal stage
        # endpoint (for example 4800.0000000000045). Restart-field validation
        # below, rather than strict timestamp ordering, establishes completion.
        tolerance = 1.0e-9 * max(1.0, abs(maximum_completed_time))
        return value <= maximum_completed_time + tolerance

    time_sets = [
        [value for value in times if eligible(value)] for times in raw_time_sets
    ]
    common_times = sorted(set.intersection(*(set(times) for times in time_sets)))
    manifests_by_time = {}
    for value in common_times:
        manifests = []
        for processor in processors:
            directory = next(
                child for child in processor.iterdir()
                if child.is_dir() and _is_float(child.name)
                and float(child.name) == value
            )
            manifests.append({
                child.relative_to(directory).as_posix()
                for child in directory.rglob("*") if child.is_file()
            })
        manifests_by_time[value] = manifests
    def restartable(manifest: set[str]) -> bool:
        basenames = {Path(name).name for name in manifest}
        return {"T", "U"}.issubset(basenames)

    complete_times = [
        value for value, manifests in manifests_by_time.items()
        if manifests and restartable(manifests[0])
        and all(item == manifests[0] for item in manifests[1:])
    ]
    restart_time_sets = []
    for processor, times in zip(processors, time_sets):
        restart_times = []
        for value in times:
            directory = next(
                child for child in processor.iterdir()
                if child.is_dir() and _is_float(child.name)
                and float(child.name) == value
            )
            manifest = {
                child.relative_to(directory).as_posix()
                for child in directory.rglob("*") if child.is_file()
            }
            if restartable(manifest):
                restart_times.append(value)
        restart_time_sets.append(restart_times)
    restart_times_aligned = all(
        times == restart_time_sets[0] for times in restart_time_sets[1:]
    )
    all_times = sorted(set().union(*(set(times) for times in raw_time_sets)))
    incomplete_newer = [
        value for value in all_times if value not in complete_times
    ]
    inspected_time = all_times[-1] if all_times else None
    latest_file_counts = []
    if inspected_time is not None:
        for processor, times in zip(processors, raw_time_sets):
            if inspected_time not in times:
                latest_file_counts.append(0)
                continue
            directory = next(
                child for child in processor.iterdir()
                if child.is_dir() and _is_float(child.name)
                and float(child.name) == inspected_time
            )
            latest_file_counts.append(sum(
                1 for child in directory.rglob("*") if child.is_file()
            ))
    common_restart_times = sorted(
        set.intersection(*(set(times) for times in restart_time_sets))
    )
    restart_manifests_aligned = (
        len(complete_times) == len(common_restart_times)
    )
    common_manifests_aligned = all(
        manifests and all(item == manifests[0] for item in manifests[1:])
        for manifests in manifests_by_time.values()
    )
    state_aligned = (
        restart_times_aligned
        and restart_manifests_aligned
        and common_manifests_aligned
    )
    return (
        complete_times,
        len(processors),
        state_aligned,
        latest_file_counts,
        incomplete_newer,
    )


def current_checkpoint_series_count(checkpoints, stride: float | None) -> int:
    """Count the cadence-aligned tail, excluding writes from prior stages."""
    if not checkpoints:
        return 0
    if stride is None or not math.isfinite(stride) or stride <= 0.0:
        return len(checkpoints)
    count = 1
    for earlier, later in zip(reversed(checkpoints[:-1]), reversed(checkpoints[1:])):
        tolerance = 1.0e-6 * max(1.0, abs(stride), abs(later))
        if abs((later - earlier) - stride) > tolerance:
            break
        count += 1
    return count


def _is_float(value: str) -> bool:
    try:
        float(value)
    except ValueError:
        return False
    return True


def format_duration(seconds: float) -> str:
    seconds = max(0.0, seconds)
    hours, remainder = divmod(int(round(seconds)), 3600)
    minutes, seconds = divmod(remainder, 60)
    return f"{hours:d}h {minutes:02d}m {seconds:02d}s"


def directory_size(path: Path) -> int:
    """Return the allocated file payload below path, tolerating live-file races."""
    total = 0
    for child in path.rglob("*"):
        try:
            if child.is_file():
                total += child.stat().st_size
        except OSError:
            # A rolling checkpoint may be purged while this report is running.
            pass
    return total


def format_bytes(byte_count: int) -> str:
    return f"{byte_count / (1024 ** 3):.2f} GiB"


def storage_usage(path: Path, fast: bool = False) -> tuple[int | None, int]:
    """Return case bytes and free bytes, optionally avoiding the tree walk."""
    case_bytes = None if fast else directory_size(path)
    return case_bytes, shutil.disk_usage(path).free


def low_space_warning(free_bytes: int, minimum_free_gib: float) -> str | None:
    minimum_free_bytes = minimum_free_gib * 1024 ** 3
    if free_bytes >= minimum_free_bytes:
        return None
    return (
        "WARNING: low free space for continued checkpoint writes: "
        f"{format_bytes(free_bytes)} available, configured warning "
        f"threshold {minimum_free_gib:.3g} GiB"
    )


def temperature_warning(
    temperature_k: float, maximum_temperature_c: float
) -> str | None:
    temperature_c = temperature_k - 273.15
    if temperature_c > maximum_temperature_c:
        return (
            "WARNING: latest region maximum exceeds the diagnostic "
            f"temperature threshold of {maximum_temperature_c:g} C; check "
            "component airflow, heat load, and material calibration"
        )
    return None


def is_stale_run(
    log_age_seconds: float,
    current_time: float,
    end_time: float,
    stale_after_seconds: float,
) -> bool:
    return current_time < end_time and log_age_seconds > stale_after_seconds


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
    parser.add_argument(
        "--stale-after",
        type=float,
        default=300.0,
        help="warn when an incomplete run log is older than this many seconds",
    )
    parser.add_argument(
        "--fast",
        action="store_true",
        help=("skip the recursive case-size count; retain solver health, ETA, "
              "free disk, workflow stage, and checkpoint validation"),
    )
    parser.add_argument(
        "--minimum-free-gib",
        type=float,
        default=5.0,
        help="warn below this free-space threshold in GiB (default: 5)",
    )
    parser.add_argument(
        "--maximum-temperature-c",
        type=float,
        default=150.0,
        help=("warn when the latest completed region maximum exceeds this "
              "temperature in C (default: 150; diagnostic only)"),
    )
    args = parser.parse_args()
    if not math.isfinite(args.minimum_free_gib) or args.minimum_free_gib < 0:
        parser.error("--minimum-free-gib must be finite and nonnegative")
    if not math.isfinite(args.maximum_temperature_c):
        parser.error("--maximum-temperature-c must be finite")

    case_directory = args.case.resolve()
    log_path = choose_log(case_directory, args.log.resolve() if args.log else None)
    samples = read_samples(log_path)
    current_time, logged_wall_time = samples[-1]
    log_age_seconds = max(0.0, time.time() - log_path.stat().st_mtime)
    start_time, configured_end_time = read_control_times(case_directory)
    slope = recent_slope_or_none(
        current_stage_samples(samples, start_time), args.window
    )
    end_time = args.end_time if args.end_time is not None else configured_end_time
    remaining_simulated = max(0.0, end_time - current_time)
    (checkpoints, processor_count, checkpoints_aligned, latest_file_counts,
     incomplete_checkpoints) = processor_checkpoints(
        case_directory, current_time
    )
    checkpoint_stride = read_checkpoint_stride(case_directory)
    thermal_only_flow = read_thermal_only_flow(case_directory)
    current_series_count = current_checkpoint_series_count(
        checkpoints, checkpoint_stride
    )
    maximum_courant, cumulative_continuity, fatal_signatures = read_health(log_path)
    temperature_ranges = read_latest_temperature_ranges(log_path)
    run_request = read_latest_run_request(case_directory)
    thermal_metrics = read_latest_thermal_metrics(case_directory)
    initial_airflow = read_initial_airflow_progress(case_directory)
    case_bytes, free_bytes = storage_usage(case_directory, args.fast)

    print(f"Case: {case_directory}")
    print(f"Log: {log_path}")
    print(f"Log last updated: {format_duration(log_age_seconds)} ago")
    print(f"Simulation: {current_time:.9g} / {end_time:.9g} s")
    stage_span = end_time - start_time
    if stage_span > 0:
        stage_fraction = min(1.0, max(0.0, (current_time - start_time) / stage_span))
        print(f"Stage progress: {100.0 * stage_fraction:.2f}%")
    if run_request is not None:
        run_mode, requested_end = run_request
        if requested_end > start_time and abs(requested_end - end_time) > 1e-9:
            overall_fraction = min(
                1.0, max(0.0, current_time / requested_end)
            )
            print(
                f"Overall requested progress ({run_mode}): "
                f"{100.0 * overall_fraction:.2f}% toward {requested_end:.9g} s"
            )
    if initial_airflow is not None:
        print(format_initial_airflow_stage(initial_airflow, current_time))
    if thermal_metrics is not None:
        (thermal_time, peak_rate, average_rate, peak_region, average_region,
         thermal_elapsed, peak_limit, average_limit) = thermal_metrics
        peak_gate = f" / {peak_limit:.6g}" if peak_limit is not None else ""
        average_gate = (
            f" / {average_limit:.6g}" if average_limit is not None else ""
        )
        print(
            "Latest thermal convergence rates at "
            f"{thermal_time:.9g} s (normalized from {thermal_elapsed:.9g} s): "
            f"peak {peak_rate:.6g}{peak_gate} K/300s [{peak_region}], "
            f"component average {average_rate:.6g}{average_gate} K/300s "
            f"[{average_region}]"
        )
    if slope is None:
        print("Recent rate: warming up after solver-stage restart")
    else:
        print(f"Recent rate: {slope:.1f} wall s / simulated s")
    print(f"Solver logged wall time: {format_duration(logged_wall_time)}")
    if slope is None:
        print("Estimated remaining wall time: unavailable until two new timesteps complete")
    else:
        print(
            "Estimated remaining wall time: "
            f"{format_duration(remaining_simulated * slope)}"
        )
    if maximum_courant is not None:
        if thermal_only_flow:
            print(
                "Latest fluid max Courant: "
                f"{maximum_courant:.6g} (diagnostic only during implicit "
                "thermal-only flow; timestep controlled by maxDeltaT)"
            )
        else:
            print(f"Latest fluid max Courant: {maximum_courant:.6g}")
            headroom = courant_timestep_headroom(case_directory, maximum_courant)
            if headroom is not None:
                current_dt, stage_cap, safe_dt, multiplier = headroom
                print(
                    "Diagnostic Courant-safe timestep (80% margin): "
                    f"{safe_dt:.9g} s ({multiplier:.3g}x current "
                    f"{current_dt:.9g} s; stage cap {stage_cap:.9g} s)"
                )
    if cumulative_continuity is not None:
        print(f"Latest cumulative continuity error: {cumulative_continuity:.6g}")
    if temperature_ranges:
        hottest_region, (_, hottest_temperature) = max(
            temperature_ranges.items(), key=lambda item: item[1][1]
        )
        print(
            "Latest hottest region: "
            f"{hottest_region} {hottest_temperature:.6g} K "
            f"({hottest_temperature - 273.15:.6g} C)"
        )
        warning = temperature_warning(
            hottest_temperature, args.maximum_temperature_c
        )
        if warning:
            print(warning)
    print(
        "Fatal signatures: "
        + (", ".join(fatal_signatures) if fatal_signatures else "none")
    )
    if is_stale_run(
        log_age_seconds, current_time, end_time, args.stale_after
    ):
        print(
            "WARNING: incomplete run log is stale; verify that the solver "
            "process is still active"
        )
    case_size = "skipped (--fast)" if case_bytes is None else format_bytes(case_bytes)
    print(f"Storage: case {case_size}, volume free {format_bytes(free_bytes)}")
    storage_warning = low_space_warning(free_bytes, args.minimum_free_gib)
    if storage_warning:
        print(storage_warning)
    if checkpoints:
        print(
            f"Processor checkpoints: {len(checkpoints)} common across "
            f"{processor_count} ranks; current cadence series "
            f"{current_series_count} "
            f"(latest {checkpoints[-1]:.9g} s)"
        )
        print(
            "Checkpoint consistency: "
            + (
                f"aligned; latest files/rank {latest_file_counts}"
                if checkpoints_aligned
                else f"MISMATCHED RANK OUTPUT; latest files/rank {latest_file_counts}"
            )
        )
        if incomplete_checkpoints:
            print(
                "Checkpoint write in progress/incomplete: "
                + ", ".join(f"{value:.9g}" for value in incomplete_checkpoints)
                + f" s; observed files/rank {latest_file_counts}"
            )
        if checkpoint_stride is not None:
            next_checkpoint = min(end_time, checkpoints[-1] + checkpoint_stride)
            if next_checkpoint > current_time + 1e-12:
                eta = (
                    format_duration((next_checkpoint-current_time)*slope)
                    if slope is not None else "warming up"
                )
                print(f"Next checkpoint: {next_checkpoint:.9g} s (ETA {eta})")
    else:
        print(
            "Processor checkpoints: none"
            + (f" across {processor_count} ranks" if processor_count else "")
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
