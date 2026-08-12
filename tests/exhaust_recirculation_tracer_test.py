import tempfile
import unittest
from pathlib import Path

from tools.exhaust_recirculation_tracer import load_devices, parse_solver_output


class ExhaustTracerTest(unittest.TestCase):
    def test_loads_paired_devices_and_ignores_unpaired(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "devices.csv"
            path.write_text("zone,component_id,component,kind,device\nintake0,0,A,intake,front\nexhaust0,0,A,exhaust,rear\nintake1,1,B,intake,front\n", encoding="utf-8")
            devices = load_devices(path)
            self.assertEqual([(d.component, d.intake_zone, d.exhaust_zone) for d in devices], [("A", "intake0", "exhaust0")])

    def test_parses_zone_values_and_convergence(self):
        values, change = parse_solver_output("ZONE_AVERAGE,intake0,0.125\nFinal max change: 9e-10\n", 1e-9)
        self.assertEqual(values["intake0"], 0.125)
        self.assertEqual(change, 9e-10)

    def test_rejects_unconverged_result(self):
        with self.assertRaisesRegex(ValueError, "did not converge"):
            parse_solver_output("ZONE_AVERAGE,intake0,0.1\nFinal max change: 2e-7\n", 1e-9)


if __name__ == "__main__":
    unittest.main()
