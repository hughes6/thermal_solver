import math
import tempfile
import unittest
from pathlib import Path

from plot.recirculation_report import (
    boundary_patch_names,
    boundary_flow_floors,
    boundary_histories,
    combined_samples,
    directional_patch_sample,
    exported_heat_watts,
    internal_device_temperature_rows,
    read_report,
    selected_time_path,
    selected_result_paths,
    solver_postprocess_command,
)


class RecirculationReportTest(unittest.TestCase):
    def test_solver_postprocess_command_loads_endpoint_fields(self):
        command = solver_postprocess_command(Path("example_case"))
        self.assertIn("semiFrozenChtMultiRegionFoam -postProcess", command)
        self.assertIn("-latestTime", command)
        self.assertIn("-case '", command)

    def test_face_resolved_bidirectional_patch(self):
        sample = directional_patch_sample(
            [-0.3, -0.2, 0.4], [293.0, 295.0, 305.0])
        self.assertAlmostEqual(sample[0], -0.1)
        self.assertAlmostEqual(sample[1], 0.5)
        self.assertAlmostEqual(sample[2], 0.4)
        self.assertAlmostEqual(sample[3], 293.8)
        self.assertAlmostEqual(sample[4], 305.0)
        self.assertAlmostEqual(sample[5], 0.4 / 0.9)

    def test_external_patch_names_exclude_walls_and_mapped_interfaces(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            mesh = case / "constant" / "fluid" / "polyMesh"
            mesh.mkdir(parents=True)
            (mesh / "boundary").write_text(
                """3
(
inlet
{
    type patch;
}
rack_walls
{
    type wall;
}
fluid_to_solid
{
    type mappedWall;
}
)
""",
                encoding="utf-8",
            )
            self.assertEqual(boundary_patch_names(case), ["inlet"])

    def test_selected_time_path_uses_numeric_tolerance(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            checkpoint = case / "24000.009999999991"
            checkpoint.mkdir()
            value, path = selected_time_path(case, 24000.01)
            self.assertAlmostEqual(value, 24000.01)
            self.assertEqual(path, checkpoint)

    def test_selected_result_paths_use_all_decomposed_ranks(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            expected = []
            for rank in range(2):
                checkpoint = case / f"processor{rank}" / "24000.009999999991"
                checkpoint.mkdir(parents=True)
                expected.append(checkpoint)
            value, paths = selected_result_paths(case, 24000.01)
            self.assertAlmostEqual(value, 24000.01)
            self.assertEqual(paths, expected)

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

    def test_internal_device_air_rise_pairs_adjacent_intake_and_exhaust(self):
        with tempfile.TemporaryDirectory() as directory:
            fluid = Path(directory) / "postProcessing" / "fluid"
            intake = fluid / "internal_Front_intake_2_temperature_average" / "0"
            exhaust = (
                fluid / "internal_Rear_exhaust_fan_3_temperature_average" / "0"
            )
            unrelated = fluid / "internal_Front_intake_4_temperature_average" / "0"
            intake.mkdir(parents=True)
            exhaust.mkdir(parents=True)
            unrelated.mkdir(parents=True)
            (intake / "volFieldValue.dat").write_text(
                "10 299\n", encoding="utf-8"
            )
            (exhaust / "volFieldValue.dat").write_text(
                "10 313\n", encoding="utf-8"
            )
            (unrelated / "volFieldValue.dat").write_text(
                "10 301\n", encoding="utf-8"
            )

            rows = internal_device_temperature_rows(
                Path(directory), ambient_k=293.0
            )
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0][1], "internal_pair_2_3")
            self.assertAlmostEqual(rows[0][4], 299.0)
            self.assertAlmostEqual(rows[0][5], 313.0)
            self.assertAlmostEqual(rows[0][6], 0.3)

    def test_internal_device_metadata_labels_component_pair(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            fluid = case / "postProcessing" / "fluid"
            intake = fluid / "internal_Intake_8_temperature_average" / "0"
            exhaust = fluid / "internal_Fan_9_temperature_average" / "0"
            intake.mkdir(parents=True)
            exhaust.mkdir(parents=True)
            (intake / "volFieldValue.dat").write_text(
                "10 296\n", encoding="utf-8"
            )
            (exhaust / "volFieldValue.dat").write_text(
                "10 306\n", encoding="utf-8"
            )
            (case / "internal_airflow_devices.csv").write_text(
                "zone,component_id,component,kind,device\n"
                'internal_Intake_8,4,"Server A",intake,"Intake"\n'
                'internal_Fan_9,4,"Server A",exhaust,"Fan"\n',
                encoding="utf-8",
            )

            rows = internal_device_temperature_rows(case, ambient_k=293.0)
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0][1], "Server A")


if __name__ == "__main__":
    unittest.main()
