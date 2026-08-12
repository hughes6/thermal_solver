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


if __name__ == "__main__":
    unittest.main()
