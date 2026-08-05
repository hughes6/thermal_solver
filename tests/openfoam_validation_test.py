import tempfile
import unittest
from pathlib import Path

from tools.validate_openfoam_case import signed_weighted_average


class SignedOutletAverageTests(unittest.TestCase):
    def test_reverse_flow_uses_net_flux(self):
        temperatures = [300.0, 293.0]
        fluxes = [0.010, -0.002]
        self.assertAlmostEqual(
            signed_weighted_average(temperatures, fluxes), 301.75
        )

    def test_zero_net_flow_is_rejected(self):
        with self.assertRaises(ValueError):
            signed_weighted_average([300.0, 293.0], [0.01, -0.01])


if __name__ == "__main__":
    unittest.main()
