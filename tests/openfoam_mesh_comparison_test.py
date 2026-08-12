import unittest


class OpenFoamMeshComparisonTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            import numpy as np
            import pyvista as pv
        except ImportError as exc:
            raise unittest.SkipTest(str(exc))
        cls.np = np
        cls.pv = pv

    def grid(self):
        grid = self.pv.ImageData(dimensions=(3, 2, 2), spacing=(0.5, 1.0, 1.0))
        grid.cell_data["T"] = self.np.array([300.0, 302.0])
        grid.cell_data["U"] = self.np.array([[1.0, 0.0, 0.0], [3.0, 0.0, 0.0]])
        return grid

    def test_mesh_summary_is_volume_weighted(self):
        from tools.openfoam_mesh_comparison import mesh_summary

        stats = mesh_summary(self.grid(), "T")
        self.assertEqual(stats["cells"], 2)
        self.assertAlmostEqual(stats["volume"], 1.0)
        self.assertAlmostEqual(stats["mean"], 301.0)
        self.assertAlmostEqual(stats["maximum"], 302.0)

    def test_identical_mesh_sampling_has_zero_error(self):
        from tools.openfoam_mesh_comparison import sampled_error

        grid = self.grid()
        coverage, error = sampled_error(grid, grid.copy(deep=True), "T")
        self.assertEqual(coverage, 1.0)
        self.assertAlmostEqual(error["rms"], 0.0)
        self.assertAlmostEqual(error["maximum"], 0.0)
        self.assertTrue(self.np.isfinite(error["maximum_x"]))
        self.assertTrue(self.np.isfinite(error["maximum_y"]))
        self.assertTrue(self.np.isfinite(error["maximum_z"]))


if __name__ == "__main__":
    unittest.main()
