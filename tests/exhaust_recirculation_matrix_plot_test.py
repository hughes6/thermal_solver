import tempfile
import unittest
from pathlib import Path

from plot.exhaust_recirculation_matrix import (
    aggregate,
    difference,
    read_matrix,
    short_name,
    write_aggregate_csv,
)


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

    def test_aggregates_aligned_snapshots_and_writes_statistics(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "matrix.csv"
            path.write_text(CSV, encoding="utf-8")
            first = read_matrix(path)
            second = type(first)(first.sources, first.targets,
                                 ((12.0, 18.0), (34.0, 38.0)))
            result = aggregate([first, second])
            self.assertEqual(result.sample_count, 2)
            self.assertEqual(result.mean.values,
                             ((11.0, 19.0), (32.0, 39.0)))
            self.assertEqual(result.maximum_deviation.values,
                             ((1.0, 1.0), (2.0, 1.0)))
            output = Path(directory) / "statistics.csv"
            write_aggregate_csv(result, output)
            text = output.read_text(encoding="utf-8")
            self.assertIn("maximum_deviation_pp", text)
            self.assertIn("Generic A,Generic A,11.0,10.0,12.0,1.0,2", text)

    def test_rejects_misaligned_snapshot_ordering(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "matrix.csv"
            path.write_text(CSV, encoding="utf-8")
            first = read_matrix(path)
            misaligned = type(first)(("Generic B", "Generic A"), first.targets,
                                     first.values)
            with self.assertRaisesRegex(ValueError, "different source/target"):
                aggregate([first, misaligned])


if __name__ == "__main__":
    unittest.main()
