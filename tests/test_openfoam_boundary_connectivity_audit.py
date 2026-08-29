import importlib.util
from pathlib import Path
import tempfile
import unittest


MODULE_PATH = Path(__file__).parents[1] / "tools" / "openfoam_boundary_connectivity_audit.py"
SPEC = importlib.util.spec_from_file_location("connectivity_audit", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class LabelListTest(unittest.TestCase):
    def test_uniform_vol_scalar_field_is_accepted_as_integer_labels(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "cellToRegion"
            path.write_text(
                "FoamFile\n{\n format ascii;\n}\n"
                "dimensions [0 0 0 0 0 0 0];\n"
                "internalField uniform 0;\n",
                encoding="ascii",
            )
            labels = MODULE.LabelList(path)
            try:
                self.assertEqual(labels[0], 0)
                self.assertEqual(labels[10_000_000], 0)
                with self.assertRaises(IndexError):
                    _ = labels[-1]
            finally:
                labels.close()


if __name__ == "__main__":
    unittest.main()
