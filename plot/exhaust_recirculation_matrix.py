"""Plot saved exhaust-to-intake passive-tracer attribution matrices."""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class MatrixData:
    sources: tuple[str, ...]
    targets: tuple[str, ...]
    values: tuple[tuple[float, ...], ...]


def short_name(name: str) -> str:
    for prefix in ("Generic ",):
        if name.startswith(prefix):
            name = name[len(prefix):]
    return name


def read_matrix(path: Path) -> MatrixData:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise ValueError(f"No attribution rows in {path}")
    value_column = ("mass_weighted_tracer_fraction"
                    if "mass_weighted_tracer_fraction" in rows[0]
                    else "tracer_fraction")
    sources = tuple(dict.fromkeys(row["source_component"] for row in rows))
    targets = tuple(dict.fromkeys(row["target_component"] for row in rows))
    lookup = {(row["source_component"], row["target_component"]):
              100.0 * float(row[value_column]) for row in rows}
    missing = [(source, target) for source in sources for target in targets
               if (source, target) not in lookup]
    if missing:
        raise ValueError(f"Incomplete attribution matrix; missing {missing[0]}")
    values = tuple(tuple(lookup[source, target] for target in targets)
                   for source in sources)
    return MatrixData(sources, targets, values)


def difference(first: MatrixData, second: MatrixData) -> MatrixData:
    if first.sources != second.sources or first.targets != second.targets:
        raise ValueError("Attribution matrices have different source/target ordering")
    values = tuple(tuple(second.values[i][j] - first.values[i][j]
                         for j in range(len(first.targets)))
                   for i in range(len(first.sources)))
    return MatrixData(first.sources, first.targets, values)


def plot_matrix(data: MatrixData, output: Path, title: str,
                comparison: MatrixData | None = None,
                comparison_title: str = "Comparison") -> None:
    import matplotlib.pyplot as plt
    import numpy as np

    panels = [(data, title, "viridis", 0.0, 100.0)]
    if comparison is not None:
        delta = difference(data, comparison)
        limit = max(1.0, max(abs(value) for row in delta.values for value in row))
        panels.extend([(comparison, comparison_title, "viridis", 0.0, 100.0),
                       (delta, f"{comparison_title} minus {title}", "coolwarm", -limit, limit)])
    figure, axes = plt.subplots(1, len(panels), figsize=(5.2 * len(panels), 4.4),
                                constrained_layout=True, squeeze=False)
    for axis, (matrix, panel_title, cmap, vmin, vmax) in zip(axes[0], panels):
        array = np.asarray(matrix.values)
        image = axis.imshow(array, cmap=cmap, vmin=vmin, vmax=vmax, aspect="auto")
        axis.set_title(panel_title)
        axis.set_xlabel("Intake component")
        axis.set_ylabel("Exhaust source")
        axis.set_xticks(range(len(matrix.targets)), [short_name(x) for x in matrix.targets],
                        rotation=30, ha="right")
        axis.set_yticks(range(len(matrix.sources)), [short_name(x) for x in matrix.sources])
        for i in range(array.shape[0]):
            for j in range(array.shape[1]):
                value = array[i, j]
                label = f"{value:+.2f} pp" if vmin < 0 else f"{value:.2f}%"
                normalized = (value - vmin) / (vmax - vmin) if vmax > vmin else 0.5
                axis.text(j, i, label, ha="center", va="center",
                          color="white" if normalized < 0.35 or normalized > 0.75 else "black",
                          fontsize=9)
        figure.colorbar(image, ax=axis, shrink=0.8,
                        label="Difference (percentage points)" if vmin < 0 else "Attributed intake mass (%)")
    output.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output, dpi=180)
    plt.close(figure)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("matrix_csv", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--title", default="Exhaust recirculation attribution")
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--compare-title", default="Comparison")
    args = parser.parse_args()
    output = args.output or args.matrix_csv.with_suffix(".png")
    first = read_matrix(args.matrix_csv)
    second = read_matrix(args.compare) if args.compare else None
    plot_matrix(first, output, args.title, second, args.compare_title)
    print(output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
