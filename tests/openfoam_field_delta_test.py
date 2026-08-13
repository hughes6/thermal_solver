import struct
import tempfile
import unittest
from pathlib import Path

from tools.openfoam_field_delta import (
    compare_fields,
    latest_common_time_names,
    processor_field_paths,
    read_internal_field,
    resolve_time_directory,
    scalar_delta_distribution,
    top_scalar_delta_locations,
    vector_delta_distribution,
    verify_processor_topology_identity,
)


def write_field(path: Path, field_type: str, values: list[float]) -> None:
    width = 1 if field_type == "scalar" else 3
    path.parent.mkdir(parents=True, exist_ok=True)
    header = (
        'FoamFile\n{\n    format      binary;\n'
        '    arch        "LSB;label=32;scalar=64";\n}\n'
        f"internalField nonuniform List<{field_type}>\n{len(values)//width}\n("
    ).encode("ascii")
    path.write_bytes(header + struct.pack(f"<{len(values)}d", *values) + b")\n")


class OpenFoamFieldDeltaTest(unittest.TestCase):
    def test_resolves_openfoam_floating_point_directory_spelling(self):
        with tempfile.TemporaryDirectory() as directory:
            processor = Path(directory)
            written = processor / "1.237173877551027"
            written.mkdir()
            self.assertEqual(
                resolve_time_directory(processor, "1.2371738775510202"), written
            )

    def test_rejects_unavailable_nearest_time(self):
        with tempfile.TemporaryDirectory() as directory:
            processor = Path(directory)
            (processor / "1.0").mkdir()
            with self.assertRaisesRegex(FileNotFoundError, "nearest is 1.0"):
                resolve_time_directory(processor, "1.1")

    def test_selects_one_processor_rank(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            for rank in (0, 1):
                write_field(
                    case / f"processor{rank}" / "1" / "fluid" / "U",
                    "vector",
                    [1.0, 2.0, 3.0],
                )
            self.assertEqual(
                processor_field_paths(case, "1", "fluid", "U", rank=1),
                [case / "processor1" / "1" / "fluid" / "U"],
            )

    def test_cross_case_paths_can_feed_identical_layout_comparison(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before_case = root / "before_case"
            after_case = root / "after_case"
            write_field(
                before_case / "processor0" / "1" / "fluid" / "T",
                "scalar", [300.0, 301.0],
            )
            write_field(
                after_case / "processor0" / "2" / "fluid" / "T",
                "scalar", [300.5, 302.0],
            )
            result = compare_fields(
                processor_field_paths(before_case, "1", "fluid", "T"),
                processor_field_paths(after_case, "2", "fluid", "T"),
            )
            self.assertEqual(result[0], 2)
            self.assertEqual(result[2], 1.0)

    def test_cross_case_topology_requires_identical_cell_addressing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "first"
            second = root / "second"
            relative = Path("processor0/constant/fluid/polyMesh/cellProcAddressing")
            (first / relative).parent.mkdir(parents=True)
            (second / relative).parent.mkdir(parents=True)
            (first / relative).write_bytes(b"same-addressing")
            (second / relative).write_bytes(b"same-addressing")
            self.assertEqual(
                verify_processor_topology_identity(first, second, "fluid"), 1
            )
            (second / relative).write_bytes(b"reordered")
            with self.assertRaisesRegex(ValueError, "cell ordering differs"):
                verify_processor_topology_identity(first, second, "fluid")

    def test_cross_case_topology_rejects_partition_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "first"
            second = root / "second"
            for case, ranks in ((first, (0, 1)), (second, (0,))):
                for rank in ranks:
                    path = case / f"processor{rank}/constant/fluid/polyMesh/cellProcAddressing"
                    path.parent.mkdir(parents=True)
                    path.write_bytes(b"same")
            with self.assertRaisesRegex(ValueError, "processor partitions differ"):
                verify_processor_topology_identity(first, second, "fluid")

    def test_selects_latest_pair_common_to_all_ranks(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            for rank in (0, 1):
                for time in ("1.0", "2.000000000000001"):
                    (case / f"processor{rank}" / time).mkdir(parents=True)
            (case / "processor0" / "3.0").mkdir()
            self.assertEqual(
                latest_common_time_names(case), ("1.0", "2.000000000000001")
            )

    def test_latest_pair_requires_two_common_times(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            (case / "processor0" / "1").mkdir(parents=True)
            (case / "processor1" / "1").mkdir(parents=True)
            with self.assertRaisesRegex(ValueError, "fewer than two"):
                latest_common_time_names(case)

    def test_reads_binary_vector_field(self):
        with tempfile.TemporaryDirectory() as directory:
            field = Path(directory) / "U"
            write_field(field, "vector", [1.0, 2.0, 3.0, 4.0, 5.0, 6.0])
            self.assertEqual(
                read_internal_field(field),
                ("vector", [1.0, 2.0, 3.0, 4.0, 5.0, 6.0]),
            )

    def test_reads_zero_cell_partition_field(self):
        with tempfile.TemporaryDirectory() as directory:
            field = Path(directory) / "T"
            field.write_bytes(
                b'FoamFile\n{\nformat      binary;\n'
                b'arch        "LSB;label=32;scalar=64";\n}\n'
                b'internalField nonuniform List<scalar> 0;\n'
            )
            self.assertEqual(read_internal_field(field), ("scalar", []))

    def test_comparison_accepts_matching_zero_cell_rank(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before = root / "before"
            after = root / "after"
            zero_before = root / "zero_before"
            zero_after = root / "zero_after"
            write_field(before, "scalar", [1.0])
            write_field(after, "scalar", [2.0])
            for path in (zero_before, zero_after):
                path.write_bytes(
                    b'FoamFile\n{\nformat      binary;\n'
                    b'arch        "LSB;label=32;scalar=64";\n}\n'
                    b'internalField nonuniform List<scalar> 0;\n'
                )
            count, *_ = compare_fields(
                [before, zero_before], [after, zero_after]
            )
            self.assertEqual(count, 1)

    def test_compares_binary_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before = root / "before"
            after = root / "after"
            write_field(before, "scalar", [1.0, 2.0])
            write_field(after, "scalar", [2.0, 4.0])
            count, rms_delta, maximum, rms_field, relative = compare_fields(
                [before], [after]
            )
            self.assertEqual(count, 2)
            self.assertAlmostEqual(rms_delta, (2.5) ** 0.5)
            self.assertEqual(maximum, 2.0)
            self.assertAlmostEqual(rms_field, (10.0) ** 0.5)
            self.assertAlmostEqual(relative, 0.5)

    def test_reports_vector_delta_distribution_and_concentration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before = root / "before"
            after = root / "after"
            write_field(before, "vector", [0.0] * 12)
            write_field(
                after,
                "vector",
                [1.0, 0.0, 0.0, 2.0, 0.0, 0.0,
                 3.0, 0.0, 0.0, 4.0, 0.0, 0.0],
            )
            count, percentiles, concentration = vector_delta_distribution(
                [before], [after]
            )
            self.assertEqual(count, 4)
            self.assertEqual(percentiles["p50"], 2.5)
            self.assertEqual(percentiles["maximum"], 4.0)
            self.assertAlmostEqual(concentration["top_1pct"], 16.0 / 30.0)
            self.assertAlmostEqual(concentration["top_10pct"], 16.0 / 30.0)

    def test_vector_distribution_rejects_scalar_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before = root / "before"
            after = root / "after"
            write_field(before, "scalar", [1.0])
            write_field(after, "scalar", [2.0])
            with self.assertRaisesRegex(ValueError, "requires vector"):
                vector_delta_distribution([before], [after])

    def test_reports_scalar_delta_distribution(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before = root / "before"
            after = root / "after"
            write_field(before, "scalar", [0.0, 0.0, 0.0, 0.0])
            write_field(after, "scalar", [-1.0, 2.0, -3.0, 4.0])
            count, percentiles, concentration = scalar_delta_distribution(
                [before], [after]
            )
            self.assertEqual(count, 4)
            self.assertEqual(percentiles["p50"], 2.5)
            self.assertEqual(percentiles["maximum"], 4.0)
            self.assertAlmostEqual(concentration["top_5pct"], 16.0 / 30.0)

    def test_reports_top_scalar_delta_locations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before = root / "before"
            after = root / "after"
            centres = root / "C"
            write_field(before, "scalar", [10.0, 20.0])
            write_field(after, "scalar", [11.0, 25.0])
            write_field(centres, "vector", [1.0, 2.0, 3.0, 4.0, 5.0, 6.0])
            self.assertEqual(
                top_scalar_delta_locations([before], [after], [centres], 1),
                [(5.0, 4.0, 5.0, 6.0, 20.0, 25.0)],
            )


if __name__ == "__main__":
    unittest.main()
