import tempfile
import unittest
from pathlib import Path

from plot.outlet_mass_weighted_temperature import (
    direct_patch_temperature,
    read_surface_report,
    select_report_time,
)
from plot_outlet_flow import read_samples


class SurfaceReportTest(unittest.TestCase):
    def test_direct_boundary_fields_avoid_vtk(self):
        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary)
            fluid = case / "12.5" / "fluid"
            fluid.mkdir(parents=True)
            (fluid / "phi").write_text(
                "FoamFile { format ascii; }\n"
                "boundaryField { outlet { type calculated; "
                "value nonuniform List<scalar> 2 (0.1 0.2); } }\n"
            )
            (fluid / "T").write_text(
                "FoamFile { format ascii; }\n"
                "boundaryField { outlet { type calculated; "
                "value uniform 300; } }\n"
            )
            time, flow, temperature, faces = direct_patch_temperature(
                case, "outlet", "latest"
            )
            self.assertEqual(time, 12.5)
            self.assertAlmostEqual(flow, 0.3)
            self.assertAlmostEqual(temperature, 300.0)
            self.assertEqual(faces, 2)

    def test_selects_latest_report_without_vtk(self):
        samples = {12000.0: 298.0, 16800.01: 299.0}
        self.assertEqual(select_report_time(samples, "latest"), 16800.01)
        self.assertEqual(select_report_time(samples, "16800.01"), 16800.01)
        self.assertIsNone(select_report_time(samples, "15000"))

    def test_reads_openfoam_collision_safe_suffix(self):
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary)
            time_dir = report / "16800.01"
            time_dir.mkdir()
            (time_dir / "surfaceFieldValue.dat").write_text(
                "# Time weightedAverage(T)\n16800.01\n",
                encoding="utf-8",
            )
            (time_dir / "surfaceFieldValue_16800.01.dat").write_text(
                "# Time weightedAverage(T)\n16800.01 298.6494\n",
                encoding="utf-8",
            )

            self.assertEqual(read_surface_report(report), {16800.01: 298.6494})

    def test_flow_plot_reader_accepts_openfoam_suffix(self):
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary)
            time_dir = report / "16800.01"
            time_dir.mkdir()
            (time_dir / "surfaceFieldValue.dat").write_text(
                "# Time sum(phi)\n16800.01\n",
                encoding="utf-8",
            )
            (time_dir / "surfaceFieldValue_16800.01.dat").write_text(
                "# Time sum(phi)\n16800.01 0.29655068\n",
                encoding="utf-8",
            )

            self.assertEqual(read_samples(report), ([16800.01], [0.29655068]))


if __name__ == "__main__":
    unittest.main()
