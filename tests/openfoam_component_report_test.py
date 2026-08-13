import tempfile
import unittest
from pathlib import Path

from tools.openfoam_component_report import (
    heat_sources,
    parse_volume_average,
    reconstructed_time,
    solid_regions,
)


class ComponentReportTest(unittest.TestCase):
    def test_parses_regions_and_heat_allocation(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            constant = case / "constant"
            constant.mkdir()
            (constant / "regionProperties").write_text(
                "regions { fluid (fluid); solid (Solid_A Solid_B); }\n")
            (constant / "openfoamExportProperties").write_text("""
heatSources
(
{
 name source_A;
 componentRegion Solid_A;
 solverRegion fluid;
 watts 50;
 selectedVolume 0.025;
 volumetricPower 2000;
}
);
""")
            self.assertEqual(solid_regions(case), ["Solid_A", "Solid_B"])
            source = heat_sources(case)[0]
            self.assertEqual(source.component_region, "Solid_A")
            self.assertEqual(source.watts, 50.0)
            self.assertEqual(source.selected_volume_m3, 0.025)

    def test_parses_openfoam_volume_average(self):
        self.assertEqual(parse_volume_average(
            "volAverage(Generic_Dell) of T = 305.068\n"), 305.068)
        with self.assertRaisesRegex(ValueError, "did not report"):
            parse_volume_average("no result")

    def test_selects_only_complete_reconstructed_time(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            for time, complete in (("1", True), ("2", False)):
                for region in (["A", "B"] if complete else ["A"]):
                    path = case / time / region
                    path.mkdir(parents=True)
                    (path / "T").write_text("field")
            value, path = reconstructed_time(case, ["A", "B"], None)
            self.assertEqual(value, 1.0)
            self.assertEqual(path.name, "1")


if __name__ == "__main__":
    unittest.main()
