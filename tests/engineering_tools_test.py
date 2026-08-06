from argparse import Namespace
import math
from pathlib import Path
import sys
import tomllib
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.fan_curve_fitter import fit_curve
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

    def test_heat_estimation_methods(self) -> None:
        args = Namespace(
            input_power_w=500.0,
            dc_load_w=450.0,
            efficiency=90.0,
            exported_power_w=10.0,
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
        self.assertAlmostEqual(estimates["steady_temperature"], 200.0)
        self.assertAlmostEqual(estimates["transient_temperature"], 190.0)

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

    def test_kvm_component_is_fanless(self) -> None:
        root = Path(__file__).resolve().parents[1]
        for filename in ("eaton_KVM.toml", "generic_kvm_1u.toml"):
            with (root / "library/components" / filename).open("rb") as stream:
                document = tomllib.load(stream)
            region_states = [region["state"] for region in document["internal_regions"]]
            self.assertNotIn("fan", region_states, filename)


if __name__ == "__main__":
    unittest.main()
