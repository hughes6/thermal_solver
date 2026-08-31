"""Audit reusable component geometry and rack placements without meshing."""

from __future__ import annotations

import argparse
import itertools
import math
import sys
import tomllib
from pathlib import Path


UNIT_SCALE = {"m": 1.0, "mm": 1e-3, "cm": 1e-2, "in": 0.0254, "u": 0.04445}
SURFACE_STATES = {"fan", "vent"}


def vector(table: dict, keys: tuple[str, str, str]) -> tuple[float, float, float]:
    units = str(table.get("units", "m")).lower()
    scale = UNIT_SCALE[units]
    return tuple(float(table[key]) * scale for key in keys)


def size(table: dict) -> tuple[float, float, float]:
    return vector(table, ("width", "depth", "height"))


def position(table: dict) -> tuple[float, float, float]:
    return vector(table, ("x", "y", "z"))


def material_properties(
    value: object, label: str, errors: list[str]
) -> dict | None:
    """Resolve an inline material table or a repository-relative TOML file."""
    if isinstance(value, dict):
        return value
    if not isinstance(value, str) or not value:
        errors.append(f"{label}: material must be a table or non-empty file path")
        return None
    path = Path(value)
    if not path.is_absolute():
        path = Path.cwd() / path
    try:
        material = tomllib.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, tomllib.TOMLDecodeError) as exc:
        errors.append(f"{label}: unable to load material file {value}: {exc}")
        return None
    return material


def audit_material(value: object, label: str, errors: list[str]) -> None:
    material = material_properties(value, label, errors)
    if material is None:
        return
    for key in ("rho", "cp", "k"):
        try:
            property_value = float(material.get(key, 0.0))
        except (TypeError, ValueError):
            property_value = math.nan
        if not math.isfinite(property_value) or property_value <= 0.0:
            errors.append(f"{label}: material {key} must be finite and positive")


def aabb_overlap(a, b, tolerance=1e-9) -> bool:
    return all(
        a[0][axis] < b[1][axis] - tolerance
        and b[0][axis] < a[1][axis] - tolerance
        for axis in range(3)
    )


def coplanar_surface_overlap(first: dict, second: dict, tolerance=1e-9) -> bool:
    """Return true only for positive-area overlap on one shared plane."""
    if first["axis"] != second["axis"]:
        return False
    axis = first["axis"]
    if abs(first["center"][axis] - second["center"][axis]) > tolerance:
        return False

    tangents = tuple(value for value in range(3) if value != axis)
    if not first["is_circular"] and not second["is_circular"]:
        return all(
            first["footprint"][0][tangent]
            < second["footprint"][1][tangent] - tolerance
            and second["footprint"][0][tangent]
            < first["footprint"][1][tangent] - tolerance
            for tangent in tangents
        )

    if first["is_circular"] and second["is_circular"]:
        distance = sum(
            (first["center"][tangent] - second["center"][tangent]) ** 2
            for tangent in tangents
        ) ** 0.5
        return distance < first["radius"] + second["radius"] - tolerance

    circle, rectangle = (
        (first, second) if first["is_circular"] else (second, first)
    )
    effective_radius = circle["radius"] - tolerance
    if effective_radius <= 0.0:
        return False
    distance_squared = 0.0
    for tangent in tangents:
        center = circle["center"][tangent]
        lower = rectangle["footprint"][0][tangent]
        upper = rectangle["footprint"][1][tangent]
        if center < lower:
            distance_squared += (lower - center) ** 2
        elif center > upper:
            distance_squared += (center - upper) ** 2
    return distance_squared < effective_radius**2


