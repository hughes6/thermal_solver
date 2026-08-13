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


@dataclass(frozen=True)
class MatrixAggregate:
    mean: MatrixData
    maximum_deviation: MatrixData
    minima: MatrixData
    maxima: MatrixData
    sample_count: int


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


def aggregate(samples: list[MatrixData]) -> MatrixAggregate:
    if not samples:
        raise ValueError("At least one attribution matrix is required")
    reference = samples[0]
    for sample in samples[1:]:
        if (sample.sources != reference.sources or
                sample.targets != reference.targets):
            raise ValueError(
                "Attribution matrices have different source/target ordering")
    means = []
    deviations = []
    minima = []
    maxima = []
    for i in range(len(reference.sources)):
        mean_row = []
        deviation_row = []
        minimum_row = []
        maximum_row = []
        for j in range(len(reference.targets)):
            values = [sample.values[i][j] for sample in samples]
            mean = sum(values) / len(values)
            mean_row.append(mean)
            deviation_row.append(max(abs(value - mean) for value in values))
            minimum_row.append(min(values))
            maximum_row.append(max(values))
        means.append(tuple(mean_row))
        deviations.append(tuple(deviation_row))
        minima.append(tuple(minimum_row))
        maxima.append(tuple(maximum_row))
    shape = (reference.sources, reference.targets)
    return MatrixAggregate(
        MatrixData(*shape, tuple(means)),
        MatrixData(*shape, tuple(deviations)),
        MatrixData(*shape, tuple(minima)),
        MatrixData(*shape, tuple(maxima)),
        len(samples))


def write_aggregate_csv(aggregate_data: MatrixAggregate, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(("source_component", "target_component",
                         "mean_percent", "minimum_percent", "maximum_percent",
                         "maximum_deviation_pp", "sample_count"))
        for i, source in enumerate(aggregate_data.mean.sources):
            for j, target in enumerate(aggregate_data.mean.targets):
                writer.writerow((
                    source, target, aggregate_data.mean.values[i][j],
                    aggregate_data.minima.values[i][j],
                    aggregate_data.maxima.values[i][j],
                    aggregate_data.maximum_deviation.values[i][j],
                    aggregate_data.sample_count))


def plot_matrix(data: MatrixData, output: Path, title: str,
                comparison: MatrixData | None = None,
                comparison_title: str = "Comparison",
                uncertainty: MatrixData | None = None) -> None:
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
                if vmin < 0:
                    label = f"{value:+.2f} pp"
                elif uncertainty is not None and matrix is data:
                    label = (f"{value:.2f}%\n"
                             f"±{uncertainty.values[i][j]:.2f} pp")
                else:
                    label = f"{value:.2f}%"
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
    parser.add_argument(
        "--sample", action="append", type=Path, default=[],
        help="additional aligned snapshot matrix to average with matrix_csv; "
             "repeat for more snapshots")
    parser.add_argument(
        "--stats-csv", type=Path,
        help="write per-path mean, range, and maximum snapshot deviation")
    args = parser.parse_args()
    output = args.output or args.matrix_csv.with_suffix(".png")
    aggregate_data = aggregate(
        [read_matrix(args.matrix_csv),
         *(read_matrix(path) for path in args.sample)])
    first = aggregate_data.mean
    second = read_matrix(args.compare) if args.compare else None
    title = args.title
    uncertainty = None
    if aggregate_data.sample_count > 1:
        title = f"{title} (mean of {aggregate_data.sample_count} snapshots)"
        uncertainty = aggregate_data.maximum_deviation
    if args.stats_csv:
        write_aggregate_csv(aggregate_data, args.stats_csv)
        print(args.stats_csv.resolve())
    plot_matrix(first, output, title, second, args.compare_title, uncertainty)
    print(output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
