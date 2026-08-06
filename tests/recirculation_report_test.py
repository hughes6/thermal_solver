import math
import tempfile
import unittest
from pathlib import Path

from plot.recirculation_report import (
    boundary_flow_floors,
    boundary_histories,
    combined_samples,
    exported_heat_watts,
    read_report,
)


class RecirculationReportTest(unittest.TestCase):
    def test_reingestion_and_net_sensible_heat(self):
        histories = {
            "intake": {
                "flow": {10.0: -1.0},
                "temperature": {10.0: 300.0},
            },
            "exhaust": {
                "flow": {10.0: 1.0},
                "temperature": {10.0: 310.0},
            },
        }
        rows = combined_samples(histories, ambient_k=290.0, cp_air=1000.0)
        self.assertEqual(len(rows), 1)
        self.assertAlmostEqual(rows[0][1], 1.0)
        self.assertAlmostEqual(rows[0][2], 1.0)
        self.assertAlmostEqual(rows[0][5], 0.5)
        self.assertAlmostEqual(rows[0][6], 10000.0)

    def test_missing_exhaust_produces_nan_diagnostics(self):
        histories = {
            "intake": {
                "flow": {2.0: -0.5},
                "temperature": {2.0: 293.15},
            },
        }
        row = combined_samples(histories, ambient_k=293.15)[0]
        self.assertTrue(math.isnan(row[4]))
        self.assertTrue(math.isnan(row[5]))
        self.assertTrue(math.isnan(row[6]))

    def test_negligible_flow_temperature_is_excluded(self):
        histories = {
            "intake": {
                "flow": {10.0: -1.0},
                "temperature": {10.0: 300.0},
            },
            "exhaust": {
                "flow": {10.0: 1.0},
                "temperature": {10.0: 310.0},
            },
            "stagnant_kvm": {
                "flow": {10.0: -2.0e-9},
                "temperature": {10.0: -215008.0},
            },
        }
        floor = boundary_flow_floors(histories)[10.0]
        self.assertGreater(floor, abs(histories["stagnant_kvm"]["flow"][10.0]))
        row = combined_samples(histories, ambient_k=290.0, cp_air=1000.0)[0]
        self.assertAlmostEqual(row[1], 1.0)
        self.assertAlmostEqual(row[2], 1.0)
        self.assertAlmostEqual(row[3], 300.0)
        self.assertAlmostEqual(row[4], 310.0)
        self.assertAlmostEqual(row[5], 0.5)
        self.assertAlmostEqual(row[6], 10000.0)

    def test_exported_heat_is_summed(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            constant = case / "constant"
            constant.mkdir()
            (constant / "openfoamExportProperties").write_text(
                "watts 50;\nwatts 125.5;\n", encoding="utf-8"
            )
            self.assertAlmostEqual(exported_heat_watts(case), 175.5)

    def test_restart_suffixed_surface_reports_are_merged(self):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory)
            first = report / "0"
            restarted = report / "2400.03"
            first.mkdir()
            restarted.mkdir()
            (first / "surfaceFieldValue.dat").write_text(
                "# Time value\n10 1.0\n", encoding="utf-8"
            )
            (restarted / "surfaceFieldValue_2400.03.dat").write_text(
                "# Time value\n2700 2.0\n", encoding="utf-8"
            )
            self.assertEqual(read_report(report), {10.0: 1.0, 2700.0: 2.0})

    def test_boundary_flow_and_temperature_reports_are_paired(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            flow = case / "postProcessing" / "fluid" / "outlet_mass_flow" / "0"
            temperature = (
                case / "postProcessing" / "fluid"
                / "outlet_mass_weighted_temperature" / "0"
            )
            flow.mkdir(parents=True)
            temperature.mkdir(parents=True)
            (flow / "surfaceFieldValue.dat").write_text(
                "10 0.2\n", encoding="utf-8"
            )
            (temperature / "surfaceFieldValue.dat").write_text(
                "10 300\n", encoding="utf-8"
            )
            histories = boundary_histories(case)
            self.assertEqual(histories["outlet"]["flow"], {10.0: 0.2})
            self.assertEqual(histories["outlet"]["temperature"], {10.0: 300.0})


if __name__ == "__main__":
    unittest.main()