def audit_component(path: Path) -> tuple[list[str], list[str]]:
    data = tomllib.loads(path.read_text(encoding="utf-8-sig"))
    outer = size(data["size"])
    errors: list[str] = []
    warnings: list[str] = []
    volumes = []
    surfaces = []

    outer_watts = float(data.get("watts", 0.0))
    if not math.isfinite(outer_watts) or outer_watts < 0.0:
        errors.append(f"{path.name}: outer watts must be finite and nonnegative")
    outer_material = data.get("material")
    if outer_material:
        audit_material(outer_material, f"{path.name}: outer material", errors)

    for index, region in enumerate(data.get("internal_regions", []), 1):
        label = f"{path.name}: region {index} ({region.get('name', 'unnamed')})"
        state = str(region.get("state", "")).lower()
        p = position(region["position"])
        s = size(region["size"])
        watts = float(region.get("watts", 0.0))
        if not math.isfinite(watts) or watts < 0.0:
            errors.append(f"{label}: watts must be finite and nonnegative")

        if state in SURFACE_STATES:
            direction = region.get("direction") or region.get("normal")
            if not direction:
                errors.append(f"{label}: missing direction/normal")
                continue
            normal = (float(direction["x"]), float(direction["y"]), float(direction["z"]))
            magnitude = sum(value * value for value in normal) ** 0.5
            if not math.isclose(magnitude, 1.0, rel_tol=1e-9, abs_tol=1e-9):
                errors.append(f"{label}: direction/normal magnitude is {magnitude:.6g}, expected 1")
            axis = max(range(3), key=lambda value: abs(normal[value]))
            zero_axes = [value for value in range(3) if abs(s[value]) <= 1e-9]
            is_circular = str(region.get("shape", "")).lower() == "circular"
            if abs(normal[axis]) <= 0.0:
                errors.append(f"{label}: zero direction/normal")
            if not is_circular and len(zero_axes) != 1:
                errors.append(f"{label}: expected exactly one zero-size surface axis, got {zero_axes}")
            elif not is_circular and zero_axes[0] != axis:
                errors.append(
                    f"{label}: zero-size plane axis {zero_axes[0]} does not match "
                    f"direction/normal axis {axis}"
                )
            if not is_circular and s[axis] > 1e-9:
                errors.append(f"{label}: surface has nonzero normal-axis thickness {s[axis]:.6g} m")
            if state == "vent" and min(abs(p[axis]), abs(p[axis] - outer[axis])) > 1e-6:
                errors.append(
                    f"{label}: surface center is {p[axis]:.6g} m on axis {axis}, "
                    f"not on enclosure face 0 or {outer[axis]:.6g} m"
                )
            if state == "vent":
                free_area = float(region.get("free_area_ratio", 0.0))
                discharge = float(region.get("vent_discharge_coeff", 0.0))
                if not math.isfinite(free_area) or not 0.0 < free_area <= 1.0:
                    errors.append(f"{label}: free_area_ratio must be in (0, 1]")
                if not math.isfinite(discharge) or not 0.0 < discharge <= 1.0:
                    errors.append(f"{label}: vent_discharge_coeff must be in (0, 1]")
            if state == "fan":
                cfm = float(region.get("cfm", 0.0))
                if not math.isfinite(cfm) or cfm <= 0.0:
                    errors.append(f"{label}: cfm must be finite and positive")
            diameter = float(region.get("diameter", 0.0)) * UNIT_SCALE.get(
                str(region.get("diameter_units", "m")).lower(), 1.0
            )
            for tangent in set(range(3)) - {axis}:
                extent = diameter if is_circular else s[tangent]
                lower = p[tangent] - extent / 2.0
                upper = p[tangent] + extent / 2.0
                if lower < -1e-6 or upper > outer[tangent] + 1e-6:
                    errors.append(
                        f"{label}: surface span [{lower:.6g}, {upper:.6g}] m "
                        f"exceeds enclosure axis {tangent} [0, {outer[tangent]:.6g}] m"
                    )
            footprint_lower = list(p)
            footprint_upper = list(p)
            for tangent in set(range(3)) - {axis}:
                extent = diameter if is_circular else s[tangent]
                footprint_lower[tangent] = p[tangent] - extent / 2.0
                footprint_upper[tangent] = p[tangent] + extent / 2.0
            surfaces.append(
                {
                    "label": label,
                    "state": state,
                    "axis": axis,
                    "center": p,
                    "footprint": (tuple(footprint_lower), tuple(footprint_upper)),
                    "is_circular": is_circular,
                    "radius": diameter / 2.0,
                }
            )
        else:
            upper = tuple(p[axis] + s[axis] for axis in range(3))
            for axis in range(3):
                if p[axis] < -1e-6 or upper[axis] > outer[axis] + 1e-6:
                    errors.append(
                        f"{label}: volume [{p[axis]:.6g}, {upper[axis]:.6g}] m "
                        f"exceeds enclosure axis {axis} [0, {outer[axis]:.6g}] m"
                    )
            volumes.append((label, state, (p, upper)))
            material = region.get("material")
            if state == "solid" and not material:
                errors.append(f"{label}: solid region is missing material")
            if material:
                audit_material(material, label, errors)

    for first, second in itertools.combinations(volumes, 2):
        if aabb_overlap(first[2], second[2]):
            if first[1] == "air" and second[1] == "air":
                warnings.append(f"{first[0]} overlaps {second[0]} (air/air)")
            elif first[1] != "air" and second[1] != "air":
                errors.append(f"{first[0]} overlaps {second[0]} (solid/solid)")

    # Air volumes are carving envelopes in the current component schema, so
    # both air/solid overlap and the unfinished nested/card-air layout can be
    # intentional.  Solid/solid interpenetration is unambiguous, as is two
    # boundary-condition surfaces claiming positive area on the same plane.
    for first, second in itertools.combinations(surfaces, 2):
        if coplanar_surface_overlap(first, second):
            errors.append(
                f"surface overlap: {first['label']} intersects {second['label']}"
            )

    # A boundary opening cannot overlap a solid separator that spans the
    # complete enclosure depth along the opening normal.  The mesh would have
    # no finite inward path from that part of the opening to the air cavity.
    # Treat edge contact as valid; only positive-area tangential overlap fails.
    tolerance = 1e-9
    for surface in surfaces:
        if surface["state"] != "vent":
            continue
        surface_label = surface["label"]
        axis = surface["axis"]
        footprint = surface["footprint"]
        tangent_axes = tuple(value for value in range(3) if value != axis)
        for volume_label, volume_state, bounds in volumes:
            if volume_state != "solid":
                continue
            spans_normal = (
                bounds[0][axis] <= tolerance
                and bounds[1][axis] >= outer[axis] - tolerance
            )
            tangential_overlap = all(
                footprint[0][tangent] < bounds[1][tangent] - tolerance
                and bounds[0][tangent] < footprint[1][tangent] - tolerance
                for tangent in tangent_axes
            )
            if spans_normal and tangential_overlap:
                errors.append(
                    f"{surface_label}: opening overlaps full-depth solid "
                    f"{volume_label}"
                )

    return errors, warnings


