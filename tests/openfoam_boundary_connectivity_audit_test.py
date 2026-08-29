import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "connectivity_audit", ROOT / "tools/openfoam_boundary_connectivity_audit.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def label_list(path, values, binary=False):
    header = (
        "FoamFile\n{\n    format " + ("binary" if binary else "ascii") +
        ";\n    class labelList;\n}\n" + str(len(values)) + "\n(\n"
    ).encode("ascii")
    body = (
        b"".join(struct.pack("<i", value) for value in values)
        if binary else " ".join(map(str, values)).encode("ascii")
    )
    path.write_bytes(header + body + b"\n)\n")


class BoundaryConnectivityAuditTest(unittest.TestCase):
    def test_audit_maps_ascii_and_binary_label_lists(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mesh = root / "polyMesh"
            mesh.mkdir()
            label_list(mesh / "owner", [0, 1, 2, 1], binary=True)
            label_list(root / "cellToRegion", [0, 1, 1])
            (mesh / "boundary").write_text(
                "2\n(\n"
                "    inlet\n    {\n        type patch;\n        nFaces 2;\n"
                "        startFace 1;\n    }\n"
                "    walls\n    {\n        type wall;\n        nFaces 1;\n"
                "        startFace 3;\n    }\n)\n",
                encoding="ascii",
            )
            rows = MODULE.audit(mesh, root / "cellToRegion")
            self.assertEqual(rows[0]["regions"], {1: 2})
            self.assertEqual(rows[1]["regions"], {1: 1})

    def test_validation_enforces_region_count_and_opening_location(self):
        rows = [
            {"name": "inlet", "type": "patch", "regions": {0: 3}},
            {"name": "wall", "type": "mappedWall", "regions": {0: 2, 1: 1}},
        ]
        self.assertEqual(MODULE.validate(rows, 2, 0), [])
        errors = MODULE.validate(rows, 3, 1)
        self.assertEqual(len(errors), 2)
        self.assertIn("expected 3", errors[0])
        self.assertIn("physical opening inlet", errors[1])


if __name__ == "__main__":
    unittest.main()
