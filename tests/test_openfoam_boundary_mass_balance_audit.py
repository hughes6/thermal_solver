import importlib.util
import csv
from contextlib import redirect_stdout
import io
from pathlib import Path
import tempfile
import unittest


MODULE_PATH = Path(__file__).parents[1] / "tools" / "openfoam_boundary_mass_balance_audit.py"
SPEC = importlib.util.spec_from_file_location("mass_audit", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class MassBalanceAuditTest(unittest.TestCase):
    @staticmethod
    def write_flows(case, values):
        root = case / "postProcessing" / "fluid"
        for name, samples in values.items():
            for time, value in samples:
                directory = root / f"{name}_mass_flow" / str(time)
                directory.mkdir(parents=True)
                (directory / "surfaceFieldValue.dat").write_text(
                    f"# header\n{time} {value}\n", encoding="utf-8"
                )

    @staticmethod
    def write_devices(case, lines):
        path = case / "airflow_devices.txt"
        path.write_text(
            "OpenFOAM airflow-device connectivity report\n\n"
            "AMBIENT DEVICES\n" + "\n".join(lines) +
            "\n\nINTERNAL DEVICES\n"
            "- ignored | internal fan | cells=1\n",
            encoding="utf-8",
        )
        return path

    def test_groups_openings_and_uses_larger_flow_as_scale(self):
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary)
            root = case / "postProcessing" / "fluid"
            for name, values in {
                "opening_a_mass_flow": [(0.1, 3.0), (0.2, 4.0)],
                "opening_b_mass_flow": [(0.1, -2.7), (0.2, -4.1)],
            }.items():
                for time, value in values:
                    directory = root / name / str(time)
                    directory.mkdir(parents=True)
                    (directory / "surfaceFieldValue.dat").write_text(
                        f"# header\n{time} {value}\n", encoding="utf-8"
                    )
            ignored = root / "ambient_net_mass_flow" / "0.2"
            ignored.mkdir(parents=True)
            (ignored / "surfaceFieldValue.dat").write_text(
                "0.2 99\n", encoding="utf-8"
            )

            rows = MODULE.audit(case)
            self.assertEqual(len(rows), 2)
            self.assertEqual(rows[0]["opening_count"], 2)
            self.assertAlmostEqual(rows[0]["mismatch_fraction"], 0.1)
            self.assertAlmostEqual(rows[1]["mismatch_fraction"], 0.1 / 4.1)

    def test_latest_manifest_fans_are_direction_gated_and_vents_are_not(self):
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary)
            self.write_flows(case, {
                "Front_Intake": [(0.1, 0.2), (0.2, -1.1)],
                "Roof_Exhaust": [(0.1, -0.2), (0.2, 0.9)],
                "Side_Vent": [(0.2, 0.2)],
            })
            report = self.write_devices(case, [
                "- Front Intake | intake fan | faces=3",
                "- Roof Exhaust | exhaust fan | faces=4",
                "- Side Vent | passive vent | faces=5",
            ])

            devices = MODULE.read_ambient_devices(report)
            rows = MODULE.audit_openings(case, devices)

            self.assertEqual(len(rows), 3)
            self.assertTrue(all(row["time_s"] == 0.2 for row in rows))
            self.assertEqual(
                [row["direction_status"] for row in rows],
                ["PASS", "PASS", "NOT_GATED"],
            )
            self.assertEqual(rows[0]["observed_direction"], "inward")
            self.assertEqual(rows[1]["observed_direction"], "outward")
            self.assertEqual(MODULE.direction_failures(rows), [])

    def test_exchange_metrics_match_runner_trapezoidal_mass_accounting(self):
        rows = [
            {
                "time_s": 1.0,
                "outflow_kg_s": 1.0,
                "inflow_kg_s": 1.0,
            },
            {
                "time_s": 2.0,
                "outflow_kg_s": 1.0,
                "inflow_kg_s": 1.0,
            },
        ]

        enriched = MODULE.add_exchange_metrics(rows, 1.0, 2.0)

        self.assertAlmostEqual(enriched[0]["cumulative_exchanged_mass_kg"], 0.5)
        self.assertAlmostEqual(enriched[0]["cumulative_exchange_fraction"], 0.25)
        self.assertAlmostEqual(enriched[0]["projected_one_exchange_time_s"], 2.5)
        self.assertAlmostEqual(enriched[1]["cumulative_exchanged_mass_kg"], 1.5)
        self.assertAlmostEqual(enriched[1]["cumulative_exchange_fraction"], 0.75)
        self.assertAlmostEqual(enriched[1]["projected_one_exchange_time_s"], 2.5)

    def test_wrong_fans_and_missing_manifest_measurements_fail(self):
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary)
            self.write_flows(case, {
                "Front_Intake": [(0.2, 1.0)],
                "Roof_Exhaust": [(0.2, -1.0)],
            })
            report = self.write_devices(case, [
                "- Front Intake | intake fan | faces=3",
                "- Roof Exhaust | exhaust fan | faces=4",
                "- Missing Fan | exhaust fan | faces=2",
                "- Missing Vent | passive vent | faces=2",
            ])

            rows = MODULE.audit_openings(
                case, MODULE.read_ambient_devices(report)
            )
            failures = MODULE.direction_failures(rows)
            missing = MODULE.measurement_failures(rows)

            self.assertEqual(len(failures), 3)
            self.assertEqual(len(missing), 2)
            self.assertEqual(
                [row["direction_status"] for row in rows],
                ["FAIL", "FAIL", "MISSING", "NOT_GATED"],
            )
            self.assertEqual(rows[-1]["measurement_status"], "MISSING")

    def test_cli_preserves_mass_gate_and_writes_per_opening_csv(self):
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary)
            self.write_flows(case, {
                "Front_Intake": [(0.2, 1.0)],
                "Roof_Exhaust": [(0.2, -1.0)],
            })
            self.write_devices(case, [
                "- Front Intake | intake fan | faces=3",
                "- Roof Exhaust | exhaust fan | faces=4",
            ])
            output = case / "openings.csv"

            with redirect_stdout(io.StringIO()):
                status = MODULE.main([
                    str(case), "--airflow-devices",
                    "--openings-csv", str(output),
                ])

            self.assertEqual(status, 1)
            with output.open(newline="", encoding="utf-8") as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual(len(rows), 2)
            self.assertEqual(
                [row["direction_status"] for row in rows], ["FAIL", "FAIL"]
            )

    def test_legacy_cli_does_not_require_a_device_report(self):
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary)
            self.write_flows(case, {
                "Opening_A": [(0.2, 1.0)],
                "Opening_B": [(0.2, -1.0)],
            })

            with redirect_stdout(io.StringIO()):
                status = MODULE.main([str(case)])

            self.assertEqual(status, 0)

    def test_duplicate_normalized_manifest_names_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary)
            report = self.write_devices(case, [
                "- Fan A | exhaust fan | faces=3",
                "- Fan-A | exhaust fan | faces=4",
            ])
            with self.assertRaisesRegex(ValueError, "normalize"):
                MODULE.read_ambient_devices(report)


if __name__ == "__main__":
    unittest.main()
