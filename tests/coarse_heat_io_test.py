import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "plot"))

from coarse_heat_io import read_spacing, temperature_limits, validate_columns


class CoarseHeatIoTest(unittest.TestCase):
    def test_spacing_and_temperature_range(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "coarse.csv"
            path.write_text(
                "step,time,x,y,z,T,is_component,vx,vy,vz\n"
                "dx,0.05,dy,0.10,dz,0.20\n"
                "0,0,0,0,0,20,0,0,0,0\n",
                encoding="utf-8",
            )
            self.assertEqual(read_spacing(str(path)), (0.05, 0.10, 0.20))

        self.assertEqual(temperature_limits([20.0, 21.5]), (20.0, 21.5))
        self.assertEqual(temperature_limits([20.0, 20.0]), (20.0, 21.0))
        self.assertEqual(
            temperature_limits([20.0, 21.5], ambient=19.0),
            (19.0, 21.5),
        )

    def test_rejects_bad_spacing(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.csv"
            path.write_text(
                "step,time,x,y,z,T,is_component,vx,vy,vz\n"
                "dx,0.05,dy,0,dz,0.20\n",
                encoding="utf-8",
            )
            with self.assertRaises(ValueError):
                read_spacing(str(path))

    def test_required_columns(self):
        validate_columns(
            ["step", "time", "x", "y", "z", "T", "is_component",
             "vx", "vy", "vz"]
        )
        with self.assertRaises(ValueError):
            validate_columns(["step", "time", "T"])

    def test_rejects_nonfinite_temperature_data(self):
        with self.assertRaises(ValueError):
            temperature_limits([float("nan"), float("inf")])


if __name__ == "__main__":
    unittest.main()
