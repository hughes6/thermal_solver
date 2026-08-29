import unittest

try:
    import numpy as np
except ModuleNotFoundError:
    np = None

from tools.openfoam_temperature_comparison import temperature_summary_rows


@unittest.skipUnless(np is not None, "optional NumPy dependency unavailable")
class OpenFoamTemperatureComparisonTest(unittest.TestCase):
    def test_volume_weighted_component_and_aggregate_statistics(self):
        reference = {
            "solid/internalMesh": {
                "center": np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0]]),
                "volume": np.array([1.0, 3.0]),
                "T": np.array([300.0, 310.0]),
            },
            "fluid/internalMesh": {
                "center": np.array([[2.0, 0.0, 0.0]]),
                "volume": np.array([2.0]),
                "T": np.array([290.0]),
            },
        }
        sample = {
            region: {
                "center": values["center"].copy(),
                "volume": values["volume"].copy(),
                "T": values["T"].copy(),
            }
            for region, values in reference.items()
        }
        sample["solid/internalMesh"]["T"] = np.array([302.0, 306.0])

        rows = temperature_summary_rows(reference, sample)
        solid = next(row for row in rows if row["region"] == "solid/internalMesh")
        aggregate = next(row for row in rows if row["region"] == "all")

        self.assertAlmostEqual(solid["reference_mean_K"], 307.5)
        self.assertAlmostEqual(solid["sample_mean_K"], 305.0)
        self.assertAlmostEqual(solid["mean_delta_K"], -2.5)
        self.assertAlmostEqual(solid["cellwise_rms_K"], np.sqrt(13.0))
        self.assertAlmostEqual(solid["cellwise_max_abs_K"], 4.0)
        self.assertEqual(aggregate["cells"], 3)
        self.assertAlmostEqual(aggregate["volume_m3"], 6.0)


if __name__ == "__main__":
    unittest.main()
