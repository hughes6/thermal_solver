import tempfile
import unittest
import json
from pathlib import Path

from tools.exhaust_recirculation_tracer import (
    MESH_RE,
    compact_output_case,
    copy_case,
    load_devices,
    parse_solver_output,
    reconstruction_command,
    select_reconstructed_source_time,
    select_time,
)


class ExhaustTracerTest(unittest.TestCase):
    def test_reconstruction_command_converts_windows_case_for_wsl(self):
        command = reconstruction_command(
            Path(r"C:\OpenFOAM\thermal_sim_v2\rack case"), "2.12"
        )
        if Path.cwd().drive:
            self.assertEqual(
                command,
                "wsl openfoam2606 reconstructPar -case "
                "'/mnt/c/OpenFOAM/thermal_sim_v2/rack case' "
                "-region fluid -time '2.12'",
            )

    def test_loads_paired_devices_and_ignores_unpaired(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "devices.csv"
            path.write_text("zone,component_id,component,kind,device\nintake0,0,A,intake,front\nintake0b,0,A,intake,front2\noutlet0,0,A,outlet,rear vent\nexhaust0,0,A,exhaust,fan1\nexhaust0b,0,A,exhaust,fan2\nintake1,1,B,intake,front\n", encoding="utf-8")
            devices = load_devices(path)
            self.assertEqual(
                [(d.component, d.intake_zones, d.exhaust_zones)
                 for d in devices],
                [("A", ("intake0", "intake0b"),
                  ("exhaust0", "exhaust0b")),
                 ("B", ("intake1",), ())])

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

    def test_compact_output_preserves_reports_and_logs_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            output = root / "derived"
            source.mkdir()
            (output / "constant").mkdir(parents=True)
            (output / "system").mkdir()
            (output / "12.5" / "fluid").mkdir(parents=True)
            (output / "constant" / "mesh").write_text("mesh")
            (output / "system" / "dictionary").write_text("dictionary")
            (output / "12.5" / "fluid" / "tracer").write_text("field")
            (output / "log.tracer_source_0").write_text("converged")
            for report in (
                "exhaust_recirculation_matrix.csv",
                "exhaust_recirculation_matrix.md",
            ):
                (output / report).write_text("report")
            (output / "exhaust_recirculation_metadata.json").write_text("{}")
            compact_output_case(output, source)
            self.assertFalse((output / "constant").exists())
            self.assertFalse((output / "system").exists())
            self.assertFalse((output / "12.5").exists())
            self.assertTrue((output / "log.tracer_source_0").is_file())
            self.assertTrue((output / "exhaust_recirculation_matrix.csv").is_file())
            metadata = json.loads(
                (output / "exhaust_recirculation_metadata.json").read_text())
            self.assertTrue(metadata["compacted_output"])

    def test_compact_refuses_source_or_incomplete_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, "source OpenFOAM case"):
                compact_output_case(root, root)
            output = root / "derived"
            output.mkdir()
            with self.assertRaisesRegex(ValueError, "before report creation"):
                compact_output_case(output, root)

    def test_selects_latest_exact_and_numeric_time(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            times = []
            for name in ("18.0000000001", "80.01"):
                path = root / name
                (path / "fluid").mkdir(parents=True)
                for field in ("rho", "phi", "nut"):
                    (path / "fluid" / field).write_text(field)
                times.append((float(name), path))
            self.assertEqual(select_time(times, None).name, "80.01")
            self.assertEqual(select_time(times, "18.0000000001").name, "18.0000000001")
            self.assertEqual(select_time(times, "18").name, "18.0000000001")
            with self.assertRaisesRegex(ValueError, "did not uniquely match"):
                select_time(times, "99")

    def test_refuses_stale_root_when_newer_decomposed_state_exists(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            root_time = case / "0.06" / "fluid"
            root_time.mkdir(parents=True)
            for field in ("rho", "phi", "nut"):
                (root_time / field).write_text(field)
            for rank in range(2):
                fluid = case / f"processor{rank}" / "2.12" / "fluid"
                fluid.mkdir(parents=True)
                for field in ("rho", "phi", "nut"):
                    (fluid / field).write_text(field)
            with self.assertRaisesRegex(
                ValueError, r"reconstructPar .* -time '2.12'"
            ):
                select_reconstructed_source_time(case, None)

    def test_reconstructed_latest_remains_valid_with_older_processors(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            for name in ("1", "3"):
                fluid = case / name / "fluid"
                fluid.mkdir(parents=True)
                for field in ("rho", "phi", "nut"):
                    (fluid / field).write_text(field)
            for rank in range(2):
                fluid = case / f"processor{rank}" / "2" / "fluid"
                fluid.mkdir(parents=True)
                for field in ("rho", "phi", "nut"):
                    (fluid / field).write_text(field)
            self.assertEqual(
                select_reconstructed_source_time(case, None).name, "3"
            )


if __name__ == "__main__":
    unittest.main()
