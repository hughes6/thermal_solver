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


def processor_field_paths(case: Path, time: str, region: str, field: str) -> list[Path]:
    processors = sorted(
        (path for path in case.glob("processor*") if path.is_dir()),
        key=lambda path: int(path.name[9:]),
    )
    paths = [processor / time / region / field for processor in processors]
    missing = [str(path) for path in paths if not path.is_file()]
    if missing:
        raise FileNotFoundError("missing field files: " + ", ".join(missing))
    return paths


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path)
    parser.add_argument("before")
    parser.add_argument("after")
    parser.add_argument("--region", default="fluid")
    parser.add_argument("--field", default="U")
    args = parser.parse_args()
    case = args.case.resolve()
    before = processor_field_paths(case, args.before, args.region, args.field)
    after = processor_field_paths(case, args.after, args.region, args.field)
    count, rms_delta, maximum_delta, rms_field, relative = compare_fields(
        before, after
    )
    print(f"Field: {args.region}/{args.field}")
    print(f"Checkpoints: {args.before} -> {args.after}")
    print(f"Scalar values compared: {count}")
    print(f"RMS delta: {rms_delta:.9g}")
    print(f"Maximum component delta: {maximum_delta:.9g}")
    print(f"RMS field component: {rms_field:.9g}")
    print(f"Relative RMS delta: {100.0 * relative:.6g}%")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
