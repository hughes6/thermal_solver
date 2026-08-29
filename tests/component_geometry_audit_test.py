import tempfile
import tomllib
import unittest
from pathlib import Path

from tools.audit_component_geometry import audit_component, audit_model


ROOT = Path(__file__).resolve().parents[1]


class ComponentGeometryAuditTest(unittest.TestCase):
    def test_updated_rack_devices_are_on_boundary_and_nonoverlapping(self):
        errors, _ = audit_model(
            ROOT / "library" / "models" / "new_model_updated.toml"
        )
        self.assertEqual(errors, [])

    def test_rejects_out_of_bounds_and_overlapping_roof_fans(self):
        model = """
[rack]
name = "test"
[rack.size]
units = "m"
width = 1.0
depth = 1.0
height = 1.0

[[fans]]
name = "first"
shape = "circular"
diameter = 0.4
diameter_units = "m"
[fans.position]
units = "m"
x = 0.1
y = 0.5
z = 1.0
[fans.direction]
x = 0.0
y = 0.0
z = 1.0

[[fans]]
name = "second"
shape = "circular"
diameter = 0.4
diameter_units = "m"
[fans.position]
units = "m"
x = 0.3
y = 0.5
z = 1.0
[fans.direction]
x = 0.0
y = 0.0
z = 1.0
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "model.toml"
            path.write_text(model, encoding="utf-8")
            errors, _ = audit_model(path)
        self.assertTrue(any("span" in error for error in errors))
        self.assertTrue(any("opening overlap" in error for error in errors))

    def test_unplaced_updated_r360_geometry_is_internally_bounded(self):
        errors, warnings = audit_component(
            ROOT / "library" / "components" / "updated_DELL_R360.toml"
        )
        self.assertEqual(errors, [])
        self.assertEqual(warnings, [])

    def test_rejects_vent_overlap_with_full_depth_solid_but_accepts_edge_touch(self):
        component = """
name = "blocked opening"
watts = 0.0
[size]
units = "mm"
width = 100.0
depth = 100.0
height = 100.0
[material]
rho = 2700.0
cp = 900.0
k = 150.0

[[internal_regions]]
name = "Interior air"
state = "air"
[internal_regions.position]
units = "mm"
x = 5.0
y = 5.0
z = 5.0
[internal_regions.size]
units = "mm"
width = 90.0
depth = 90.0
height = 90.0

[[internal_regions]]
name = "Full-depth separator"
state = "solid"
watts = 0.0
[internal_regions.position]
units = "mm"
x = 40.0
y = 0.0
z = 0.0
[internal_regions.size]
units = "mm"
width = 10.0
depth = 100.0
height = 100.0
[internal_regions.material]
rho = 1850.0
cp = 900.0
k = 8.0

[[internal_regions]]
name = "Front vent"
state = "vent"
shape = "rectangular"
free_area_ratio = 0.5
vent_discharge_coeff = 0.8
[internal_regions.position]
units = "mm"
x = 45.0
y = 0.0
z = 50.0
[internal_regions.size]
units = "mm"
width = 20.0
depth = 0.0
height = 20.0
[internal_regions.normal]
x = 0.0
y = 1.0
z = 0.0
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "component.toml"
            path.write_text(component, encoding="utf-8")
            errors, _ = audit_component(path)
            self.assertTrue(
                any("opening overlaps full-depth solid" in error for error in errors)
            )

            # Moving the vent so its left edge is exactly the separator's
            # right edge must not be treated as positive-area overlap.
            path.write_text(
                component.replace("x = 45.0\ny = 0.0\nz = 50.0", "x = 60.0\ny = 0.0\nz = 50.0"),
                encoding="utf-8",
            )
            errors, _ = audit_component(path)
            self.assertFalse(
                any("opening overlaps full-depth solid" in error for error in errors)
            )

    def test_rejects_solid_interpenetration_but_accepts_edge_touch(self):
        component = """
name = "solid overlap"
watts = 0.0
[size]
units = "mm"
width = 100.0
depth = 100.0
height = 100.0
[material]
rho = 2700.0
cp = 900.0
k = 150.0

[[internal_regions]]
name = "First solid"
state = "solid"
watts = 0.0
[internal_regions.position]
units = "mm"
x = 10.0
y = 10.0
z = 10.0
[internal_regions.size]
units = "mm"
width = 20.0
depth = 20.0
height = 20.0
[internal_regions.material]
rho = 1850.0
cp = 900.0
k = 8.0

[[internal_regions]]
name = "Second solid"
state = "solid"
watts = 0.0
[internal_regions.position]
units = "mm"
x = 25.0
y = 10.0
z = 10.0
[internal_regions.size]
units = "mm"
width = 20.0
depth = 20.0
height = 20.0
[internal_regions.material]
rho = 1850.0
cp = 900.0
k = 8.0
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "component.toml"
            path.write_text(component, encoding="utf-8")
            errors, _ = audit_component(path)
            self.assertTrue(any("solid/solid" in error for error in errors))

            path.write_text(component.replace("x = 25.0", "x = 30.0"), encoding="utf-8")
            errors, _ = audit_component(path)
            self.assertFalse(any("solid/solid" in error for error in errors))

    def test_rejects_component_surface_overlap_but_accepts_edge_touch(self):
        component = """
