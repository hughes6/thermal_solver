#!/usr/bin/env python3
"""Compare an OpenFOAM binary internal field between decomposed checkpoints.

This lightweight reader weights every stored scalar component equally.  It
does not read mesh volumes, so its RMS values are not the volume-weighted
vector-magnitude metric used by the generated OpenFOAM runner's airflow-
convergence gate. Identical-topology checkpoints may be in one case or in two
different cases via ``--after-case``.
"""

from __future__ import annotations

import argparse
import hashlib
import math
import re
import struct
from pathlib import Path


FIELD_RE = re.compile(
    rb"internalField\s+nonuniform\s+List<(scalar|vector)>\s+(\d+)\s*\(",
    re.MULTILINE,
)
EMPTY_FIELD_RE = re.compile(
    rb"internalField\s+nonuniform\s+List<(scalar|vector)>\s+0\s*;",
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
        empty = EMPTY_FIELD_RE.search(payload)
        if empty:
            return empty.group(1).decode("ascii"), []
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


def vector_delta_distribution(
    before_paths: list[Path], after_paths: list[Path]
) -> tuple[int, dict[str, float], dict[str, float]]:
    """Summarize cell-vector changes and concentration of squared change."""
    if len(before_paths) != len(after_paths):
        raise ValueError("checkpoint rank counts differ")
    magnitudes: list[float] = []
    for before_path, after_path in zip(before_paths, after_paths):
        before_type, before = read_internal_field(before_path)
        after_type, after = read_internal_field(after_path)
        if before_type != "vector" or after_type != "vector":
            raise ValueError("vector delta distribution requires vector fields")
        if len(before) != len(after):
            raise ValueError(f"field layout differs: {before_path} vs {after_path}")
        for index in range(0, len(before), 3):
            dx = after[index] - before[index]
            dy = after[index + 1] - before[index + 1]
            dz = after[index + 2] - before[index + 2]
            magnitudes.append(math.sqrt(dx * dx + dy * dy + dz * dz))
    if not magnitudes:
        raise ValueError("fields contain no internal vectors")
    return delta_distribution(magnitudes)


def scalar_delta_distribution(
    before_paths: list[Path], after_paths: list[Path]
) -> tuple[int, dict[str, float], dict[str, float]]:
    """Summarize absolute scalar changes and concentration of squared change."""
    if len(before_paths) != len(after_paths):
        raise ValueError("checkpoint rank counts differ")
    magnitudes: list[float] = []
    for before_path, after_path in zip(before_paths, after_paths):
        before_type, before = read_internal_field(before_path)
        after_type, after = read_internal_field(after_path)
        if before_type != "scalar" or after_type != "scalar":
            raise ValueError("scalar delta distribution requires scalar fields")
        if len(before) != len(after):
            raise ValueError(f"field layout differs: {before_path} vs {after_path}")
        magnitudes.extend(abs(new - old) for old, new in zip(before, after))
    return delta_distribution(magnitudes)


def top_scalar_delta_locations(
    before_paths: list[Path], after_paths: list[Path], centre_paths: list[Path],
    limit: int,
) -> list[tuple[float, float, float, float, float, float]]:
    """Return abs delta, coordinates, old value, and new value for top cells."""
    if not (len(before_paths) == len(after_paths) == len(centre_paths)):
        raise ValueError("checkpoint and cell-centre rank counts differ")
    rows: list[tuple[float, float, float, float, float, float]] = []
    for before_path, after_path, centre_path in zip(
        before_paths, after_paths, centre_paths
    ):
        before_type, before = read_internal_field(before_path)
        after_type, after = read_internal_field(after_path)
        centre_type, centres = read_internal_field(centre_path)
        if before_type != "scalar" or after_type != "scalar":
            raise ValueError("top scalar locations require scalar fields")
        if centre_type != "vector":
            raise ValueError("cell centres must be a vector field")
        if len(before) != len(after) or len(centres) != 3 * len(before):
            raise ValueError("field and cell-centre layouts differ")
        for index, (old, new) in enumerate(zip(before, after)):
            base = 3 * index
            rows.append(
                (abs(new - old), centres[base], centres[base + 1],
                 centres[base + 2], old, new)
            )
    return sorted(rows, reverse=True)[:limit]


def delta_distribution(
    magnitudes: list[float],
) -> tuple[int, dict[str, float], dict[str, float]]:
    """Calculate percentiles and squared-change concentration."""
    if not magnitudes:
        raise ValueError("fields contain no internal values")
    ordered = sorted(magnitudes)

    def percentile(fraction: float) -> float:
        position = fraction * (len(ordered) - 1)
        lower = int(math.floor(position))
        upper = int(math.ceil(position))
        if lower == upper:
            return ordered[lower]
        weight = position - lower
        return ordered[lower] * (1.0 - weight) + ordered[upper] * weight

    percentiles = {
        "p50": percentile(0.50),
        "p90": percentile(0.90),
        "p95": percentile(0.95),
        "p99": percentile(0.99),
        "maximum": ordered[-1],
    }
    squared_descending = sorted((value * value for value in magnitudes), reverse=True)
    total_squared = sum(squared_descending)
    concentration: dict[str, float] = {}
    for fraction in (0.01, 0.05, 0.10):
        count = max(1, math.ceil(fraction * len(squared_descending)))
        contribution = sum(squared_descending[:count])
        concentration[f"top_{int(fraction * 100)}pct"] = (
            contribution / total_squared if total_squared else 0.0
        )
    return len(magnitudes), percentiles, concentration


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


def verify_processor_topology_identity(
    before_case: Path, after_case: Path, region: str, rank: int | None = None
) -> int:
    """Require byte-identical cell addressing before cross-case value pairing."""
    before_processors = sorted(
        (path for path in before_case.glob("processor*") if path.is_dir()),
        key=lambda path: int(path.name[9:]),
    )
    after_processors = sorted(
        (path for path in after_case.glob("processor*") if path.is_dir()),
        key=lambda path: int(path.name[9:]),
    )
    if rank is not None:
        before_processors = [
            path for path in before_processors if path.name == f"processor{rank}"
        ]
        after_processors = [
            path for path in after_processors if path.name == f"processor{rank}"
        ]
    before_names = [path.name for path in before_processors]
    after_names = [path.name for path in after_processors]
    if not before_names or before_names != after_names:
        raise ValueError(
            "processor partitions differ; cross-case fields cannot be paired safely"
        )
    for before_processor, after_processor in zip(
        before_processors, after_processors
    ):
        relative = Path("constant") / region / "polyMesh" / "cellProcAddressing"
        before_addressing = before_processor / relative
        after_addressing = after_processor / relative
        if not before_addressing.is_file() or not after_addressing.is_file():
            raise ValueError(
                f"missing cellProcAddressing for {before_processor.name}/{region}"
            )
        before_digest = hashlib.sha256(before_addressing.read_bytes()).digest()
        after_digest = hashlib.sha256(after_addressing.read_bytes()).digest()
        if before_digest != after_digest:
            raise ValueError(
                "processor cell ordering differs for "
                f"{before_processor.name}/{region}; use a mapped-field comparator"
            )
    return len(before_processors)


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
    parser.add_argument(
        "--after-case", type=Path,
        help="case containing the after checkpoint; requires identical topology",
    )
    parser.add_argument("--rank", type=int, help="compare only one processor rank")
    parser.add_argument(
        "--top-cells", type=int, default=0,
        help="for scalar fields, report this many largest-delta cell locations; "
             "requires a C cell-centres field at the after time",
    )
    args = parser.parse_args()
    case = args.case.resolve()
    after_case = args.after_case.resolve() if args.after_case else case
    if after_case != case:
        verify_processor_topology_identity(case, after_case, args.region, args.rank)
    if args.latest_pair:
        if args.after_case is not None:
            parser.error("--latest-pair cannot be combined with --after-case")
        if args.before is not None or args.after is not None:
            parser.error("--latest-pair cannot be combined with before/after times")
        args.before, args.after = latest_common_time_names(case, args.rank)
    elif args.before is None or args.after is None:
        parser.error("before and after times are required unless --latest-pair is used")
    before = processor_field_paths(
        case, args.before, args.region, args.field, args.rank
    )
    after = processor_field_paths(
        after_case, args.after, args.region, args.field, args.rank
    )
    count, rms_delta, maximum_delta, rms_field, relative = compare_fields(
        before, after
    )
    print(f"Field: {args.region}/{args.field}")
    if after_case != case:
        print(f"Cases: {case} -> {after_case}")
        print("Topology: byte-identical processor cell addressing verified")
    print(f"Checkpoints: {args.before} -> {args.after}")
    if args.rank is not None:
        print(f"Processor rank: {args.rank}")
    print(f"Scalar values compared: {count}")
    print(
        "Weighting: equal per scalar component (not the runner's "
        "volume-weighted vector-magnitude gate metric)"
    )
    print(f"Component-weighted RMS delta: {rms_delta:.9g}")
    print(f"Maximum component delta: {maximum_delta:.9g}")
    print(f"Component-weighted RMS field value: {rms_field:.9g}")
    print(f"Component-weighted relative RMS delta: {100.0 * relative:.6g}%")
    field_type = read_internal_field(before[0])[0]
    if field_type == "vector":
        cells, percentiles, concentration = vector_delta_distribution(before, after)
        print(f"Cell vectors compared: {cells}")
        print(
            "Delta-magnitude percentiles (m/s): "
            f"p50={percentiles['p50']:.9g}, p90={percentiles['p90']:.9g}, "
            f"p95={percentiles['p95']:.9g}, p99={percentiles['p99']:.9g}, "
            f"max={percentiles['maximum']:.9g}"
        )
        print(
            "Squared-delta concentration: "
            f"top 1%={100.0 * concentration['top_1pct']:.6g}%, "
            f"top 5%={100.0 * concentration['top_5pct']:.6g}%, "
            f"top 10%={100.0 * concentration['top_10pct']:.6g}%"
        )
    elif field_type == "scalar":
        cells, percentiles, concentration = scalar_delta_distribution(before, after)
        print(f"Scalar cells compared: {cells}")
        print(
            "Absolute-delta percentiles: "
            f"p50={percentiles['p50']:.9g}, p90={percentiles['p90']:.9g}, "
            f"p95={percentiles['p95']:.9g}, p99={percentiles['p99']:.9g}, "
            f"max={percentiles['maximum']:.9g}"
        )
        print(
            "Squared-delta concentration: "
            f"top 1%={100.0 * concentration['top_1pct']:.6g}%, "
            f"top 5%={100.0 * concentration['top_5pct']:.6g}%, "
            f"top 10%={100.0 * concentration['top_10pct']:.6g}%"
        )
        if args.top_cells > 0:
            centres = processor_field_paths(
                after_case, args.after, args.region, "C", args.rank
            )
            rows = top_scalar_delta_locations(
                before, after, centres, args.top_cells
            )
            print("Largest scalar deltas (absDelta, x, y, z, before, after):")
            for row in rows:
                print("  " + ", ".join(f"{value:.9g}" for value in row))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
