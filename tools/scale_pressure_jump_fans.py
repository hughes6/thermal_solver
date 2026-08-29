"""Set an absolute pressure scale on converted cyclic fan jump tables."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


def scaled_table(points: list[list[float]], scale: float) -> bytes:
    rows = "\n".join(f"({q:.17g} {dp * scale:.17g})" for q, dp in points)
    return f"values\n{len(points)}\n(\n{rows}\n)\n;".encode("ascii")


def scale_field(path: Path, curves: dict[str, list[list[float]]], scale: float) -> int:
    data = path.read_bytes()
    changed = 0
    for name, points in curves.items():
        patch = f"{name}_pressure_jump_master".encode("ascii")
        start = data.find(patch, data.find(b"boundaryField"))
        if start < 0:
            continue
        match = re.search(
            rb"\bvalues\s*(?:\d+\s*)?\(.*?\)\s*;",
            data[start:],
            re.DOTALL,
        )
        if not match:
            raise ValueError(f"no jumpTable values for {patch.decode()} in {path}")
        begin, end = start + match.start(), start + match.end()
        data = data[:begin] + scaled_table(points, scale) + data[end:]
        changed += 1
    if changed:
        path.write_bytes(data)
    return changed


def numeric_times(root: Path) -> list[Path]:
    result = []
    for path in root.iterdir() if root.is_dir() else ():
        if path.is_dir():
            try:
                float(path.name)
                result.append(path)
            except ValueError:
                pass
    return result


def scale_case(case: Path, scale: float) -> tuple[int, int]:
    if not 0.0 <= scale <= 1.0:
        raise ValueError("fan pressure scale must be between 0 and 1")
    manifest = case / "constant" / "fluid" / "pressureJumpFanCurves.json"
    curves = json.loads(manifest.read_text(encoding="utf-8"))["fans"]
    targets = [case / "0" / "fluid" / "p_rgh"]
    processors = sorted(case.glob("processor[0-9]*"))
    if processors:
        common = None
        for processor in processors:
            times = {float(p.name): p for p in numeric_times(processor)}
            common = set(times) if common is None else common & set(times)
        if common:
            latest = max(common)
            for processor in processors:
                match = next(p for p in numeric_times(processor)
                             if float(p.name) == latest)
                targets.append(match / "fluid" / "p_rgh")
    files = fans = 0
    for target in targets:
        if target.is_file():
            count = scale_field(target, curves, scale)
            files += count > 0
            fans += count
    if not fans:
        raise ValueError("no cyclic fan jump tables found in restart fields")
    return files, fans


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path)
    parser.add_argument("scale", type=float)
    args = parser.parse_args()
    files, fans = scale_case(args.case.resolve(), args.scale)
    print(f"Applied cyclic fan pressure scale {args.scale:g} to {fans} patches "
          f"across {files} fields.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
