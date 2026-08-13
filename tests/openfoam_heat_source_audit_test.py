import tempfile
import unittest
from pathlib import Path

from tools.openfoam_heat_source_audit import analyze_case


def make_case(root: Path, second_cells: str = "3\n4", second_watts: float = 20) -> Path:
    (root / "constant/fluid/polyMesh/sets").mkdir(parents=True)
    (root / "constant/openfoamExportProperties").write_text("""heatSources
(
{ name heater_0; componentRegion part_0; solverRegion fluid; watts 10;
  selectedVolume 0.01; volumetricPower 1000; }
{ name heater_1; componentRegion part_1; solverRegion fluid; watts 20;
  selectedVolume 0.02; volumetricPower 1000; }
);\n""")
    (root / "constant/fluid/fvOptions").write_text(f"""
heater_0_energy {{ type scalarSemiImplicitSource; active true;
 selectionMode cellZone; cellZone heater_0; volumeMode absolute;
 sources {{ h (10 0); }} }}
heater_1_energy {{ type scalarSemiImplicitSource; active true;
 selectionMode cellZone; cellZone heater_1; volumeMode absolute;
 sources {{ h ({second_watts} 0); }} }}
""")
    header = ("FoamFile {{ format ascii; }}\n{count}\n(\n{cells}\n)\n\n"
              "// ************************************************************************* //\n")
    (root / "constant/fluid/polyMesh/sets/heater_0").write_text(
        header.format(count=2, cells="1\n2"))
    (root / "constant/fluid/polyMesh/sets/heater_1").write_text(
        header.format(count=2, cells=second_cells))
    return root


class HeatSourceAuditTest(unittest.TestCase):
    def test_valid_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            rows = analyze_case(make_case(Path(directory)))
            self.assertEqual([row.cells for row in rows], [2, 2])
            self.assertTrue(all(row.status == "PASS" for row in rows))

    def test_rejects_active_watt_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "metadata is 20 W"):
                analyze_case(make_case(Path(directory), second_watts=19))

    def test_rejects_overlapping_heat_zones(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "overlap"):
                analyze_case(make_case(Path(directory), second_cells="2\n4"))


if __name__ == "__main__":
    unittest.main()
