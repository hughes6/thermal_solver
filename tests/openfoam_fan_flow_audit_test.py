import tempfile
import unittest
from pathlib import Path

from tools.openfoam_fan_flow_audit import audit, markdown


class FanFlowAuditTest(unittest.TestCase):
    def write_flow(self, root: Path, rank: int, time: str, fan: str, value: float):
        directory = root / f"processor{rank}" / time / "fluid" / "uniform"
        directory.mkdir(parents=True, exist_ok=True)
        (directory / f"{fan}Properties").write_text(
            f"flow_rate {value};\n", encoding="utf-8")

    def test_audits_common_rank_consistent_checkpoints(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for rank in range(2):
                self.write_flow(root, rank, "0.01", "fan_a", 0.2)
                self.write_flow(root, rank, "0.020000", "fan_a", -0.1)
            times, fans, records = audit(root)
            self.assertEqual(times, [0.01, 0.02])
            self.assertEqual(fans, ["fan_a"])
            report = markdown(times, fans, records)
            self.assertIn("-0.1 ⚠ reverse", report)

    def test_rejects_rank_inconsistency(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_flow(root, 0, "1", "fan_a", 0.2)
            self.write_flow(root, 1, "1", "fan_a", 0.3)
            with self.assertRaisesRegex(ValueError, "rank-inconsistent"):
                audit(root)

    def test_rejects_ambiguous_numeric_times(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_flow(root, 0, "1", "fan_a", 0.2)
            self.write_flow(root, 0, "1.0", "fan_a", 0.2)
            with self.assertRaisesRegex(ValueError, "ambiguous checkpoint"):
                audit(root)


if __name__ == "__main__":
    unittest.main()
