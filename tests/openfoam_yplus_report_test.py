import tempfile
import unittest
from pathlib import Path

from tools.openfoam_yplus_report import analyze_case


class YPlusReportTest(unittest.TestCase):
    def test_selects_nonzero_live_flow_report(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            root = case / "postProcessing/fluid/fluid_y_plus/100"
            root.mkdir(parents=True)
            header = "# Time patch min max average\n"
            (root / "yPlus.dat").write_text(
                header + "100 rack_walls 0 0 0\n")
            (root / "yPlus_100.dat").write_text(
                header + "100 rack_walls 2 80 20\n")
            rows = analyze_case(case)
            self.assertEqual(rows[0].source_file, "yPlus_100.dat")
            self.assertEqual(rows[0].regime, "mixed/buffer-layer")

    def test_rejects_only_zero_reports(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            root = case / "postProcessing/fluid/fluid_y_plus/100"
            root.mkdir(parents=True)
            (root / "yPlus.dat").write_text("100 wall 0 0 0\n")
            with self.assertRaisesRegex(ValueError, "only zeros"):
                analyze_case(case)


if __name__ == "__main__":
    unittest.main()
