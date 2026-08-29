"""Audit per-rank and per-region OpenFOAM cell ownership imbalance."""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import json
import math
from pathlib import Path
import re


NCELLS_PATTERN = re.compile(rb"\bnCells:([0-9]+)\b")


@dataclass(frozen=True)
class RankCells:
    rank: int
    fluid: int
    solid: int
    total: int
    difference_from_ideal: float
    regions: dict[str, int]


@dataclass(frozen=True)
class PartitionAudit:
    case: str
    ranks: int
    total_cells: int
    ideal_cells_per_rank: float
    maximum_cells: int
    minimum_cells: int
    maximum_over_ideal: float
    maximum_to_minimum: float
    ranks_detail: list[RankCells]


def owner_ncells(path: Path) -> int:
    with path.open("rb") as stream:
        header = stream.read(4096)
    match = NCELLS_PATTERN.search(header)
    if not match:
        raise ValueError(f"missing nCells note in {path}")
    return int(match.group(1))


def processor_directories(case: Path) -> list[Path]:
    found = {}
    for candidate in case.glob("processor[0-9]*"):
        if not candidate.is_dir():
            continue
        suffix = candidate.name.removeprefix("processor")
        if suffix.isdigit():
            found[int(suffix)] = candidate
    if not found:
        raise ValueError(f"no processor directories in {case}")
    expected = list(range(max(found) + 1))
    if sorted(found) != expected:
        raise ValueError(
            "processor directories are not contiguous: "
            + ", ".join(str(rank) for rank in sorted(found))
        )
    return [found[rank] for rank in expected]


def audit_case(case: Path, fluid_region: str = "fluid") -> PartitionAudit:
    case = case.expanduser().resolve()
    processors = processor_directories(case)
    rows = []
    expected_regions = None
    for rank, processor in enumerate(processors):
        constant = processor / "constant"
        regions = {
            region.name: owner_ncells(region / "polyMesh" / "owner")
            for region in sorted(constant.iterdir(), key=lambda item: item.name)
            if (region / "polyMesh" / "owner").is_file()
        }
        if expected_regions is None:
            expected_regions = set(regions)
        elif set(regions) != expected_regions:
            missing = sorted(expected_regions - set(regions))
            extra = sorted(set(regions) - expected_regions)
            raise ValueError(
                f"rank {rank} region mismatch; missing={missing}, extra={extra}"
            )
        if fluid_region not in regions:
            raise ValueError(f"rank {rank} has no {fluid_region!r} region")
        fluid = regions[fluid_region]
        solid = sum(value for name, value in regions.items() if name != fluid_region)
        rows.append((rank, fluid, solid, fluid + solid, regions))

    total = sum(row[3] for row in rows)
    ideal = total / len(rows)
    details = [
        RankCells(
            rank=rank,
            fluid=fluid,
            solid=solid,
            total=rank_total,
            difference_from_ideal=(rank_total - ideal) / ideal if ideal else 0.0,
            regions=regions,
        )
        for rank, fluid, solid, rank_total, regions in rows
    ]
    maximum = max(row.total for row in details)
    minimum = min(row.total for row in details)
    return PartitionAudit(
        case=str(case),
        ranks=len(details),
        total_cells=total,
        ideal_cells_per_rank=ideal,
        maximum_cells=maximum,
        minimum_cells=minimum,
        maximum_over_ideal=maximum / ideal - 1.0 if ideal else 0.0,
        maximum_to_minimum=(
            maximum / minimum if minimum else math.inf if maximum else 1.0
        ),
        ranks_detail=details,
    )


def markdown(audit: PartitionAudit) -> str:
    lines = [
        "# OpenFOAM partition audit",
        "",
        f"Case: `{audit.case}`",
        "",
        "| Rank | Fluid cells | Solid cells | Total cells | Difference from ideal |",
        "|---:|---:|---:|---:|---:|",
    ]
    for row in audit.ranks_detail:
        lines.append(
            f"| {row.rank} | {row.fluid:,} | {row.solid:,} | {row.total:,} | "
            f"{row.difference_from_ideal:+.2%} |"
        )
    lines.extend(
        [
            "",
            f"- Total cells: {audit.total_cells:,}",
            f"- Ideal cells/rank: {audit.ideal_cells_per_rank:,.1f}",
            f"- Maximum over ideal: {audit.maximum_over_ideal:.2%}",
            f"- Maximum/minimum rank ratio: {audit.maximum_to_minimum:.4g}",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path)
    parser.add_argument("--fluid-region", default="fluid")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--markdown", type=Path)
    parser.add_argument(
        "--maximum-over-ideal",
        type=float,
        help="return failure when the heaviest rank exceeds ideal by this fraction",
    )
    args = parser.parse_args()
    if (
        args.maximum_over_ideal is not None
        and (
            not math.isfinite(args.maximum_over_ideal)
            or args.maximum_over_ideal < 0.0
        )
    ):
        parser.error("--maximum-over-ideal must be finite and nonnegative")

    try:
        result = audit_case(args.case, args.fluid_region)
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error
    rendered = markdown(result)
    print(rendered)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps(asdict(result), indent=2) + "\n", encoding="utf-8"
        )
    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text(rendered, encoding="utf-8")
    if (
        args.maximum_over_ideal is not None
        and result.maximum_over_ideal > args.maximum_over_ideal
    ):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
