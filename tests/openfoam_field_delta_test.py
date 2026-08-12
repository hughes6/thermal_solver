import struct
import tempfile
import unittest
from pathlib import Path

from tools.openfoam_field_delta import compare_fields, read_internal_field


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
