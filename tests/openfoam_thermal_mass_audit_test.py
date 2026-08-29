import tempfile
import unittest
from pathlib import Path

from tools.openfoam_thermal_mass_audit import (
    audit_case, heat_by_region, markdown_report, mesh_volumes
)


class OpenFoamThermalMassAuditTest(unittest.TestCase):
    def test_parses_checkmesh_sentence_punctuation_and_sums_heat(self):
        self.assertEqual(
            mesh_volumes("Mesh stats solid_a\n Total volume = 0.125.  Cell volumes OK.\n"),
            {"solid_a": 0.125},
        )
        properties = """
        heatSources
        (
        { componentRegion solid_a; watts 10; }
        { componentRegion solid_a; watts 15; }
        );
        """
        self.assertEqual(heat_by_region(properties), {"solid_a": 25.0})

    def test_audit_calculates_mass_capacity_and_adiabatic_rate(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            (case / "constant" / "solid_a").mkdir(parents=True)
            (case / "checkMesh.prepare.log").write_text(
                "Mesh stats solid_a\n Total volume = 0.5.  Cell volumes OK.\n"
            )
            (case / "constant" / "openfoamExportProperties").write_text(
                "heatSources\n(\n{ componentRegion solid_a; watts 100; }\n);\n"
            )
            (case / "constant" / "solid_a" / "thermophysicalProperties").write_text(
                "thermodynamics { Cp 500; }\n equationOfState { rho 2000; }\n"
            )
            rows = audit_case(case)

        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["mass_kg"], 1000.0)
        self.assertEqual(rows[0]["capacity_j_k"], 500000.0)
        self.assertAlmostEqual(rows[0]["adiabatic_k_per_hour"], 0.72)

    def test_markdown_report_contains_units_region_and_totals(self):
        rows = [{
            "region": "solid_a", "volume_m3": 0.5, "mass_kg": 1000.0,
            "capacity_j_k": 500000.0, "watts": 100.0,
            "adiabatic_k_per_hour": 0.72,
        }]
        report = markdown_report(rows)
        self.assertIn("# OpenFOAM thermal-mass audit", report)
        self.assertIn("| solid_a | 0.5 | 1000", report)
        self.assertIn("| **TOTAL** | **0.5** | **1000**", report)


if __name__ == "__main__":
    unittest.main()