name = "surface overlap"
watts = 0.0
[size]
units = "mm"
width = 100.0
depth = 100.0
height = 100.0
[material]
rho = 2700.0
cp = 900.0
k = 150.0

[[internal_regions]]
name = "First vent"
state = "vent"
shape = "rectangular"
free_area_ratio = 0.5
vent_discharge_coeff = 0.8
[internal_regions.position]
units = "mm"
x = 35.0
y = 0.0
z = 50.0
[internal_regions.size]
units = "mm"
width = 30.0
depth = 0.0
height = 20.0
[internal_regions.normal]
x = 0.0
y = 1.0
z = 0.0

[[internal_regions]]
name = "Second vent"
state = "vent"
shape = "rectangular"
free_area_ratio = 0.5
vent_discharge_coeff = 0.8
[internal_regions.position]
units = "mm"
x = 45.0
y = 0.0
z = 50.0
[internal_regions.size]
units = "mm"
width = 30.0
depth = 0.0
height = 20.0
[internal_regions.normal]
x = 0.0
y = 1.0
z = 0.0
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "component.toml"
            path.write_text(component, encoding="utf-8")
            errors, _ = audit_component(path)
            self.assertTrue(any("surface overlap" in error for error in errors))

            path.write_text(component.replace("x = 45.0", "x = 65.0"), encoding="utf-8")
            errors, _ = audit_component(path)
            self.assertFalse(any("surface overlap" in error for error in errors))

    def test_circle_rectangle_corner_bbox_overlap_is_not_a_surface_conflict(self):
        component = """
name = "mixed surface geometry"
watts = 0.0
[size]
units = "mm"
width = 100.0
depth = 100.0
height = 100.0
[material]
rho = 2700.0
cp = 900.0
k = 150.0

[[internal_regions]]
name = "Circular vent"
state = "vent"
shape = "circular"
diameter = 20.0
diameter_units = "mm"
free_area_ratio = 0.5
vent_discharge_coeff = 0.8
[internal_regions.position]
units = "mm"
x = 30.0
y = 0.0
z = 30.0
[internal_regions.size]
units = "mm"
width = 20.0
depth = 0.0
height = 20.0
[internal_regions.normal]
x = 0.0
y = 1.0
z = 0.0

[[internal_regions]]
name = "Rectangular vent"
state = "vent"
shape = "rectangular"
free_area_ratio = 0.5
vent_discharge_coeff = 0.8
[internal_regions.position]
units = "mm"
x = 43.0
y = 0.0
z = 43.0
[internal_regions.size]
units = "mm"
width = 10.0
depth = 0.0
height = 10.0
[internal_regions.normal]
x = 0.0
y = 1.0
z = 0.0
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "component.toml"
            path.write_text(component, encoding="utf-8")
            errors, _ = audit_component(path)
            self.assertFalse(any("surface overlap" in error for error in errors))

            path.write_text(
                component.replace("x = 43.0\ny = 0.0\nz = 43.0", "x = 39.0\ny = 0.0\nz = 39.0"),
                encoding="utf-8",
            )
            errors, _ = audit_component(path)
            self.assertTrue(any("surface overlap" in error for error in errors))

    def test_every_installed_reusable_component_is_audited(self):
        with (
            ROOT / "library" / "models" / "new_model_updated.toml"
        ).open("rb") as source:
            model = tomllib.load(source)
        templates = list(dict.fromkeys(
            component["template"]
            for component in model["components"]
            if "template" in component
        ))
        self.assertEqual(len(templates), 11)

        all_errors = []
        all_warnings = []
        for template in templates:
            with self.subTest(template=template):
                errors, warnings = audit_component(ROOT / template)
                self.assertEqual(errors, [])
                all_errors.extend(errors)
                all_warnings.extend(warnings)

        self.assertEqual(all_errors, [])
        self.assertEqual(
            all_warnings,
            [
                "updated_NI_PXIe_Chassis.toml: region 1 (Interior air) "
                "overlaps updated_NI_PXIe_Chassis.toml: region 2 "
                "(Card Slot air) (air/air)"
            ],
        )


if __name__ == "__main__":
    unittest.main()
