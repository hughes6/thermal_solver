import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "plot"))

from run_metadata import load_last_run, resolve_case


class RunMetadataTest(unittest.TestCase):
    def test_explicit_case_remains_backward_compatible(self):
        explicit = Path("explicit_case")
        self.assertEqual(resolve_case(explicit), explicit.resolve())

    def test_resolves_valid_last_openfoam_case(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            case = root / "long_generated_case_name"
            (case / "system").mkdir(parents=True)
            (case / "system" / "controlDict").write_text("", encoding="utf-8")
            metadata = root / ".thermal_sim_last_run.json"
            metadata.write_text(json.dumps({
                "schema_version": 1,
                "backend": "openfoam",
                "case_directory": str(case),
            }), encoding="utf-8")
            data, source = load_last_run(metadata)
            self.assertEqual(source, metadata.resolve())
            self.assertEqual(data["backend"], "openfoam")
            self.assertEqual(resolve_case(None, metadata), case.resolve())

    def test_native_metadata_requires_explicit_case(self):
        with tempfile.TemporaryDirectory() as directory:
            metadata = Path(directory) / ".thermal_sim_last_run.json"
            metadata.write_text(json.dumps({
                "schema_version": 1,
                "backend": "native",
                "case_directory": "",
            }), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "has no OpenFOAM case"):
                resolve_case(None, metadata)


if __name__ == "__main__":
    unittest.main()
