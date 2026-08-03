import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "plot"))

import heat_animation
from heat_animation import (
    enable_internal_meshes_only,
    build_argument_parser,
    run_openfoam_animation,
    run_openfoam_convergence_report,
    select_openfoam_animation_times,
)


class OpenFoamAnimationTest(unittest.TestCase):
    def test_reader_enables_only_internal_volume_meshes(self):
        class Reader:
            patch_array_names = [
                "internalMesh", "patch/rack_walls", "patch/empty_interface"]

            def __init__(self):
                self.enabled = []
                self.disabled_all = False

            def disable_all_patch_arrays(self):
                self.disabled_all = True

            def enable_patch_array(self, name):
                self.enabled.append(name)

        reader = Reader()
        enable_internal_meshes_only(reader)
        self.assertTrue(reader.disabled_all)
        self.assertEqual(reader.enabled, ["internalMesh"])

    def test_inclusive_time_range_and_skip(self):
        self.assertEqual(
            select_openfoam_animation_times(
                [900, 0, 600, 300, 300], start_time=300,
                end_time=900, skip=2
            ),
            [300.0, 900.0],
        )

    def test_rejects_empty_range_and_invalid_skip(self):
        with self.assertRaises(ValueError):
            select_openfoam_animation_times([0, 300], 600, 900, 1)
        with self.assertRaises(ValueError):
            select_openfoam_animation_times([0, 300], skip=0)

    def test_animation_arguments(self):
        args = build_argument_parser().parse_args([
            "--format", "openfoam", "--case", "case", "--animate",
            "--start-time", "300", "--end-time", "18000", "--skip", "3",
            "--slice-axis", "none", "--output", "rack.gif",
        ])
        self.assertTrue(args.animate)
        self.assertEqual(args.start_time, 300.0)
        self.assertEqual(args.end_time, 18000.0)
        self.assertEqual(args.skip, 3)
        self.assertEqual(args.slice_axis, "none")
        self.assertEqual(args.output, "rack.gif")
        report_args = build_argument_parser().parse_args([
            "--format", "openfoam", "--case", "case",
            "--convergence-report", "--output", "report.png",
        ])
        self.assertTrue(report_args.convergence_report)

    def test_pyvista_gif_writer_with_3d_temperature_field(self):
        try:
            import imageio
            import numpy as np
            import pyvista as pv
        except ImportError:
            self.skipTest("optional PyVista/imageio animation dependency is unavailable")

        class FakeReader:
            time_values = [0.0, 1.0]

            def set_active_time_value(self, value):
                self.value = value

            def read(self):
                grid = pv.ImageData(dimensions=(3, 3, 3), spacing=(0.1, 0.1, 0.1))
                grid.point_data["T"] = np.full(grid.n_points, 293.15 + self.value)
                return grid

        heat_animation.np = np
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "rack.gif"
            args = SimpleNamespace(
                start_time=None, end_time=None, skip=1,
                temperature_units="C", output=str(output), fps=2,
                slice_axis="none", slice_position=None, opacity=0.35,
            )
            run_openfoam_animation(args, pv, FakeReader(), rack=None)
            self.assertTrue(output.is_file())
            self.assertGreater(output.stat().st_size, 0)

    def test_convergence_png_and_csv_with_multiregion_field(self):
        try:
            import matplotlib
            import numpy as np
            import pyvista as pv
        except ImportError:
            self.skipTest("optional convergence plotting dependencies are unavailable")
        matplotlib.use("Agg")

        class FakeReader:
            time_values = [0.0, 10.0, 20.0]

            def set_active_time_value(self, value):
                self.value = value

            def read(self):
                def region(temperature):
                    grid = pv.ImageData(
                        dimensions=(3, 3, 3), spacing=(0.1, 0.1, 0.1)
                    )
                    grid.cell_data["T"] = np.full(grid.n_cells, temperature)
                    return pv.MultiBlock({"internalMesh": grid})

                return pv.MultiBlock({
                    "fluid": region(293.15 + 0.1 * self.value),
                    "server_0": region(303.15 + 0.2 * self.value),
                })

        heat_animation.np = np
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "convergence.png"
            args = SimpleNamespace(
                start_time=None, end_time=None, skip=1,
                temperature_units="C", output=str(output),
            )
            run_openfoam_convergence_report(args, FakeReader())
            csv_output = output.with_suffix(".csv")
            self.assertTrue(output.is_file())
            self.assertGreater(output.stat().st_size, 0)
            self.assertIn("fluid_mean", csv_output.read_text(encoding="utf-8"))
            self.assertIn("server_0_maximum", csv_output.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
