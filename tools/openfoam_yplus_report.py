#!/usr/bin/env python3
"""Report authoritative latest OpenFOAM y+ patch summaries."""

from __future__ import annotations

import argparse
import json
from dataclasses import asdict, dataclass
from pathlib import Path


@dataclass(frozen=True)
class YPlusRow:
    case: str
    time_s: float
    patch: str
    minimum: float
    maximum: float
    average: float
    regime: str
    source_file: str


def numeric_directories(root: Path) -> list[tuple[float, Path]]:
    rows = []
    for path in root.iterdir() if root.is_dir() else ():
        try:
            rows.append((float(path.name), path))
        except ValueError:
            pass
    return sorted(rows)


def parse_file(path: Path) -> list[tuple[float, str, float, float, float]]:
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        fields = line.split()
        if len(fields) != 5:
            continue
        try:
            rows.append((float(fields[0]), fields[1], float(fields[2]),
                         float(fields[3]), float(fields[4])))
        except ValueError:
            continue
    return rows


def regime(minimum: float, maximum: float, average: float) -> str:
    if maximum <= 1.0:
        return "wall-resolved"
    if minimum >= 30.0:
        return "log-layer wall-function"
    if minimum < 30.0 and maximum > 1.0:
        return "mixed/buffer-layer"
    return "unclassified"


def analyze_case(case: Path) -> list[YPlusRow]:
    case = case.resolve()
    root = case / "postProcessing" / "fluid" / "fluid_y_plus"
    times = numeric_directories(root)
    if not times:
        raise ValueError(f"No yPlus report times in {root}")
    time_value, directory = times[-1]
    candidates = []
    for path in directory.glob("yPlus*.dat"):
        rows = parse_file(path)
        if rows:
            score = max(row[3] for row in rows)
            candidates.append((score, path, rows))
    if not candidates:
        raise ValueError(f"No parseable yPlus reports in {directory}")
    # A thermal-only stage can emit a zero placeholder next to the subsequent
    # live-flow report. The report with the largest observed y+ is authoritative.
    score, path, values = max(candidates, key=lambda item: item[0])
    if score <= 0.0:
        raise ValueError(f"Latest yPlus reports contain only zeros in {directory}")
    result = []
    for sample_time, patch, minimum, maximum, average in values:
        tolerance = 1e-8 * max(1.0, abs(time_value))
        if abs(sample_time - time_value) > tolerance:
            raise ValueError(f"Stale yPlus row in {path}: {sample_time:g}")
        result.append(YPlusRow(
            str(case), time_value, patch, minimum, maximum, average,
            regime(minimum, maximum, average), path.name))
    return result


def write_markdown(path: Path, rows: list[YPlusRow]) -> None:
    lines = [
        "# OpenFOAM wall y+ report", "",
        "A mixed/buffer-layer classification means the patch spans y+ above "
        "1 and below 30. Local wall shear and heat transfer remain wall-model "
        "sensitive; this is not proof of grid-independent near-wall physics.", "",
        "| Case | Patch | Min | Average | Max | Regime | Source |",
        "|---|---|---:|---:|---:|---|---|",
    ]
    for row in rows:
        lines.append(
            f"| {Path(row.case).name} | {row.patch} | {row.minimum:.6g} | "
            f"{row.average:.6g} | {row.maximum:.6g} | {row.regime} | "
            f"{row.source_file} |")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cases", nargs="+", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--markdown", type=Path)
    args = parser.parse_args()
    rows = [row for case in args.cases for row in analyze_case(case)]
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps([asdict(row) for row in rows], indent=2) + "\n",
            encoding="utf-8")
    if args.markdown:
        write_markdown(args.markdown, rows)
    for row in rows:
        print(f"{Path(row.case).name} | {row.patch} | avg {row.average:.6g} | "
              f"range {row.minimum:.6g}-{row.maximum:.6g} | {row.regime}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
