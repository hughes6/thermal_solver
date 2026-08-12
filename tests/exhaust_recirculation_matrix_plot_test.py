import tempfile
import unittest
from pathlib import Path

from plot.exhaust_recirculation_matrix import difference, read_matrix, short_name


CSV = """source_exhaust,source_component,target_intake,target_component,mass_weighted_tracer_fraction,percent,target_incoming_mass_flow_kg_s
e0,Generic A,i0,Generic A,0.1,10,0.1
e0,Generic A,i1,Generic B,0.2,20,0.2
e1,Generic B,i0,Generic A,0.3,30,0.1
e1,Generic B,i1,Generic B,0.4,40,0.2
"""


class MatrixPlotTest(unittest.TestCase):
    def test_reads_percent_matrix_and_preserves_order(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "matrix.csv"
            path.write_text(CSV, encoding="utf-8")
            matrix = read_matrix(path)
            self.assertEqual(matrix.sources, ("Generic A", "Generic B"))
            self.assertEqual(matrix.targets, ("Generic A", "Generic B"))
            self.assertEqual(matrix.values, ((10.0, 20.0), (30.0, 40.0)))

    def test_difference_is_second_minus_first(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "matrix.csv"
            path.write_text(CSV, encoding="utf-8")
            matrix = read_matrix(path)
            self.assertEqual(difference(matrix, matrix).values,
                             ((0.0, 0.0), (0.0, 0.0)))

    def test_shortens_generic_prefix(self):
        self.assertEqual(short_name("Generic Dell R470 1U"), "Dell R470 1U")


if __name__ == "__main__":
    unittest.main()
