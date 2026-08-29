#!/usr/bin/env python3
"""Audit material properties that OpenFOAM's one-region/component export loses."""

from __future__ import annotations

import argparse
import math
from pathlib import Path
import tomllib


PROPERTIES = ("rho", "cp", "k")
UNIT_SCALE = {"m": 1.0, "meter": 1.0, "meters": 1.0,
              "mm": 1e-3, "millimeter": 1e-3, "millimeters": 1e-3,
              "in": 0.0254, "inch": 0.0254, "inches": 0.0254,
              "u": 0.04445, "rack_unit": 0.04445, "rack_units": 0.04445}


def load_toml(path: Path) -> dict:
    with path.open("rb") as stream:
        return tomllib.load(stream)


def material_values(material: object, root: Path) -> dict[str, float] | None:
    if isinstance(material, str):
        material = load_toml(root / material)
    if not isinstance(material, dict):
        return None
    try:
        return {key: float(material[key]) for key in PROPERTIES}
    except (KeyError, TypeError, ValueError):
        return None


def differs(lhs: dict[str, float], rhs: dict[str, float]) -> bool:
    return any(
        not math.isclose(lhs[key], rhs[key], rel_tol=1e-9, abs_tol=1e-9)
        for key in PROPERTIES
    )


def region_volume(size: object) -> float | None:
    if not isinstance(size, dict):
        return None
    units = str(size.get("units", "m")).lower()
    scale = UNIT_SCALE.get(units)
    if scale is None:
        return None
    try:
        return math.prod(float(size[key]) * scale for key in ("width", "depth", "height"))
    except (KeyError, TypeError, ValueError):
        return None


def audit_model(model_path: Path) -> list[dict]:
    root = Path.cwd()
    model = load_toml(model_path)
    results = []
    for instance, entry in enumerate(model.get("components", [])):
        source = entry
        template = entry.get("template")
        if template:
            source = load_toml(root / template)
        name = str(source.get("name", entry.get("name", f"component_{instance}")))
        outer = material_values(source.get("material"), root)
        differing_regions = []
        solid_regions = 0
        for region in source.get("internal_regions", []):
            if str(region.get("state", "")).lower() != "solid":
                continue
            solid_regions += 1
            inner = material_values(region.get("material"), root)
            if outer is not None and inner is not None and differs(inner, outer):
                volume = region_volume(region.get("size"))
                differing_regions.append(
                    {"name": str(region.get("name", "unnamed")),
                     "material": inner,
                     "volume": volume,
                     "mass_delta": None if volume is None else
                         volume * (inner["rho"] - outer["rho"]),
                     "capacity_delta": None if volume is None else
                         volume * (inner["rho"] * inner["cp"] -
                                   outer["rho"] * outer["cp"])}
                )
        results.append(
            {
                "instance": instance,
                "name": name,
                "template": template or "<inline>",
                "outer": outer,
                "solid_regions": solid_regions,
                "differing_regions": differing_regions,
            }
        )
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", type=Path)
    args = parser.parse_args()
    rows = audit_model(args.model)
    affected = 0
    differing = 0
    affected_volume = 0.0
    mass_delta = 0.0
    capacity_delta = 0.0
    print("OpenFOAM component material-fidelity audit")
    print(f"Model: {args.model}")
    for row in rows:
        regions = row["differing_regions"]
        status = "HETEROGENEOUS" if regions else "homogeneous/no distinct solid material"
        print(f"[{row['instance']:02d}] {row['name']}: {status}")
        for region in regions:
            values = region["material"]
            print(
                f"     {region['name']}: rho={values['rho']:g}, "
                f"cp={values['cp']:g}, k={values['k']:g}, "
                + ("volume unavailable" if region["volume"] is None else
                   f"V={region['volume']:.9g} m3, "
                   f"delta(m)={region['mass_delta']:+.6g} kg, "
                   f"delta(C)={region['capacity_delta']:+.6g} J/K")
            )
            if region["volume"] is not None:
                affected_volume += region["volume"]
                mass_delta += region["mass_delta"]
                capacity_delta += region["capacity_delta"]
        if regions:
            affected += 1
            differing += len(regions)
    print(
        f"Summary: {len(rows)} component instances; {affected} heterogeneous "
        f"instances; {differing} distinct internal solid regions homogenized."
    )
    print(
        f"Exact source-region total: V={affected_volume:.9g} m3; "
        f"OpenFOAM-minus-defined mass={-mass_delta:+.6g} kg; "
        f"OpenFOAM-minus-defined capacity={-capacity_delta:+.6g} J/K."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
