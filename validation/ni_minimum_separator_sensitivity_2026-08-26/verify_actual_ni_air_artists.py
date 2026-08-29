"""Verify the provisional NI export's actual rendered Air artist colors."""

from __future__ import annotations

import hashlib
from pathlib import Path
import runpy
import sys
from unittest import mock

import matplotlib

matplotlib.use("Agg", force=True)
import matplotlib.pyplot as plt
from matplotlib.colors import to_rgba
from mpl_toolkits.mplot3d.axes3d import Axes3D


EVIDENCE_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = EVIDENCE_DIR.parents[1]
GEOMETRY = EVIDENCE_DIR / "plots" / "geometry_input.txt"
PLOTTER = PROJECT_ROOT / "plot" / "plot_component.py"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def drawn_region_kinds(path: Path) -> list[str]:
    """Return region kinds in the order that plot_component calls bar3d."""
    lines = [line.strip() for line in path.read_text(encoding="utf-8").splitlines()]
    regions: list[tuple[str, list[float]]] = []
    index = 0
    while index < len(lines):
        if not lines[index].startswith("Internal Region "):
            index += 1
            continue
        kind = None
        size = None
        index += 1
        while index < len(lines) and not lines[index].startswith(
            ("Internal Region ", "Component ", "Fan ", "Vent ")
        ):
            if lines[index].startswith("type:"):
                kind = lines[index].split(":", 1)[1].strip().rsplit("/", 1)[-1]
            elif lines[index].startswith("size:"):
                size = [
                    float(value)
                    for value in lines[index]
                    .split(":", 1)[1]
                    .replace("m", "")
                    .split()
                ]
            index += 1
        if kind is None or size is None:
            raise AssertionError("exported internal region lacks type or size")
        if any(value > 0.0 for value in size):
            regions.append((kind, size))
    return [kind for kind, _ in regions]


def main() -> None:
    if not GEOMETRY.is_file():
        raise SystemExit(f"geometry evidence is missing: {GEOMETRY}")

    kinds = drawn_region_kinds(GEOMETRY)
    calls: list[dict[str, object]] = []
    original_bar3d = Axes3D.bar3d

    def recording_bar3d(axis, *args, **kwargs):
        calls.append(kwargs.copy())
        return original_bar3d(axis, *args, **kwargs)

    argv = [
        str(PLOTTER),
        "--input",
        str(GEOMETRY),
        "--component-index",
        "1",
    ]
    with (
        mock.patch.object(Axes3D, "bar3d", recording_bar3d),
        mock.patch.object(plt, "show"),
        mock.patch.object(sys, "argv", argv),
    ):
        runpy.run_path(str(PLOTTER), run_name="__main__")
    plt.close("all")

    # The first bar3d call is the transparent component envelope.
    region_calls = calls[1:]
    if len(region_calls) != len(kinds):
        raise AssertionError(
            f"captured {len(region_calls)} region artists for {len(kinds)} drawn regions"
        )

    air_indices = [index for index, kind in enumerate(kinds) if kind.casefold() == "air"]
    if len(air_indices) != 2:
        raise AssertionError(
            f"expected exactly two provisional NI Air regions, found {len(air_indices)}"
        )

    expected_rgba = to_rgba("tab:cyan")
    fill_rgba = [to_rgba(region_calls[index]["color"]) for index in air_indices]
    edge_rgba = [to_rgba(region_calls[index]["edgecolor"]) for index in air_indices]
    if fill_rgba != [expected_rgba] * len(air_indices):
        raise AssertionError(f"Air fill colors differ: {fill_rgba}")
    if edge_rgba != [expected_rgba] * len(air_indices):
        raise AssertionError(f"Air edge colors differ: {edge_rgba}")

    print(f"python={sys.version.split()[0]}")
    print(f"matplotlib={matplotlib.__version__}")
    print(f"geometry={GEOMETRY}")
    print(f"geometry_sha256={sha256(GEOMETRY)}")
    print(f"plotter={PLOTTER}")
    print(f"plotter_sha256={sha256(PLOTTER)}")
    print(f"drawn_region_count={len(kinds)}")
    print(f"air_region_count={len(air_indices)}")
    print(f"air_artist_indices_1_based={[index + 1 for index in air_indices]}")
    print(f"expected_fill_and_edge_rgba={expected_rgba}")
    print("PASS: both actual provisional NI Air artists use identical tab:cyan fill and edge colors")


if __name__ == "__main__":
    main()
