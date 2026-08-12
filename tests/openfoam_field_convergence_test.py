import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import numpy as np

from tools.openfoam_field_convergence import (
    component_air_partitions,
    compare_snapshots,
    directory_times,
    exact_time,
    field_error,
    select_case_type,
)


class OpenFoamFieldConvergenceTest(unittest.TestCase):
    def test_component_air_partitions_leave_external_cells(self):
        centers = np.array([
            [0.25, 0.25, 0.25],
            [1.25, 0.25, 0.25],
            [2.25, 0.25, 0.25],
        ])
        partitions = component_air_partitions(
            centers,
            [
                ("server_a", (0.0, 0.0, 0.0), (1.0, 1.0, 1.0)),
                ("server_b", (1.0, 0.0, 0.0), (1.0, 1.0, 1.0)),
            ],
        )
        self.assertEqual([name for name, _ in partitions], [
            "fluid/componentAir:server_a",
            "fluid/componentAir:server_b",
            "fluid/externalRackAir",
        ])
        np.testing.assert_array_equal(partitions[0][1], [True, False, False])
        np.testing.assert_array_equal(partitions[1][1], [False, True, False])
        np.testing.assert_array_equal(partitions[2][1], [False, False, True])

    def test_scalar_volume_weighted_error(self):
        metrics = field_error(
            np.array([10.0, 20.0]),
            np.array([12.0, 16.0]),
            np.array([1.0, 3.0]),
            np.array([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]),
        )
        self.assertAlmostEqual(metrics["mean_absolute"], 3.5)
        self.assertAlmostEqual(metrics["rms"], np.sqrt(13.0))
        self.assertEqual(metrics["maximum"], 4.0)
        self.assertEqual(
            (metrics["maximum_x"], metrics["maximum_y"], metrics["maximum_z"]),
            (4.0, 5.0, 6.0),
        )

    def test_pressure_error_removes_only_uniform_gauge_offset(self):
        reference = {
            "fluid/internalMesh": {
                "volume": np.array([1.0, 1.0, 2.0]),
                "center": np.array([
                    [0.0, 0.0, 0.0],
                    [1.0, 0.0, 0.0],
                    [2.0, 0.0, 0.0],
                ]),
                "p_rgh": np.array([100000.0, 100010.0, 100020.0]),
            },
        }
        gauge_shifted = {
            "fluid/internalMesh": {
                **reference["fluid/internalMesh"],
                "p_rgh": np.array([100500.0, 100510.0, 100520.0]),
            },
        }
        shifted_row = compare_snapshots(
            reference, gauge_shifted, ("p_rgh",)
        )[0]
        self.assertAlmostEqual(shifted_row["rms"], 0.0)
        self.assertAlmostEqual(shifted_row["maximum"], 0.0)

        pattern_changed = {
            "fluid/internalMesh": {
                **reference["fluid/internalMesh"],
                "p_rgh": np.array([100500.0, 100512.0, 100519.0]),
            },
        }
        changed_row = compare_snapshots(
            reference, pattern_changed, ("p_rgh",)
        )[0]
        self.assertGreater(changed_row["rms"], 0.0)
        self.assertGreater(changed_row["relative_rms"], 0.0)

    def test_vector_norm_and_aggregate(self):
        reference = {
            "fluid/internalMesh": {
                "volume": np.array([1.0]),
                "center": np.array([[0.1, 0.2, 0.3]]),
                "U": np.array([[3.0, 4.0, 0.0]]),
            },
            "solid/internalMesh": {
                "volume": np.array([1.0]),
                "center": np.array([[0.4, 0.5, 0.6]]),
            },
        }
        sample = {
            "fluid/internalMesh": {
                "volume": np.array([1.0]),
                "center": np.array([[0.1, 0.2, 0.3]]),
                "U": np.array([[0.0, 0.0, 0.0]]),
            },
            "solid/internalMesh": {
                "volume": np.array([1.0]),
                "center": np.array([[0.4, 0.5, 0.6]]),
            },
        }
        rows = compare_snapshots(reference, sample, ("U",))
        aggregate = next(row for row in rows if row["region"] == "all")
        self.assertAlmostEqual(aggregate["rms"], 5.0)
        self.assertAlmostEqual(aggregate["relative_rms"], 1.0)
        self.assertEqual(
            (aggregate["maximum_x"], aggregate["maximum_y"],
             aggregate["maximum_z"]),
            (0.1, 0.2, 0.3),
        )

    def test_rejects_mismatched_cell_centers(self):
        with self.assertRaisesRegex(ValueError, "Cell centers"):
            field_error(
                np.array([1.0]), np.array([2.0]), np.array([1.0]),
                np.array([[0.0, 0.0]]),
            )

    def test_exact_time_rejects_unwritten_value(self):
        self.assertEqual(exact_time([0.0, 1.0], 1.0), 1.0)
        with self.assertRaisesRegex(ValueError, "nearest written time"):
            exact_time([0.0, 1.0], 0.5)

    def test_auto_prefers_complete_reconstructed_history(self):
        with TemporaryDirectory() as directory:
            case = Path(directory)
            (case / "20400.16").mkdir()
            (case / "21600.049999999999").mkdir()
            (case / "processor0").mkdir()
            (case / "processor1").mkdir()
            self.assertEqual(
                directory_times(case), [20400.16, 21600.049999999999]
            )
            self.assertEqual(
                select_case_type(case, [20400.16, 21600.05]),
                "reconstructed",
            )

    def test_auto_uses_processors_when_root_history_is_incomplete(self):
        with TemporaryDirectory() as directory:
            case = Path(directory)
            (case / "21600.05").mkdir()
            (case / "processor0").mkdir()
            self.assertEqual(
                select_case_type(case, [20400.16, 21600.05]), "decomposed"
            )

    def test_explicit_case_type_overrides_auto_selection(self):
        with TemporaryDirectory() as directory:
            case = Path(directory)
            (case / "1").mkdir()
            self.assertEqual(
                select_case_type(case, [1.0], "decomposed"), "decomposed"
            )


if __name__ == "__main__":
    unittest.main()