def audit_model(path: Path) -> tuple[list[str], list[str]]:
    data = tomllib.loads(path.read_text(encoding="utf-8-sig"))
    rack_size = size(data["rack"]["size"])
    errors: list[str] = []
    warnings: list[str] = []
    boxes = []

    for index, component in enumerate(data.get("components", []), 1):
        template = component.get("template")
        template_data = (
            tomllib.loads(Path(template).read_text(encoding="utf-8-sig")) if template else component
        )
        label = template_data.get("name", f"component {index}")
        p = position(component["position"])
        s = size(template_data["size"])
        upper = tuple(p[axis] + s[axis] for axis in range(3))
        for axis in range(3):
            if p[axis] < -1e-6 or upper[axis] > rack_size[axis] + 1e-6:
                errors.append(
                    f"{label}: rack axis {axis} span [{p[axis]:.6g}, {upper[axis]:.6g}] m "
                    f"exceeds [0, {rack_size[axis]:.6g}] m"
                )
        boxes.append((label, (p, upper)))

    for first, second in itertools.combinations(boxes, 2):
        if aabb_overlap(first[1], second[1]):
            errors.append(f"rack overlap: {first[0]} intersects {second[0]}")

    rack_surfaces = []
    for collection, state in (("fans", "fan"), ("vents", "vent")):
        for index, surface in enumerate(data.get(collection, []), 1):
            label = f"rack {state} {index} ({surface.get('name', 'unnamed')})"
            direction = surface.get("direction") or surface.get("normal")
            if not direction:
                errors.append(f"{label}: missing direction/normal")
                continue
            normal = tuple(float(direction[key]) for key in ("x", "y", "z"))
            magnitude = sum(value * value for value in normal) ** 0.5
            if not math.isclose(magnitude, 1.0, rel_tol=1e-9, abs_tol=1e-9):
                errors.append(f"{label}: direction/normal magnitude is {magnitude:.6g}, expected 1")
            axis = max(range(3), key=lambda value: abs(normal[value]))
            if abs(normal[axis]) <= 0.0:
                errors.append(f"{label}: zero direction/normal")
                continue
            p = position(surface["position"])
            if min(abs(p[axis]), abs(p[axis] - rack_size[axis])) > 1e-6:
                errors.append(
                    f"{label}: plane is {p[axis]:.6g} m on axis {axis}, "
                    f"not on rack face 0 or {rack_size[axis]:.6g} m"
                )
            circular = str(surface.get("shape", "")).lower() == "circular"
            if state == "fan":
                cfm = float(surface.get("cfm", 0.0))
                if not math.isfinite(cfm) or cfm <= 0.0:
                    errors.append(f"{label}: cfm must be finite and positive")
            else:
                free_area = float(surface.get("free_area_ratio", 0.0))
                discharge = float(surface.get("vent_discharge_coeff", 0.0))
                if not math.isfinite(free_area) or not 0.0 < free_area <= 1.0:
                    errors.append(f"{label}: free_area_ratio must be in (0, 1]")
                if not math.isfinite(discharge) or not 0.0 < discharge <= 1.0:
                    errors.append(f"{label}: vent_discharge_coeff must be in (0, 1]")
            if circular:
                diameter = float(surface.get("diameter", 0.0)) * UNIT_SCALE.get(
                    str(surface.get("diameter_units", "m")).lower(), 1.0
                )
                if diameter <= 0.0:
                    errors.append(f"{label}: circular opening has nonpositive diameter")
                tangential = {value: diameter for value in range(3) if value != axis}
            else:
                if "size" not in surface:
                    errors.append(f"{label}: rectangular opening is missing size")
                    continue
                s = size(surface["size"])
                if abs(s[axis]) > 1e-9:
                    errors.append(f"{label}: surface has nonzero normal-axis thickness")
                tangential = {value: s[value] for value in range(3) if value != axis}
            for tangent, extent in tangential.items():
                lower = p[tangent] - extent / 2.0
                upper = p[tangent] + extent / 2.0
                if lower < -1e-6 or upper > rack_size[tangent] + 1e-6:
                    errors.append(
                        f"{label}: span [{lower:.6g}, {upper:.6g}] m exceeds "
                        f"rack axis {tangent} [0, {rack_size[tangent]:.6g}] m"
                    )
            rack_surfaces.append((label, axis, p, tangential, circular))

    for first, second in itertools.combinations(rack_surfaces, 2):
        if first[1] != second[1] or abs(first[2][first[1]] - second[2][second[1]]) > 1e-6:
            continue
        tangents = [value for value in range(3) if value != first[1]]
        if first[4] and second[4]:
            distance = sum(
                (first[2][axis] - second[2][axis]) ** 2 for axis in tangents
            ) ** 0.5
            if distance < (first[3][tangents[0]] + second[3][tangents[0]]) / 2.0 - 1e-9:
                errors.append(f"rack opening overlap: {first[0]} intersects {second[0]}")
            continue
        overlaps = all(
            abs(first[2][axis] - second[2][axis])
            < (first[3][axis] + second[3][axis]) / 2.0 - 1e-9
            for axis in tangents
        )
        if overlaps:
            errors.append(f"rack opening overlap: {first[0]} intersects {second[0]}")

    return errors, warnings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("model", type=Path)
    parser.add_argument("components", nargs="*", type=Path)
    args = parser.parse_args()

    component_paths = args.components
    if not component_paths:
        model = tomllib.loads(args.model.read_text(encoding="utf-8-sig"))
        component_paths = sorted(
            {Path(item["template"]) for item in model.get("components", []) if "template" in item}
        )

    errors, warnings = audit_model(args.model)
    for component_path in component_paths:
        component_errors, component_warnings = audit_component(component_path)
        errors.extend(component_errors)
        warnings.extend(component_warnings)

    for warning in warnings:
        print(f"WARNING: {warning}")
    for error in errors:
        print(f"ERROR: {error}")
    print(
        f"Audited {len(component_paths)} reusable components: "
        f"{len(errors)} errors, {len(warnings)} warnings"
    )
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
