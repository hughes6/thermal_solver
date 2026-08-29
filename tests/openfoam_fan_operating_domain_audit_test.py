import tempfile
import unittest
from pathlib import Path
from contextlib import redirect_stdout
import io

from tools.openfoam_fan_operating_domain_audit import (
    FanCurve,
    audit_case,
    classify,
    main,
    positive_pressure_limit,
    read_exported_curves,
)


class FanOperatingDomainAuditTest(unittest.TestCase):
    @staticmethod
    def curve(name="fan", direction="forward"):
        return FanCurve(
            name=name,
            direction=direction,
            points=((0.0, 100.0), (0.5, 50.0), (1.0, 0.0), (1.5, 0.0)),
            source="fixture",
        )

    def test_interpolates_first_positive_to_negative_crossing(self):
        self.assertAlmostEqual(
            positive_pressure_limit(((0.0, 10.0), (1.0, 2.0), (2.0, -2.0))),
            1.5,
        )
        self.assertIsNone(positive_pressure_limit(((0.0, 10.0), (1.0, 2.0))))

    def test_classifies_below_near_at_and_above_limit(self):
        curve = self.curve()
        self.assertEqual(classify("internal", "fan", 1, 0.89, None, curve, 0.9).status, "PASS")
        self.assertEqual(classify("internal", "fan", 1, 0.90, None, curve, 0.9).status, "WARN_NEAR_LIMIT")
        self.assertEqual(classify("internal", "fan", 1, 1.00, None, curve, 0.9).status, "FAIL_OUTSIDE_CURVE")
        self.assertEqual(classify("internal", "fan", 1, 1.20, None, curve, 0.9).status, "FAIL_OUTSIDE_CURVE")

    def test_missing_reverse_and_unknown_limit_cannot_false_pass(self):
        curve = self.curve()
        self.assertEqual(classify("internal", "fan", 1, None, None, curve, 0.9).status, "FAIL_MISSING")
        self.assertEqual(classify("internal", "fan", 1, -0.1, None, curve, 0.9).status, "FAIL_DIRECTION")
        no_limit = FanCurve("fan", "forward", ((0.0, 10.0), (1.0, 1.0)), "fixture")
        self.assertEqual(classify("internal", "fan", 1, 0.5, None, no_limit, 0.9).status, "FAIL_NO_LIMIT")

    def test_reads_internal_and_nested_boundary_tables(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            internal = root / "fvOptions"
            internal.write_text(
                "fan_a\n{\n type fanMomentumSource;\n fanCurve\n {\n"
                "  type table; values ((0 10) (1 0) (2 0));\n }\n}\n",
                encoding="utf-8",
            )
            boundary = root / "p_rgh"
            boundary.write_text(
                "boundaryField\n{\n fan_b\n {\n type fanPressure; direction in;\n"
                " fanCurve { type table; values ((0 20) (2 0) (4 0)); }\n }\n}\n",
                encoding="utf-8",
            )
            self.assertEqual(read_exported_curves(internal, "internal")["fan_a"].points[-1], (2.0, 0.0))
            parsed = read_exported_curves(boundary, "boundary")["fan_b"]
            self.assertEqual(parsed.direction, "in")
            self.assertEqual(positive_pressure_limit(parsed.points), 2.0)

    @staticmethod
    def write_internal_flow(case: Path, rank: int, value: float):
        directory = case / f"processor{rank}" / "1.0" / "fluid" / "uniform"
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "fan_aProperties").write_text(
            f"flow_rate {value};\n", encoding="utf-8"
        )

    @staticmethod
    def write_boundary_flow(case: Path, value: float):
        directory = case / "postProcessing" / "fluid" / "fan_b_mass_flow" / "1.0"
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "surfaceFieldValue.dat").write_text(
            f"# header\n1.0 {value}\n", encoding="utf-8"
        )

    def test_case_integration_converts_mass_flow_and_checks_direction(self):
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary)
            constant = case / "constant" / "fluid"
            initial = case / "0" / "fluid"
            constant.mkdir(parents=True)
            initial.mkdir(parents=True)
            (constant / "fvOptions").write_text(
                "fan_a\n{ type fanMomentumSource; fanCurve { values ((0 10) (1 0) (2 0)); } }\n",
                encoding="utf-8",
            )
            (initial / "p_rgh").write_text(
                "boundaryField\n{\nfan_b\n{ type fanPressure; direction in; "
                "fanCurve { values ((0 10) (1 0) (2 0)); } }\n}\n",
                encoding="utf-8",
            )
            for rank in range(2):
                self.write_internal_flow(case, rank, 1.1)
            self.write_boundary_flow(case, -0.95)

            rows = audit_case(case, density_kg_m3=1.0, warning_fraction=0.9)
            self.assertEqual([row.status for row in rows], [
                "FAIL_OUTSIDE_CURVE", "WARN_NEAR_LIMIT"
            ])
            self.assertAlmostEqual(rows[1].flow_m3_s, 0.95)

            self.write_boundary_flow(case, 0.95)
            rows = audit_case(case, density_kg_m3=1.0, warning_fraction=0.9)
            self.assertEqual(rows[1].status, "FAIL_DIRECTION")

    def test_stale_boundary_result_cannot_be_mixed_with_internal_checkpoint(self):
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary)
            constant = case / "constant" / "fluid"
            initial = case / "0" / "fluid"
            constant.mkdir(parents=True)
            initial.mkdir(parents=True)
            (constant / "fvOptions").write_text(
                "fan_a\n{ type fanMomentumSource; fanCurve { values ((0 10) (1 0)); } }\n",
                encoding="utf-8",
            )
            (initial / "p_rgh").write_text(
                "boundaryField\n{\nfan_b\n{ type fanPressure; direction in; "
                "fanCurve { values ((0 10) (1 0)); } }\n}\n",
                encoding="utf-8",
            )
            for rank in range(2):
                self.write_internal_flow(case, rank, 0.5)
            directory = (
                case / "postProcessing" / "fluid" / "fan_b_mass_flow" / "1.0"
            )
            directory.mkdir(parents=True)
            (directory / "surfaceFieldValue.dat").write_text(
                "1.0 -0.5\n1.1 -0.6\n", encoding="utf-8"
            )

            rows = audit_case(case, density_kg_m3=1.0)
            self.assertEqual(rows[1].time_s, 1.0)
            self.assertEqual(rows[1].status, "FAIL_MISSING")

    def test_cli_returns_failure_and_writes_both_reports(self):
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary)
            constant = case / "constant" / "fluid"
            initial = case / "0" / "fluid"
            constant.mkdir(parents=True)
            initial.mkdir(parents=True)
            (constant / "fvOptions").write_text(
                "fan_a\n{ type fanMomentumSource; fanCurve { values ((0 10) (1 0)); } }\n",
                encoding="utf-8",
            )
            (initial / "p_rgh").write_text(
                "boundaryField\n{\nfan_b\n{ type fanPressure; direction in; "
                "fanCurve { values ((0 10) (1 0)); } }\n}\n",
                encoding="utf-8",
            )
            for rank in range(2):
                self.write_internal_flow(case, rank, 1.1)
            self.write_boundary_flow(case, -0.5)
            csv_path = case / "result.csv"
            markdown_path = case / "result.md"

            with redirect_stdout(io.StringIO()):
                status = main([
                    str(case), "--density", "1.0",
                    "--csv", str(csv_path),
                    "--markdown", str(markdown_path),
                ])
            self.assertEqual(status, 1)
            self.assertTrue(csv_path.is_file())
            self.assertIn("FAIL_OUTSIDE_CURVE", csv_path.read_text())
            self.assertIn("FAIL_OUTSIDE_CURVE", markdown_path.read_text())


if __name__ == "__main__":
    unittest.main()
