import tempfile
import unittest
from pathlib import Path

from tools.openfoam_heat_source_audit import analyze_case, power_summary


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
            self.assertEqual(power_summary(rows), {
                "total_sources": 2,
                "fluid_sources": 2,
                "solid_sources": 0,
                "total_power_w": 30.0,
                "fluid_power_w": 30.0,
                "solid_power_w": 0,
            })

    def test_rejects_active_watt_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "metadata is 20 W"):
                analyze_case(make_case(Path(directory), second_watts=19))

    def test_rejects_overlapping_heat_zones(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "overlap"):
                analyze_case(make_case(Path(directory), second_cells="2\n4"))

    def test_prepared_solid_sources_use_component_regions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = make_case(Path(directory))
            properties = root / "constant/openfoamExportProperties"
            properties.write_text(
                properties.read_text().replace(
                    "solverRegion fluid", "solverRegion solid"))
            for index, (name, watts, cells) in enumerate((
                    ("heater_0", 10, "1\n2"),
                    ("heater_1", 20, "3\n4"))):
                region = root / f"constant/part_{index}"
                (region / "polyMesh/sets").mkdir(parents=True)
                (region / "fvOptions").write_text(f"""
{name}_energy {{ type scalarSemiImplicitSource; active true;
 selectionMode cellZone; cellZone {name}; volumeMode absolute;
 sources {{ h ({watts} 0); }} }}
""")
                (region / f"polyMesh/sets/{name}").write_text(
                    "FoamFile { format ascii; }\n2\n(\n" + cells + "\n)\n")
            (root / "constant/fluid/fvOptions").unlink()

            rows = analyze_case(root)

            self.assertEqual(
                [row.solver_region for row in rows], ["part_0", "part_1"])
            self.assertEqual([row.cells for row in rows], [2, 2])
            self.assertEqual(power_summary(rows)["solid_power_w"], 30.0)
            self.assertEqual(power_summary(rows)["fluid_power_w"], 0)

    def test_unprepared_solid_sources_use_aggregate_cell_sets(self):
        with tempfile.TemporaryDirectory() as directory:
            root = make_case(Path(directory))
            properties = root / "constant/openfoamExportProperties"
            properties.write_text(
                properties.read_text().replace(
                    "solverRegion fluid", "solverRegion solid"))
            aggregate_sets = root / "constant/polyMesh/sets"
            aggregate_sets.mkdir(parents=True)
            for index, (name, watts) in enumerate((
                    ("heater_0", 10), ("heater_1", 20))):
                region = root / f"constant/part_{index}"
                region.mkdir(parents=True)
                (region / "fvOptions").write_text(f"""
{name}_energy {{ type scalarSemiImplicitSource; active true;
 selectionMode cellZone; cellZone {name}; volumeMode absolute;
 sources {{ h ({watts} 0); }} }}
""")
                source_set = root / f"constant/fluid/polyMesh/sets/{name}"
                source_set.replace(aggregate_sets / name)
            (root / "constant/fluid/fvOptions").unlink()

            rows = analyze_case(root)

            self.assertEqual(
                [row.solver_region for row in rows], ["part_0", "part_1"])
            self.assertEqual([row.cells for row in rows], [2, 2])


if __name__ == "__main__":
    unittest.main()
