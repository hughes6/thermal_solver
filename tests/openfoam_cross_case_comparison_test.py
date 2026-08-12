import unittest

import numpy as np

from tools.openfoam_cross_case_comparison import (
    CSV_HEADINGS,
    maximum_difference,
    validate_geometry,
)
from tools.openfoam_field_convergence import compare_snapshots


class OpenFoamCrossCaseComparisonTest(unittest.TestCase):
    def snapshots(self):
        reference = {
            "fluid/internalMesh": {
                "center": np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0]]),
                "volume": np.array([1.0, 2.0]),
                "U": np.array([[1.0, 0.0, 0.0], [0.0, 2.0, 0.0]]),
            }
        }
        sample = {
            "fluid/internalMesh": {
                "center": reference["fluid/internalMesh"]["center"].copy(),
                "volume": reference["fluid/internalMesh"]["volume"].copy(),
                "U": np.array([[1.0, 0.0, 0.0], [0.0, -3.0, 0.0]]),
            }
        }
        return reference, sample

    def test_geometry_and_vector_outlier(self):
        reference, sample = self.snapshots()
        validate_geometry(reference, sample)
        maximum = maximum_difference(reference, sample, "U")
        self.assertEqual(maximum["index"], 1)
        self.assertAlmostEqual(maximum["difference"], 5.0)
        np.testing.assert_array_equal(maximum["center"], [1.0, 0.0, 0.0])

    def test_geometry_rejects_reordered_cells(self):
        reference, sample = self.snapshots()
        sample["fluid/internalMesh"]["center"] = np.array(
            [[1.0, 0.0, 0.0], [0.0, 0.0, 0.0]]
        )
        with self.assertRaisesRegex(ValueError, "ordering or geometry"):
            validate_geometry(reference, sample)

    def test_csv_schema_covers_comparison_metrics(self):
        reference, sample = self.snapshots()
        row = {
            "sample_case": "sample",
            "sample_time": 1.0,
            "reference_case": "reference",
            "reference_time": 2.0,
            **compare_snapshots(reference, sample, ("U",))[0],
        }
        self.assertLessEqual(set(row), set(CSV_HEADINGS))


if __name__ == "__main__":
    unittest.main()
