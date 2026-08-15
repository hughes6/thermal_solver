import math
import tempfile
import unittest
from pathlib import Path

from tools.porous_obstruction_calculator import (
    PressurePoint,
    calculate,
    fit_pressure_points,
    parser,
    pressure_prediction,
    read_pressure_csv,
    write_outputs,
)


class PorousObstructionCalculatorTest(unittest.TestCase):
    def test_hole_geometry_generates_percentage_and_coefficient(self):
        args = parser().parse_args([
            "--name", "test tray", "--size", "0.5,0.01,1.2",
            "--direction", "y", "--hole-count", "100",
            "--hole-diameter-mm", "10", "--discharge-coefficient", "0.65",
        ])
        result = calculate(args)
        expected_area = 100 * math.pi * 0.01 ** 2 / 4
        expected_percent = 100 * expected_area / (0.5 * 1.2)
        self.assertAlmostEqual(result["open_area_m2"], expected_area)
        self.assertAlmostEqual(result["porosity_percent"], expected_percent)
        expected_f = 1 / (0.65 ** 2 * (expected_percent / 100) ** 2 * 0.01)
        self.assertAlmostEqual(
            result["derived_forchheimer_coefficient_1_m"], expected_f)
        self.assertIn("porosity_percent", result["toml"])

    def test_recovers_known_darcy_forchheimer_curve(self):
        length, viscosity, density = 0.15, 1.81e-5, 1.225
        expected_d, expected_f = 2.5e6, 850.0
        points = [PressurePoint(
            velocity,
            pressure_prediction(velocity, length, viscosity, density,
                                expected_d, expected_f),
        ) for velocity in (0.25, 0.5, 1.0, 2.0)]
        fit = fit_pressure_points(points, length, viscosity, density)
        self.assertAlmostEqual(fit["darcy_coefficient_1_m2"], expected_d,
                               delta=expected_d * 1e-10)
        self.assertAlmostEqual(fit["forchheimer_coefficient_1_m"], expected_f,
                               delta=expected_f * 1e-10)
        self.assertLess(fit["rmse_pa"], 1e-10)
        self.assertAlmostEqual(fit["r_squared"], 1.0)

    def test_cable_void_is_reported_with_uncalibrated_warning(self):
        args = parser().parse_args([
            "--size", "0.4,0.15,1.4", "--direction", "y",
            "--cable", "20,8,1.0", "--use-cable-void-as-porosity",
            "--discharge-coefficient", "0.65",
        ])
        result = calculate(args)
        solid = 20 * math.pi * 0.008 ** 2 / 4
        expected_void = 100 * (1 - solid / (0.4 * 0.15 * 1.4))
        self.assertAlmostEqual(result["cable_solid_volume_m3"], solid)
        self.assertAlmostEqual(result["cable_void_percent"], expected_void)
        self.assertEqual(result["porosity_percent"], result["cable_void_percent"])
        self.assertEqual(len(result["warnings"]), 2)

    def test_outputs_include_fit_table_plot_and_toml(self):
        args = parser().parse_args([
            "--size", "0.4,0.15,1.4", "--direction", "y",
            "--pressure-point", "0.5,20", "--pressure-point", "1.0,70",
            "--pressure-point", "2.0,260",
        ])
        result = calculate(args)
        with tempfile.TemporaryDirectory() as directory:
            paths = write_outputs(result, Path(directory) / "fit")
            suffixes = {path.suffix for path in paths}
            self.assertEqual(suffixes, {".json", ".md", ".toml", ".csv", ".svg"})
            self.assertIn("darcy_coefficient", (Path(directory) / "fit.toml").read_text())

    def test_pressure_csv_accepts_flow_and_converts_to_velocity(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "points.csv"
            path.write_text(
                "flow_m3_s,pressure_pa\n0.3,10\n0.6,40\n",
                encoding="utf-8")
            points = read_pressure_csv(path, 0.6)
        self.assertEqual(points, [PressurePoint(0.5, 10.0),
                                  PressurePoint(1.0, 40.0)])

    def test_rejects_overfilled_cable_zone(self):
        args = parser().parse_args([
            "--size", "0.01,0.01,0.01", "--direction", "y",
            "--cable", "100,10,1", "--use-cable-void-as-porosity",
            "--discharge-coefficient", "0.65",
        ])
        with self.assertRaisesRegex(ValueError, "equals or exceeds"):
            calculate(args)

    def test_rejects_signed_or_reversed_pressure_fit_input(self):
        with self.assertRaisesRegex(ValueError, "positive velocity"):
            fit_pressure_points(
                [PressurePoint(-1.0, -10.0), PressurePoint(1.0, 10.0)],
                0.1, 1.81e-5, 1.225)

    def test_toml_name_is_escaped_and_json_inputs_are_preserved(self):
        args = parser().parse_args([
            "--name", 'Cable "A"', "--size", "0.4,0.15,1.4",
            "--direction", "y", "--porosity-percent", "55",
            "--discharge-coefficient", "0.65",
        ])
        result = calculate(args)
        self.assertIn('name = "Cable \\"A\\""', result["toml"])
        self.assertEqual(result["inputs"]["size_m"], [0.4, 0.15, 1.4])
        self.assertEqual(result["inputs"]["specified_porosity_percent"], 55.0)


if __name__ == "__main__":
    unittest.main()
