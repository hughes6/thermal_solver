from argparse import Namespace
import math
from pathlib import Path
import sys
import tomllib
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.fan_curve_fitter import fit_curve, rpm_at_load, scale_curve_for_rpm
from tools.heat_load_estimator import estimate_methods
from tools.generic_server_characterizer import component_toml


class EngineeringToolsTest(unittest.TestCase):
    def test_fan_curve_exact_recovery(self) -> None:
        a, b, c = fit_curve([
            (0.0, 300.0),
            (0.05, 170.75),
            (0.10, 0.0),
        ])
        self.assertTrue(math.isclose(a, 300.0, abs_tol=1e-8))
        self.assertTrue(math.isclose(b, 2170.0, abs_tol=1e-6))
        self.assertTrue(math.isclose(c, 8300.0, abs_tol=1e-5))

    def test_rpm_load_interpolation_and_affinity_scaling(self) -> None:
        rpm = rpm_at_load([(0.0, 1000.0), (50.0, 2000.0),
                           (100.0, 3000.0)], 75.0)
        self.assertEqual(rpm, 2500.0)
        a, b, c = scale_curve_for_rpm(300.0, 2000.0, 8000.0,
                                      3000.0, 1500.0)
        self.assertEqual(a, 75.0)
        self.assertEqual(b, 1000.0)
        self.assertEqual(c, 8000.0)

    def test_heat_estimation_methods(self) -> None:
        args = Namespace(
            input_power_w=500.0,
            dc_load_w=450.0,
            efficiency=90.0,
            exported_power_w=10.0,
            mass_flow_kg_s=0.1,
            inlet_temp=25.0,
            outlet_temp=30.0,
            cp_air_j_kg_k=1005.5,
            surface_c=60.0,
            ambient_c=20.0,
            thermal_resistance_k_per_w=0.2,
            mass_kg=12.0,
            specific_heat_j_kg_k=700.0,
            start_c=25.0,
            end_c=35.0,
            duration_s=600.0,
        )
        estimates = estimate_methods(args)
        self.assertAlmostEqual(estimates["electrical_input"], 490.0)
        self.assertAlmostEqual(estimates["dc_load_and_efficiency"], 490.0)
        self.assertAlmostEqual(estimates["airflow_temperature"], 502.75)
        self.assertAlmostEqual(estimates["steady_temperature"], 200.0)
        self.assertAlmostEqual(estimates["transient_temperature"], 190.0)

    def test_airflow_estimation_requires_complete_positive_measurements(self) -> None:
        values = dict(
            input_power_w=None, dc_load_w=None, efficiency=None,
            exported_power_w=0.0, surface_c=None, ambient_c=None,
            thermal_resistance_k_per_w=None, mass_kg=None,
            specific_heat_j_kg_k=None, start_c=None, end_c=None,
            duration_s=None, mass_flow_kg_s=0.1, inlet_temp=25.0,
            outlet_temp=None, cp_air_j_kg_k=1005.5,
        )
        with self.assertRaisesRegex(ValueError, "Airflow estimation requires"):
            estimate_methods(Namespace(**values))
        values["outlet_temp"] = 24.0
        with self.assertRaisesRegex(ValueError, "outlet-temp"):
            estimate_methods(Namespace(**values))

    def test_generic_component_has_connected_air_tunnel_and_inset_fan(self) -> None:
        document = tomllib.loads(component_toml(
            "Test server", 1, 482.0, 700.0, 500.0, 0.05,
            "test_curve"))
        regions = {region["name"]: region for region in document["internal_regions"]}
        air = regions["Interior air"]
        fan = regions["Rear exhaust fan"]
        intake = regions["Front intake"]

        self.assertEqual(air["position"]["y"], 0.0)
        self.assertEqual(air["size"]["depth"], document["size"]["depth"])
        self.assertGreater(intake["position"]["y"], 0.0)
        self.assertGreater(fan["position"]["y"], intake["position"]["y"])
        self.assertLess(fan["position"]["y"], document["size"]["depth"])
        self.assertEqual(fan["size"]["width"], air["size"]["width"])
        self.assertEqual(fan["size"]["height"], air["size"]["height"])

    def test_kvm_component_has_no_forced_or_rear_exhaust(self) -> None:
        root = Path(__file__).resolve().parents[1]
        for filename in (
                "eaton_KVM.toml",
                "generic_kvm_1u.toml",
                "tripplite_b020_u08_19_ip_kvm.toml"):
            with (root / "library/components" / filename).open("rb") as stream:
                document = tomllib.load(stream)
            region_states = [region["state"] for region in document["internal_regions"]]
            self.assertNotIn("fan", region_states, filename)
            rear_exhausts = [
                region for region in document["internal_regions"]
                if "rear" in region["name"].lower()
                and "exhaust" in region["name"].lower()
            ]
            self.assertEqual([], rear_exhausts, filename)

    def test_generic_kvm_enclosure_is_resolved_by_standard_mesh(self) -> None:
        root = Path(__file__).resolve().parents[1]
        with (root / "library/components/generic_kvm_1u.toml").open("rb") as stream:
            document = tomllib.load(stream)
        air = next(
            region for region in document["internal_regions"]
            if region["name"] == "Interior air")
        minimum_wall_mm = 5.0
        for axis, size_key in (("x", "width"), ("y", "depth"), ("z", "height")):
            lower_wall = air["position"][axis]
            upper_wall = (
                document["size"][size_key] - lower_wall -
                air["size"][size_key])
            self.assertGreaterEqual(lower_wall, minimum_wall_mm)
            self.assertGreaterEqual(upper_wall, minimum_wall_mm)


if __name__ == "__main__":
    unittest.main()
