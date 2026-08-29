"""Audit internal-fan operating points across decomposed OpenFOAM checkpoints."""

from __future__ import annotations

import argparse
import math
import re
from pathlib import Path


TIME_RE = re.compile(r"^[0-9]+(?:\.[0-9]+)?(?:[eE][-+]?[0-9]+)?$")
FLOW_RE = re.compile(r"^\s*flow_rate\s+([-+0-9.eE]+)\s*;", re.MULTILINE)


def numeric_times(processor: Path) -> dict[float, str]:
    result: dict[float, str] = {}
    if not processor.is_dir():
        return result
    for child in processor.iterdir():
        if not child.is_dir() or not TIME_RE.fullmatch(child.name):
            continue
        value = float(child.name)
        if value in result and result[value] != child.name:
            raise ValueError(
                f"ambiguous checkpoint spellings: {result[value]} and {child.name}")
        result[value] = child.name
    return result


def read_rank_flows(rank: Path, time_name: str) -> dict[str, float]:
    uniform = rank / time_name / "fluid" / "uniform"
    result: dict[str, float] = {}
    if not uniform.is_dir():
        return result
    for path in sorted(uniform.glob("*Properties")):
        match = FLOW_RE.search(path.read_text(encoding="utf-8", errors="replace"))
        if match:
            result[path.name.removesuffix("Properties")] = float(match.group(1))
    return result


def audit(case: Path, tolerance: float = 1e-10) -> tuple[list[float], list[str], dict]:
    ranks = sorted(
        (path for path in case.glob("processor*") if path.is_dir()),
        key=lambda path: int(path.name.removeprefix("processor")),
    )
    if not ranks:
        raise ValueError(f"no processor directories in {case}")
    rank_times = [numeric_times(rank) for rank in ranks]
    common = set(rank_times[0])
    for values in rank_times[1:]:
        common &= set(values)
    records: dict[float, dict[str, float]] = {}
    fan_names: set[str] = set()
    for time_value in sorted(common):
        per_rank = [
            read_rank_flows(rank, times[time_value])
            for rank, times in zip(ranks, rank_times)
        ]
        baseline = per_rank[0]
        if not baseline:
            continue
        for index, values in enumerate(per_rank[1:], start=1):
            if values.keys() != baseline.keys():
                raise ValueError(
                    f"fan-property inventory differs at t={time_value:g} on processor{index}")
            for name, value in baseline.items():
                if not math.isclose(value, values[name], rel_tol=tolerance, abs_tol=tolerance):
                    raise ValueError(
                        f"rank-inconsistent {name} at t={time_value:g}: "
                        f"{value:.9g} versus {values[name]:.9g}")
        records[time_value] = baseline
        fan_names.update(baseline)
    if not records:
        raise ValueError("no common checkpoints contain fan operating-point files")
    return sorted(records), sorted(fan_names), records


def markdown(times: list[float], fans: list[str], records: dict) -> str:
    lines = [
        "| Internal fan | " + " | ".join(f"t={value:g} s" for value in times) + " |",
        "|---|" + "---:|" * len(times),
    ]
    for fan in fans:
        values = []
        for time_value in times:
            value = records[time_value].get(fan)
            if value is None:
                values.append("—")
            else:
                marker = " ⚠ reverse" if value <= 0 else ""
                values.append(f"{value:.7g}{marker}")
        lines.append(f"| `{fan}` | " + " | ".join(values) + " |")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path)
    parser.add_argument("--markdown", type=Path, help="write a Markdown trend table")
    parser.add_argument("--tolerance", type=float, default=1e-10)
    args = parser.parse_args()
    times, fans, records = audit(args.case, args.tolerance)
    report = markdown(times, fans, records)
    if args.markdown:
        args.markdown.write_text(report, encoding="utf-8")
        print(f"Wrote {args.markdown}")
    else:
        print(report, end="")
    reversed_points = sum(
        value <= 0 for values in records.values() for value in values.values())
    print(
        f"Audited {len(fans)} fans across {len(times)} checkpoints; "
        f"nonpositive operating points: {reversed_points}."
    )
    return 1 if reversed_points else 0


if __name__ == "__main__":
    raise SystemExit(main())
