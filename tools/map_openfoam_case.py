"""Safely map a reconstructed OpenFOAM state onto a fresh exported case."""

from __future__ import annotations

import argparse
import re
import shlex
import subprocess
from pathlib import Path


def numeric_time_directories(case: Path) -> list[tuple[float, Path]]:
    result = []
    if not case.is_dir():
        return result
    for path in case.iterdir():
        if not path.is_dir():
            continue
        try:
            result.append((float(path.name), path))
        except ValueError:
            continue
    return sorted(result)


def exact_time_path(case: Path, requested: float) -> Path:
    scale = max(1.0, abs(requested))
    matches = [
        (abs(value - requested), path)
        for value, path in numeric_time_directories(case)
        if abs(value - requested) <= 1.0e-9 * scale
    ]
    if not matches:
        raise ValueError(f"No reconstructed source checkpoint matches t={requested:g} s")
    return min(matches)[1]


def regions(case: Path) -> list[str]:
    path = case / "constant" / "regionProperties"
    if not path.is_file():
        raise ValueError(f"Missing region dictionary: {path}")
    text = re.sub(r"//.*?$|/\*.*?\*/", "", path.read_text(
        encoding="utf-8", errors="replace"), flags=re.MULTILINE | re.DOTALL)
    match = re.search(r"\bregions\s*\((.*?)\)\s*;", text, re.DOTALL)
    if not match:
        raise ValueError(f"Unable to parse regions from {path}")
    names = []
    for group in re.finditer(r"\b(?:fluid|solid)\s*\(([^)]*)\)", match.group(1)):
        names.extend(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", group.group(1)))
    if not names or len(names) != len(set(names)):
        raise ValueError(f"Region list is empty or contains duplicates in {path}")
    return names


def wsl_path(path: Path) -> str:
    resolved = path.expanduser().resolve()
    text = resolved.as_posix()
    if resolved.drive:
        return f"/mnt/{resolved.drive[0].lower()}/{text.split(':', 1)[1].lstrip('/')}"
    return text


def shell_command(command: str, use_wsl: bool) -> list[str]:
    return ["wsl", "bash", "-lc", command] if use_wsl else ["bash", "-lc", command]


def validate_cases(source: Path, target: Path, source_time: float) -> Path:
    source = source.expanduser().resolve()
    target = target.expanduser().resolve()
    if source == target:
        raise ValueError("Source and target cases must be different")
    for label, case in (("source", source), ("target", target)):
        if not (case / "system" / "controlDict").is_file():
            raise ValueError(f"Not an exported OpenFOAM {label} case: {case}")
    checkpoint = exact_time_path(source, source_time)
    target_times = numeric_time_directories(target)
    if any(abs(value) > 1.0e-12 for value, _ in target_times):
        raise ValueError(
            "Target contains nonzero reconstructed results; mapping is allowed only "
            "into a fresh export so existing data cannot be overwritten")
    if list(target.glob("processor[0-9]*")):
        raise ValueError(
            "Target already contains processor partitions; use a fresh unique export")
    for script in ("prepare_regions.sh", "run_parallel.sh"):
        if not (target / script).is_file():
            raise ValueError(f"Target is missing generated workflow script: {script}")
    for region in regions(target):
        if not (checkpoint / region / "T").is_file():
            raise ValueError(
                f"Source checkpoint {checkpoint.name} lacks required {region}/T")
    if not (checkpoint / "fluid" / "U").is_file():
        raise ValueError(
            f"Source checkpoint {checkpoint.name} lacks required fluid/U")
    return checkpoint


def build_commands(source: Path, target: Path, source_time: float,
                   warm_start_end: float, processes: int, launcher: str):
    source_linux = shlex.quote(wsl_path(source))
    target_linux = shlex.quote(wsl_path(target))
    launcher = shlex.quote(launcher)
    commands = [
        f"cd {target_linux} && {launcher} env OPENFOAM_LAUNCHER=env "
        "./prepare_regions.sh",
    ]
    for region in regions(target):
        word = shlex.quote(region)
        log = shlex.quote(f"mapFields_{region}.log")
        commands.append(
            f"cd {target_linux} && {launcher} env mapFields -case . "
            f"{source_linux} -sourceTime {source_time:.17g} "
            f"-mapMethod interpolate -consistent -sourceRegion {word} "
            f"-targetRegion {word} > {log} 2>&1"
        )
    commands.append(
        f"cd {target_linux} && {launcher} env OPENFOAM_LAUNCHER=env "
        f"./run_parallel.sh {processes} --warm-start {warm_start_end:.17g}"
    )
    return commands


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--target", required=True, type=Path)
    parser.add_argument("--source-time", required=True, type=float)
    parser.add_argument("--warm-start-end", type=float, default=0.01)
    parser.add_argument("--processes", type=int, default=2)
    parser.add_argument(
        "--launcher", default="/usr/lib/openfoam/openfoam2606/etc/openfoam")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if args.source_time < 0.0:
        raise SystemExit("--source-time must be nonnegative")
    if args.warm_start_end <= 0.0:
        raise SystemExit("--warm-start-end must be positive")
    if args.processes < 2:
        raise SystemExit("--processes must be at least two")
    try:
        checkpoint = validate_cases(args.source, args.target, args.source_time)
        commands = build_commands(
            args.source, args.target, args.source_time,
            args.warm_start_end, args.processes, args.launcher)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    print(f"Validated fresh target and reconstructed source t={checkpoint.name} s.")
    for index, command in enumerate(commands, start=1):
        print(f"[{index}/{len(commands)}] {command}")
        if not args.dry_run:
            subprocess.run(shell_command(command, bool(Path().resolve().drive)), check=True)
    if args.dry_run:
        print("Dry run complete; no commands were executed.")
    else:
        print("Mapping and mandatory coupled warm start completed.")


if __name__ == "__main__":
    main()
