"""Audit signed flow through cyclic pressure-jump fan patches."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.validate_openfoam_case import internal_values, latest_result_paths, patch_values


MASTER_RE = re.compile(
    r"(?m)^\s*(internal_[A-Za-z0-9_]+_pressure_jump_master)\s*\n\s*\{")


def boundary_files(case: Path, result_paths: list[Path]) -> list[Path]:
    if len(result_paths) == 1 and result_paths[0].parent == case:
        return [case / "constant" / "fluid" / "polyMesh" / "boundary"]
    return [path.parents[1] / "constant" / "fluid" / "polyMesh" / "boundary"
            for path in result_paths]


def audit(case: Path) -> tuple[float, dict[str, tuple[float, float]]]:
    time_s, paths = latest_result_paths(case)
    patch_names: set[str] = set()
    for boundary in boundary_files(case, paths):
        text = boundary.read_text(encoding="latin-1", errors="replace")
        patch_names.update(MASTER_RE.findall(text))
    if not patch_names:
        raise ValueError("no cyclic internal-fan master patches found")

    result: dict[str, tuple[float, float]] = {}
    for patch in sorted(patch_names):
        mass_flow = 0.0
        volume_flow = 0.0
        seen = False
        for path in paths:
            phi = patch_values(path / "fluid" / "phi", patch)
            if not phi:
                continue
            rho_field = path / "fluid" / "rho"
            rho = patch_values(rho_field, patch)
            if not rho:
                region_rho = internal_values(rho_field)
                if not region_rho:
                    raise ValueError(f"no density values available on rank for {patch}")
                rho = [sum(region_rho) / len(region_rho)]
            if len(rho) == 1:
                rho *= len(phi)
            if len(phi) != len(rho):
                raise ValueError(f"phi/rho face counts differ on {patch}")
            if any(value <= 0.0 for value in rho):
                raise ValueError(f"nonpositive density on {patch}")
            mass_flow += sum(phi)
            volume_flow += sum(flux / density
                               for flux, density in zip(phi, rho))
            seen = True
        if not seen:
            raise ValueError(f"no face values found for {patch}")
        name = patch.removesuffix("_pressure_jump_master")
        result[name] = (mass_flow, volume_flow)
    return time_s, result


def markdown(time_s: float, flows: dict[str, tuple[float, float]]) -> str:
    lines = [
        f"| Cyclic internal fan | t={time_s:g} mass flow (kg/s) | "
        "estimated volume flow (m^3/s) |",
        "|---|---:|---:|",
    ]
    for name, (mass, volume) in flows.items():
        marker = " ⚠ reverse" if volume <= 0.0 else ""
        lines.append(f"| `{name}` | {mass:.7g} | {volume:.7g}{marker} |")
    failures = sum(volume <= 0.0 for _, volume in flows.values())
    lines.append(
        f"Audited {len(flows)} cyclic fans; nonpositive flows: {failures}."
    )
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path)
    parser.add_argument("--markdown", type=Path,
                        help="write the fan-flow audit as Markdown")
    args = parser.parse_args()
    time_s, flows = audit(args.case.resolve())
    report = markdown(time_s, flows)
    if args.markdown:
        args.markdown.write_text(report, encoding="utf-8")
        print(f"Wrote {args.markdown}")
    else:
        print(report, end="")
    failures = sum(volume <= 0.0 for _, volume in flows.values())
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
