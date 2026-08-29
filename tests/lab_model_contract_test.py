import math
import tomllib
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODEL_PATH = ROOT / "library" / "models" / "new_model.toml"


class LabModelContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.model = tomllib.loads(MODEL_PATH.read_text(encoding="utf-8"))

    def test_rack_and_environment_match_lab_definition(self):
        self.assertEqual(
            self.model["rack"]["size"],
            {"units": "u", "width": 13.1, "depth": 24.6, "height": 40.0},
        )
        self.assertTrue(
            math.isclose(self.model["environment"]["rho"], 0.9833, abs_tol=1e-9)
        )
        self.assertEqual(self.model["environment"]["elevation"], 5500.0)
        self.assertEqual(self.model["environment"]["T_ambient"], 20.0)

    def test_all_expected_component_instances_are_present(self):
        components = self.model["components"]
        templates = [component.get("template") for component in components]
        self.assertEqual(len(components), 12)
        self.assertEqual(
            templates,
            [
                "library/components/eaton_2U_UPS.toml",
                "library/components/Keysight_N5766A.toml",
                "library/components/Keysight_N6701C.toml",
                "library/components/trenton_3u_bam.toml",
                "library/components/eaton_KVM.toml",
                None,
                "library/components/Thruster_Load_Box.toml",
                "library/components/cisco_catalyst_9300_24.toml",
                "library/components/EATON_PDU_PDUMNH30.toml",
                "library/components/Fan_control_kit_PS.toml",
                "library/components/Fan_control_kit_PS.toml",
                "library/components/NI_PXIe_Chassis.toml",
            ],
        )
        for template in (value for value in templates if value is not None):
            path = ROOT / template
            self.assertTrue(path.is_file(), template)
            with path.open("rb") as source:
                tomllib.load(source)

        ni = components[-1]
        self.assertEqual(ni["position"], {
            "units": "u", "x": 1.0, "y": 19.5, "z": 35
        })

    def test_roof_fan_array_and_front_vent_match_lab_definition(self):
        fans = self.model["fans"]
        self.assertEqual(len(fans), 9)
        self.assertEqual({fan["name"] for fan in fans}, {
            f"Fan {index}" for index in range(1, 10)
        })
        expected_centers = {
            (3.05, 14.8), (3.05, 17.8), (3.05, 20.8),
            (6.05, 14.8), (6.05, 17.8), (6.05, 20.8),
            (9.05, 14.8), (9.05, 17.8), (9.05, 20.8),
        }
        self.assertEqual({
            (fan["position"]["x"], fan["position"]["y"]) for fan in fans
        }, expected_centers)
        for fan in fans:
            self.assertEqual(fan["position"]["z"], 40.0)
            self.assertEqual(fan["diameter"], 4.7)
            self.assertEqual(fan["diameter_units"], "in")
            self.assertEqual(fan["cfm"], 271.0)
            self.assertEqual(fan["flow_type"], "exhaust")
            self.assertEqual(fan["curve"], "provisional_top_fan_271cfm")
            self.assertEqual(fan["direction"], {"x": 0.0, "y": 0.0, "z": 1.0})

        self.assertEqual(len(self.model["vents"]), 1)
        vent = self.model["vents"][0]
        self.assertEqual(vent["name"], "Vent main 2U 74% perforation")
        self.assertEqual(vent["free_area_ratio"], 0.74)
        self.assertEqual(vent["position"], {
            "units": "u", "x": 6.55, "y": 0.0, "z": 3.5
        })
        self.assertEqual(vent["size"], {
            "units": "u", "width": 9.5, "depth": 0.0, "height": 2.0
        })

    def test_dell_template_is_available_but_not_installed(self):
        dell = ROOT / "library" / "components" / "DELL_R360.toml"
        self.assertTrue(dell.is_file())
        with dell.open("rb") as source:
            tomllib.load(source)
        self.assertNotIn(
            "library/components/DELL_R360.toml",
            [component.get("template") for component in self.model["components"]],
        )

    def test_added_component_region_and_heat_inventory(self):
        # Dimensions and region inventories transcribed from the supplied lab
        # component definitions. This catches silent omissions while allowing
        # the separate geometry audit to handle detailed spatial bounds.
        expected = {
            # file: ((width, depth, height) mm, air, solid, fan, vent, watts)
            "DELL_R360.toml": ((482.0, 817.0, 43.0), 1, 4, 6, 2, 1000.0),
            "EATON_PDU_PDUMNH30.toml": (
                (444.5, 317.5, 88.9), 1, 1, 0, 0, 10.0
            ),
            "Fan_control_kit_PS.toml": (
                (40.0, 113.5, 125.2), 1, 1, 0, 2, 15.0
            ),
            "Keysight_N5766A.toml": (
                (422.8, 432.8, 43.6), 1, 3, 2, 3, 360.0
            ),
            "Keysight_N6701C.toml": (
                (425.0, 549.7, 44.45), 1, 3, 2, 3, 360.0
            ),
            # The unfinished NI definition intentionally has no solid heat
            # zones until its separator-wall/card geometry is supplied.
            "NI_PXIe_Chassis.toml": (
                (355.6, 214.2, 177.2), 2, 0, 2, 4, 0.0
            ),
            "Thruster_Load_Box.toml": (
                (482.6, 228.6, 88.9), 1, 1, 2, 4, 10.0
            ),
            "cisco_catalyst_9300_24.toml": (
                (445.0, 488.0, 44.0), 1, 3, 4, 5, 150.0
            ),
        }
        component_root = ROOT / "library" / "components"
        for filename, contract in expected.items():
            with self.subTest(component=filename):
                with (component_root / filename).open("rb") as source:
                    component = tomllib.load(source)
                size = component["size"]
                self.assertEqual(size["units"], "mm")
                self.assertEqual(
                    (size["width"], size["depth"], size["height"]), contract[0]
                )
                regions = component.get("internal_regions", [])
                counts = {
                    state: sum(region.get("state") == state for region in regions)
                    for state in ("air", "solid", "fan", "vent")
                }
                self.assertEqual(
                    tuple(counts[state] for state in ("air", "solid", "fan", "vent")),
                    contract[1:5],
                )
                watts = float(component.get("watts", 0.0)) + sum(
                    float(region.get("watts", 0.0)) for region in regions
                )
                self.assertTrue(math.isclose(watts, contract[5], abs_tol=1e-9))

    def test_keysight_rear_fans_have_matching_chassis_outlets(self):
        component_root = ROOT / "library" / "components"
        for filename in ("Keysight_N5766A.toml", "Keysight_N6701C.toml"):
            with self.subTest(component=filename):
                with (component_root / filename).open("rb") as source:
                    component = tomllib.load(source)
                regions = component["internal_regions"]
                fan = next(region for region in regions
                           if region.get("name") == "back exhaust fan")
                outlet = next(region for region in regions
                              if region.get("name") == "rear fan outlet")
                self.assertEqual(outlet["state"], "vent")
                self.assertEqual(outlet["size"], fan["size"])
                self.assertEqual(outlet["position"]["x"], fan["position"]["x"])
                self.assertEqual(outlet["position"]["z"], fan["position"]["z"])
                self.assertEqual(
                    outlet["position"]["y"], component["size"]["depth"]
                )
                self.assertEqual(outlet["normal"], fan["direction"])

    def test_installed_heat_is_allocated_to_the_intended_components(self):
        def configured_watts(component):
            template = component.get("template")
            if template is not None:
                with (ROOT / template).open("rb") as source:
                    definition = tomllib.load(source)
            else:
                definition = component
            return float(definition.get("watts", 0.0)) + sum(
                float(region.get("watts", 0.0))
                for region in definition.get("internal_regions", [])
            )

        watts_by_instance = [
            configured_watts(component) for component in self.model["components"]
        ]
        self.assertEqual(
            watts_by_instance,
            [150.0, 360.0, 360.0, 425.0, 20.0, 0.0,
             10.0, 150.0, 10.0, 15.0, 15.0, 0.0],
        )
        self.assertTrue(math.isclose(sum(watts_by_instance), 1515.0, abs_tol=1e-9))

    def test_thruster_and_cisco_rear_fans_have_matching_wall_outlets(self):
        for filename in (
            "Thruster_Load_Box.toml",
            "cisco_catalyst_9300_24.toml",
        ):
            with self.subTest(component=filename):
                with (ROOT / "library" / "components" / filename).open("rb") as source:
                    component = tomllib.load(source)
                regions = component["internal_regions"]
                fans = [region for region in regions if region.get("state") == "fan"]
                rear_outlets = [
                    region for region in regions
                    if region.get("state") == "vent"
                    and "outlet" in region.get("name", "").casefold()
                ]
                self.assertEqual(len(rear_outlets), len(fans))
                unmatched = list(rear_outlets)
                for fan in fans:
                    match = next(
                        (outlet for outlet in unmatched
                         if outlet["position"]["x"] == fan["position"]["x"]
                         and outlet["position"]["z"] == fan["position"]["z"]
                         and outlet["size"] == fan["size"]),
                        None,
                    )
                    self.assertIsNotNone(match, fan["name"])
                    self.assertEqual(
                        match["position"]["y"], component["size"]["depth"]
                    )
                    self.assertTrue(math.isclose(
                        match["position"]["y"] - fan["position"]["y"],
                        5.0, abs_tol=1e-9,
                    ))
                    self.assertEqual(match["normal"], {"x": 0.0, "y": 1.0, "z": 0.0})
                    unmatched.remove(match)

    def test_every_installed_fan_curve_resolves_to_finite_coefficients(self):
        with (ROOT / "library" / "fan_curves" / "fan_curves.toml").open("rb") as source:
            curves = {
                curve["name"]: curve for curve in tomllib.load(source)["fan_curve"]
            }
        fan_definitions = list(self.model["fans"])
        for component in self.model["components"]:
            if component.get("template") is not None:
                with (ROOT / component["template"]).open("rb") as source:
                    definition = tomllib.load(source)
            else:
                definition = component
            fan_definitions.extend(
                region
                for region in definition.get("internal_regions", [])
                if region.get("state") == "fan"
            )
        with (ROOT / "library" / "components" / "DELL_R360.toml").open("rb") as source:
            dell = tomllib.load(source)
        fan_definitions.extend(
            region for region in dell.get("internal_regions", [])
            if region.get("state") == "fan"
        )
        referenced = {fan["curve"] for fan in fan_definitions}

        for name in referenced:
            with self.subTest(curve=name):
                self.assertIn(name, curves)
                curve = curves[name]
                for field in ("rho_rated", "a", "b", "c"):
                    self.assertTrue(math.isfinite(float(curve[field])))
                self.assertGreater(float(curve["rho_rated"]), 0.0)
                self.assertGreater(float(curve["a"]), 0.0)
                self.assertGreaterEqual(float(curve["b"]), 0.0)
                self.assertGreaterEqual(float(curve["c"]), 0.0)
        for fan in fan_definitions:
            name = fan["curve"]
            if not name.startswith("provisional_"):
                continue
            curve = curves[name]
            a, b, c = (float(curve[field]) for field in ("a", "b", "c"))
            q_zero = (
                (-b + math.sqrt(b * b + 4.0 * c * a)) / (2.0 * c)
                if c > 0.0 else a / b
            )
            nominal = float(fan["cfm"]) * 0.00047194745
            with self.subTest(fan=fan["name"], curve=name):
                self.assertTrue(math.isclose(q_zero, nominal, rel_tol=1e-10))

    def test_smoke_and_export_variants_preserve_rack_physics(self):
        expected_components = [
            (component.get("template"), component["position"])
            for component in self.model["components"]
        ]
        expected_fans = [
            (fan["name"], fan["cfm"], fan["curve"], fan["position"])
            for fan in self.model["fans"]
        ]
        for filename in (
            "new_model_native_smoke.toml",
            "new_model_openfoam_export_test.toml",
        ):
            with self.subTest(model=filename):
                with (ROOT / "library" / "models" / filename).open("rb") as source:
                    variant = tomllib.load(source)
                self.assertEqual(variant["rack"], self.model["rack"])
                self.assertEqual(
                    [(item.get("template"), item["position"])
                     for item in variant["components"]],
                    expected_components,
                )
                self.assertEqual(
                    [(fan["name"], fan["cfm"], fan["curve"], fan["position"])
                     for fan in variant["fans"]],
                    expected_fans,
                )

if __name__ == "__main__":
    unittest.main()
