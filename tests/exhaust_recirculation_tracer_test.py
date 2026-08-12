import tempfile
import unittest
from pathlib import Path

from tools.exhaust_recirculation_tracer import MESH_RE, copy_case, load_devices, parse_solver_output


class ExhaustTracerTest(unittest.TestCase):
    def test_loads_paired_devices_and_ignores_unpaired(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "devices.csv"
            path.write_text("zone,component_id,component,kind,device\nintake0,0,A,intake,front\nexhaust0,0,A,exhaust,rear\nintake1,1,B,intake,front\n", encoding="utf-8")
            devices = load_devices(path)
            self.assertEqual([(d.component, d.intake_zone, d.exhaust_zone) for d in devices], [("A", "intake0", "exhaust0")])

    def test_parses_zone_values_and_convergence(self):
        values, mass, change = parse_solver_output("ZONE_AVERAGE,intake0,0.125\nZONE_MASS_INLET,intake0,0.2,0.03\nFinal max change: 9e-10\n", 1e-9)
        self.assertEqual(values["intake0"], 0.125)
        self.assertEqual(mass["intake0"], (0.2, 0.03))
        self.assertEqual(change, 9e-10)
        self.assertEqual(MESH_RE.search("MESH_SIZE,123,456\n").groups(), ("123", "456"))

    def test_rejects_unconverged_result(self):
        with self.assertRaisesRegex(ValueError, "did not converge"):
            parse_solver_output("ZONE_AVERAGE,intake0,0.1\nZONE_MASS_INLET,intake0,0.1,0.03\nFinal max change: 2e-7\n", 1e-9)

    def test_copy_case_preserves_override_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            output = root / "output"
            time = source / "12" / "fluid"
            (source / "constant" / "fluid" / "polyMesh").mkdir(parents=True)
            (source / "system" / "fluid").mkdir(parents=True)
            time.mkdir(parents=True)
            (source / "constant" / "fluid" / "polyMesh" / "points").write_text("mesh")
            for field in ("rho", "phi", "nut"):
                (time / field).write_text(field)
            (source / "system" / "controlDict").write_text("control")
            (source / "system" / "fluid" / "fvSchemes").write_text("schemes")
            (source / "system" / "fluid" / "fvSolution").write_text(
                'solvers\n{\n    "(U|h|k|omega)" { solver PBiCGStab; }\n}\n')
            metadata = root / "legacy-devices.csv"
            metadata.write_text("zone,component_id,component,kind,device\n")
            copy_case(source, output, time.parent, metadata)
            self.assertEqual((output / "internal_airflow_devices.csv").read_text(), metadata.read_text())
            copied = (output / "system" / "fluid" / "fvSolution").read_text()
            self.assertIn('"tracer.*"', copied)
            self.assertIn("tolerance       1e-12", copied)
            self.assertIn("relTol          0", copied)


if __name__ == "__main__":
    unittest.main()
