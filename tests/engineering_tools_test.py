from argparse import Namespace
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.fan_curve_fitter import fit_curve
from tools.heat_load_estimator import estimate_methods


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


if __name__ == "__main__":
    unittest.main()
