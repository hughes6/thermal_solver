#!/usr/bin/env python3
"""Compare an OpenFOAM binary internal field between decomposed checkpoints."""

from __future__ import annotations

import argparse
import math
import re
import struct
from pathlib import Path


FIELD_RE = re.compile(
    rb"internalField\s+nonuniform\s+List<(scalar|vector)>\s+(\d+)\s*\(",
    re.MULTILINE,
)


def read_internal_field(path: Path) -> tuple[str, list[float]]:
    payload = path.read_bytes()
    header_end = payload.find(b"// * * *")
    header = payload[: header_end if header_end >= 0 else len(payload)]
    if b"format      binary;" not in header:
        raise ValueError(f"{path} is not an OpenFOAM binary field")
    if b"scalar=64" not in header:
        raise ValueError(f"{path} does not use supported 64-bit scalars")
    match = FIELD_RE.search(payload)
    if not match:
        raise ValueError(f"nonuniform internalField was not found in {path}")
    field_type = match.group(1).decode("ascii")
    item_count = int(match.group(2))
    width = 1 if field_type == "scalar" else 3
    value_count = item_count * width
    byte_count = value_count * 8
    start = match.end()
    if len(payload) < start + byte_count:
        raise ValueError(f"truncated internalField in {path}")
    values = list(struct.unpack_from(f"<{value_count}d", payload, start))
    return field_type, values


def compare_fields(
    before_paths: list[Path], after_paths: list[Path]
) -> tuple[int, float, float, float, float]:
    if len(before_paths) != len(after_paths):
        raise ValueError("checkpoint rank counts differ")
    value_count = 0
    sum_delta_squared = 0.0
    sum_after_squared = 0.0
    maximum_delta = 0.0
    for before_path, after_path in zip(before_paths, after_paths):
        before_type, before = read_internal_field(before_path)
        after_type, after = read_internal_field(after_path)
        if before_type != after_type or len(before) != len(after):
            raise ValueError(f"field layout differs: {before_path} vs {after_path}")
        value_count += len(before)
        for old, new in zip(before, after):
            delta = new - old
            sum_delta_squared += delta * delta
            sum_after_squared += new * new
            maximum_delta = max(maximum_delta, abs(delta))
    if value_count == 0:
        raise ValueError("fields contain no internal values")
    rms_delta = math.sqrt(sum_delta_squared / value_count)
    rms_after = math.sqrt(sum_after_squared / value_count)
    relative = rms_delta / rms_after if rms_after else math.inf
    return value_count, rms_delta, maximum_delta, rms_after, relative


def resolve_time_directory(processor: Path, requested: str) -> Path:
    exact = processor / requested
    if exact.is_dir():
        return exact
    requested_value = float(requested)
    candidates: list[tuple[float, Path]] = []
    for child in processor.iterdir():
        if not child.is_dir():
            continue
        try:
            candidates.append((float(child.name), child))
        except ValueError:
            pass
    if not candidates:
        raise FileNotFoundError(f"no numeric time directories in {processor}")
    value, path = min(candidates, key=lambda candidate: abs(candidate[0] - requested_value))
    tolerance = 1.0e-8 * max(1.0, abs(requested_value))
    if abs(value - requested_value) > tolerance:
        raise FileNotFoundError(
            f"time {requested} is unavailable in {processor}; nearest is {path.name}"
        )
    return path


def processor_field_paths(
    case: Path,
    time: str,
    region: str,
    field: str,
    rank: int | None = None,
) -> list[Path]:
    processors = sorted(
        (path for path in case.glob("processor*") if path.is_dir()),
        key=lambda path: int(path.name[9:]),
    )
    if rank is not None:
        processors = [path for path in processors if path.name == f"processor{rank}"]
        if not processors:
            raise FileNotFoundError(f"processor{rank} was not found in {case}")
    paths = [
        resolve_time_directory(processor, time) / region / field
        for processor in processors
    ]
    missing = [str(path) for path in paths if not path.is_file()]
    if missing:
        raise FileNotFoundError("missing field files: " + ", ".join(missing))
    return paths


def latest_common_time_names(case: Path, rank: int | None = None) -> tuple[str, str]:
    processors = sorted(
        (path for path in case.glob("processor*") if path.is_dir()),
        key=lambda path: int(path.name[9:]),
    )
    if rank is not None:
        processors = [path for path in processors if path.name == f"processor{rank}"]
    if not processors:
        suffix = f" for processor{rank}" if rank is not None else ""
        raise FileNotFoundError(f"no processor directories found{suffix} in {case}")
    common: set[float] | None = None
    names: dict[float, str] = {}
    for processor in processors:
        values: set[float] = set()
        for child in processor.iterdir():
            if not child.is_dir():
                continue
            try:
                value = float(child.name)
            except ValueError:
                continue
            values.add(value)
            names.setdefault(value, child.name)
        common = values if common is None else common & values
    ordered = sorted(common or ())
    if len(ordered) < 2:
        raise ValueError("fewer than two numeric checkpoint times are common to all ranks")
    return names[ordered[-2]], names[ordered[-1]]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path)
    parser.add_argument("before", nargs="?")
    parser.add_argument("after", nargs="?")
    parser.add_argument(
        "--latest-pair",
        action="store_true",
        help="compare the latest two checkpoint times common to all selected ranks",
    )
    parser.add_argument("--region", default="fluid")
    parser.add_argument("--field", default="U")
    parser.add_argument("--rank", type=int, help="compare only one processor rank")
    args = parser.parse_args()
    case = args.case.resolve()
    if args.latest_pair:
        if args.before is not None or args.after is not None:
            parser.error("--latest-pair cannot be combined with before/after times")
        args.before, args.after = latest_common_time_names(case, args.rank)
    elif args.before is None or args.after is None:
        parser.error("before and after times are required unless --latest-pair is used")
    before = processor_field_paths(
        case, args.before, args.region, args.field, args.rank
    )
    after = processor_field_paths(case, args.after, args.region, args.field, args.rank)
    count, rms_delta, maximum_delta, rms_field, relative = compare_fields(
        before, after
    )
    print(f"Field: {args.region}/{args.field}")
    print(f"Checkpoints: {args.before} -> {args.after}")
    if args.rank is not None:
        print(f"Processor rank: {args.rank}")
    print(f"Scalar values compared: {count}")
    print(f"RMS delta: {rms_delta:.9g}")
    print(f"Maximum component delta: {maximum_delta:.9g}")
    print(f"RMS field component: {rms_field:.9g}")
    print(f"Relative RMS delta: {100.0 * relative:.6g}%")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
