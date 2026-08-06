import unittest

import numpy as np

from tools.openfoam_field_convergence import (
    compare_snapshots,
    exact_time,
    field_error,
)


class OpenFoamFieldConvergenceTest(unittest.TestCase):
    def test_scalar_volume_weighted_error(self):
        metrics = field_error(
            np.array([10.0, 20.0]),
            np.array([12.0, 16.0]),
            np.array([1.0, 3.0]),
        )
        self.assertAlmostEqual(metrics["mean_absolute"], 3.5)
        self.assertAlmostEqual(metrics["rms"], np.sqrt(13.0))
        self.assertEqual(metrics["maximum"], 4.0)

    def test_vector_norm_and_aggregate(self):
        reference = {
            "fluid/internalMesh": {
                "volume": np.array([1.0]),
                "U": np.array([[3.0, 4.0, 0.0]]),
            },
            "solid/internalMesh": {
                "volume": np.array([1.0]),
            },
        }
        sample = {
            "fluid/internalMesh": {
                "volume": np.array([1.0]),
                "U": np.array([[0.0, 0.0, 0.0]]),
            },
            "solid/internalMesh": {
                "volume": np.array([1.0]),
            },
        }
        rows = compare_snapshots(reference, sample, ("U",))
        aggregate = next(row for row in rows if row["region"] == "all")
        self.assertAlmostEqual(aggregate["rms"], 5.0)
        self.assertAlmostEqual(aggregate["relative_rms"], 1.0)

    def test_exact_time_rejects_unwritten_value(self):
        self.assertEqual(exact_time([0.0, 1.0], 1.0), 1.0)
        with self.assertRaisesRegex(ValueError, "nearest written time"):
            exact_time([0.0, 1.0], 0.5)


if __name__ == "__main__":
    unittest.main()
