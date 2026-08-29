#!/usr/bin/env python3
"""Map OpenFOAM boundary patches to disconnected fluid cell regions."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import mmap
from pathlib import Path
import re
import struct


class LabelList:
    def __init__(self, path: Path):
        self.stream = path.open("rb")
        self.data = mmap.mmap(self.stream.fileno(), 0, access=mmap.ACCESS_READ)
        uniform = re.search(
            rb"\binternalField\s+uniform\s+(-?\d+(?:\.\d+)?)\s*;",
            self.data,
        )
        if uniform:
            self.uniform_value = int(float(uniform.group(1)))
            self.count = None
            self.start = 0
            self.binary = False
            self.ascii_values = []
            return
        self.uniform_value = None
        match = re.search(rb"\n\s*(\d+)\s*\n\s*\(\s*\n?", self.data)
        if not match:
            raise ValueError(f"Cannot parse label list {path}")
        self.count = int(match.group(1))
        self.start = match.end()
        self.binary = re.search(
            rb"\bformat\s+binary\s*;", self.data[:match.start()]) is not None
        if not self.binary:
            end = self.data.find(b")", self.start)
            self.ascii_values = [
                int(value) for value in self.data[self.start:end].split()
            ]
            if len(self.ascii_values) != self.count:
                raise ValueError(f"Wrong label count in {path}")

    def __getitem__(self, index: int) -> int:
        if self.uniform_value is not None:
            if index < 0:
                raise IndexError(index)
            return self.uniform_value
        if index < 0 or index >= self.count:
            raise IndexError(index)
        if self.binary:
            return struct.unpack_from("<i", self.data, self.start + 4*index)[0]
        return self.ascii_values[index]

    def close(self) -> None:
        self.data.close()
        self.stream.close()


def boundary_patches(path: Path) -> list[dict]:
    text = path.read_text(encoding="ascii", errors="replace")
    patches = []
    for match in re.finditer(r"(?m)^\s{4}(\S+)\s*\n\s*\{([^{}]*)\}", text):
        name, block = match.groups()
        kind = re.search(r"\btype\s+(\S+)\s*;", block)
        faces = re.search(r"\bnFaces\s+(\d+)\s*;", block)
        start = re.search(r"\bstartFace\s+(\d+)\s*;", block)
        if kind and faces and start:
            patches.append({"name": name, "type": kind.group(1),
                            "nFaces": int(faces.group(1)),
                            "startFace": int(start.group(1))})
    if not patches:
        raise ValueError(f"No boundary patches found in {path}")
    return patches


def audit(poly_mesh: Path, cell_to_region_path: Path) -> list[dict]:
    owner = LabelList(poly_mesh / "owner")
    regions = LabelList(cell_to_region_path)
    try:
        rows = []
        for patch in boundary_patches(poly_mesh / "boundary"):
            counts = Counter()
            for face in range(patch["startFace"],
                              patch["startFace"] + patch["nFaces"]):
                counts[regions[owner[face]]] += 1
            rows.append({**patch, "regions": dict(sorted(counts.items()))})
        return rows
    finally:
        owner.close()
        regions.close()


def validate(rows: list[dict], expected_regions: int | None = None,
             opening_region: int | None = None) -> list[str]:
    """Return actionable topology errors without coupling checks to printing."""
    errors = []
    found_regions = {
        region for row in rows for region in row["regions"]
    }
    if expected_regions is not None and len(found_regions) != expected_regions:
        errors.append(
            f"expected {expected_regions} connected fluid regions, found "
            f"{len(found_regions)}: {sorted(found_regions)}"
        )
    if opening_region is not None:
        for row in rows:
            if row["type"] != "patch":
                continue
            actual = sorted(row["regions"])
            if actual != [opening_region]:
                errors.append(
                    f"physical opening {row['name']} maps to regions {actual}; "
                    f"expected only region {opening_region}"
                )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path)
    parser.add_argument("--expect-regions", type=int)
    parser.add_argument("--opening-region", type=int,
                        help="Require every physical patch to map only here")
    args = parser.parse_args()
    mesh = args.case / "constant/fluid/polyMesh"
    rows = audit(mesh, args.case / "0/fluid/cellToRegion")
    openings = defaultdict(list)
    for row in rows:
        mapping = ", ".join(f"region {key}: {value} faces"
                            for key, value in row["regions"].items())
        print(f"{row['name']} | {row['type']} | {mapping}")
        if row["type"] == "patch":
            for region in row["regions"]:
                openings[region].append(row["name"])
    print("\nPhysical openings by connected fluid region")
    for region, names in sorted(openings.items()):
        print(f"region {region}: {len(names)} patch(es): {', '.join(names)}")
    errors = validate(rows, args.expect_regions, args.opening_region)
    for error in errors:
        print(f"ERROR: {error}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
