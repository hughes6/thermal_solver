import json
import io
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

from tools.model_release_readiness import (
    ReadinessInputError,
    audit_release_readiness,
    main,
)


ROOT = Path(__file__).resolve().parents[1]


def _write_ready_fixture(root: Path, *, status: str = "verified", evidence=True) -> Path:
    (root / "model.toml").write_text('name = "fixture"\n', encoding="utf-8")
    if evidence:
        (root / "measurement.txt").write_text("measured\n", encoding="utf-8")
    evidence_line = 'evidence = ["measurement.txt"]\n' if evidence else ""
    ledger = root / "assumptions.toml"
    ledger.write_text(
        "schema = \"thermal-sim-research-lab-assumptions-v1\"\n"
        "model = \"model.toml\"\n"
        "\n[[assumption]]\n"
        "id = \"measured_geometry\"\n"
        "category = \"geometry\"\n"
        f"status = \"{status}\"\n"
        "closure = \"Bind the dimensional inspection record.\"\n"
        + evidence_line,
        encoding="utf-8",
    )
    return ledger


class ModelReleaseReadinessTest(unittest.TestCase):
    def test_current_canonical_ledger_fails_with_all_nine_inputs_open(self):
        report = audit_release_readiness(
            ROOT / "validation" / "UPDATED_MODEL_ASSUMPTIONS.toml", ROOT
        )
        self.assertEqual(report["status"], "FAIL")
        self.assertFalse(report["release_ready"])
        self.assertEqual(report["summary"]["assumption_count"], 9)
        self.assertEqual(report["summary"]["verified_count"], 0)
        self.assertEqual(report["summary"]["open_count"], 9)
        self.assertEqual(
            report["summary"]["open_ids"],
            [
                "rail2_pdu_depth",
                "rail2_meanwell_depths",
                "rail2_ni_depth",
                "ni_separator_walls",
                "storage_shelf_construction",
                "unmodeled_obstructions",
                "heat_load_inventory",
                "fan_curves",
                "openfoam_material_homogenization",
            ],
        )
        self.assertEqual(
            report["model"]["path"], "library/models/new_model_updated.toml"
        )
        self.assertEqual(len(report["model"]["sha256"]), 64)

    def test_verified_row_requires_and_hashes_existing_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ledger = _write_ready_fixture(root)
            report = audit_release_readiness(ledger, root)
        self.assertEqual(report["status"], "PASS")
        self.assertTrue(report["release_ready"])
        self.assertEqual(report["summary"]["verified_count"], 1)
        descriptor = report["assumptions"][0]["evidence"][0]
        self.assertEqual(descriptor["path"], "measurement.txt")
        self.assertEqual(descriptor["bytes"], 10)
        self.assertEqual(len(descriptor["sha256"]), 64)

    def test_verified_without_evidence_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ledger = _write_ready_fixture(root, evidence=False)
            with self.assertRaisesRegex(ReadinessInputError, "no hashable evidence"):
                audit_release_readiness(ledger, root)

    def test_unknown_status_and_duplicate_id_fail_schema_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ledger = _write_ready_fixture(root, status="probably_done")
            with self.assertRaisesRegex(ReadinessInputError, "not recognized"):
                audit_release_readiness(ledger, root)

            ledger = _write_ready_fixture(root)
            duplicate = ledger.read_text(encoding="utf-8").split("[[assumption]]", 1)[1]
            ledger.write_text(
                ledger.read_text(encoding="utf-8")
                + "\n[[assumption]]"
                + duplicate,
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ReadinessInputError, "duplicate assumption id"):
                audit_release_readiness(ledger, root)

    def test_paths_cannot_escape_workspace(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ledger = _write_ready_fixture(root)
            ledger.write_text(
                ledger.read_text(encoding="utf-8").replace(
                    'model = "model.toml"', 'model = "../outside.toml"'
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ReadinessInputError, "escapes the workspace"):
                audit_release_readiness(ledger, root)

    def test_cli_creates_evidence_once_and_returns_release_verdict(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ledger = _write_ready_fixture(root)
            output = root / "readiness.json"
            arguments = [
                "--workspace",
                str(root),
                "--ledger",
                str(ledger),
                "--output",
                str(output),
            ]
            standard_output = io.StringIO()
            with redirect_stdout(standard_output):
                self.assertEqual(main(arguments), 0)
            self.assertTrue(json.loads(standard_output.getvalue())["release_ready"])
            payload = json.loads(output.read_text(encoding="utf-8"))
            self.assertTrue(payload["release_ready"])
            standard_error = io.StringIO()
            with redirect_stderr(standard_error):
                self.assertEqual(main(arguments), 2)
            self.assertEqual(json.loads(standard_error.getvalue())["status"], "ERROR")


if __name__ == "__main__":
    unittest.main()
