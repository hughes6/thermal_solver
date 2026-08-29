import tempfile
from pathlib import Path
import unittest

from tools.openfoam_partition_audit import audit_case, owner_ncells


def write_owner(path: Path, cells: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        (
            "FoamFile\n{\n format binary;\n note \"nPoints:1  nCells:"
            f"{cells}  nFaces:1\";\n class labelList;\n}}\n"
        ).encode("ascii")
        + b"\x00\xffbinary payload"
    )


class OpenFoamPartitionAuditTest(unittest.TestCase):
    def test_binary_owner_header_is_read_without_payload_decoding(self):
        with tempfile.TemporaryDirectory() as directory:
            owner = Path(directory) / "owner"
            write_owner(owner, 12345)
            self.assertEqual(owner_ncells(owner), 12345)

    def test_rank_fluid_and_solid_imbalance_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            write_owner(case / "processor0/constant/fluid/polyMesh/owner", 80)
            write_owner(case / "processor0/constant/solid/polyMesh/owner", 40)
            write_owner(case / "processor1/constant/fluid/polyMesh/owner", 80)
            write_owner(case / "processor1/constant/solid/polyMesh/owner", 0)
            result = audit_case(case)
            self.assertEqual(result.total_cells, 200)
            self.assertEqual(result.ranks_detail[0].solid, 40)
            self.assertEqual(result.ranks_detail[1].solid, 0)
            self.assertAlmostEqual(result.maximum_over_ideal, 0.2)
            self.assertAlmostEqual(result.maximum_to_minimum, 1.5)

    def test_noncontiguous_processor_directories_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            write_owner(case / "processor0/constant/fluid/polyMesh/owner", 1)
            write_owner(case / "processor2/constant/fluid/polyMesh/owner", 1)
            with self.assertRaisesRegex(ValueError, "not contiguous"):
                audit_case(case)


if __name__ == "__main__":
    unittest.main()
