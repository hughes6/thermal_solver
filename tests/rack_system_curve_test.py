import tempfile
import unittest
from pathlib import Path

from tools.rack_system_curve import (
    FLOW_TO_M3S,
    SystemPoint,
    bank_pressure,
    fan_pressure_tables,
    fit_system_curve,
    operating_point,
)


class RackSystemCurveTests(unittest.TestCase):
    def test_reads_exported_fan_pressure_table(self):
        content = """boundaryField
{
    Fan_1
    {
        type fanPressure;
        fanCurve
        {
            type table;
            values ((0 300) (0.1 150) (0.2 0));
        }
    }
    Vent_1
    {
        type pressureInletOutletVelocity;
    }
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "p_rgh"
            path.write_text(content, encoding="ascii")
            self.assertEqual(
                fan_pressure_tables(path)["Fan_1"],
                [(0.0, 300.0), (0.1, 150.0), (0.2, 0.0)],
            )

    def test_recovers_linear_plus_quadratic_system_curve(self):
        expected_b, expected_c = 72.0, 1650.0
        points = [
            SystemPoint("test", None, q, expected_b * q + expected_c * q * q)
            for q in (0.10, 0.22, 0.39, 0.51)
        ]
        b, c, method = fit_system_curve(points)
        self.assertAlmostEqual(b, expected_b, places=8)
        self.assertAlmostEqual(c, expected_c, places=8)
        self.assertIn("least-squares", method)

    def test_one_point_is_explicit_quadratic_estimate(self):
        b, c, method = fit_system_curve([SystemPoint("test", None, 0.5, 100.0)])
        self.assertEqual(b, 0.0)
        self.assertEqual(c, 400.0)
        self.assertIn("single-point", method)

    def test_parallel_fan_intersection_balances_pressures(self):
        curve = (300.0, 2170.0, 8300.0, 1.2)
        flow, pressure = operating_point(40.0, 800.0, curve, 3, 1.2)
        self.assertGreater(flow, 0.0)
        self.assertAlmostEqual(
            pressure, bank_pressure(flow, curve, 3, 1.2), places=8
        )
        self.assertGreater(flow / 3 / FLOW_TO_M3S["cfm"], 0.0)


if __name__ == "__main__":
    unittest.main()
