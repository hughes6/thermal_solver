import tempfile
import unittest
from pathlib import Path

from tools.openfoam_progress import (
    choose_log,
    format_duration,
    numeric_directories,
    read_control_times,
    read_checkpoint_stride,
    read_health,
    read_samples,
    recent_slope,
)


class OpenFoamProgressTest(unittest.TestCase):
    def test_reads_samples_and_recent_rate(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "run.stdout.log"
            log.write_text(
                "Time = 1\nExecutionTime = 10 s ClockTime = 10 s\n"
                "Time = 2\nExecutionTime = 30 s ClockTime = 30 s\n"
                "Time = 3\nExecutionTime = 50 s ClockTime = 50 s\n",
                encoding="utf-8",
            )
            samples = read_samples(log)
            self.assertEqual(samples, [(1.0, 10.0), (2.0, 30.0), (3.0, 50.0)])
            self.assertAlmostEqual(recent_slope(samples, 3), 20.0)

    def test_health_parser_ignores_trap_banner(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "run.stdout.log"
            log.write_text(
                "trapFpe: Floating point exception trapping enabled (FOAM_SIGFPE).\n"
                "Region: fluid Courant Number mean: 0.1 max: 2.5\n"
                "time step continuity errors (fluid): sum local = 1e-8, "
                "global = 1e-9, cumulative = 6e-6\n",
                encoding="utf-8",
            )
            self.assertEqual(read_health(log), (2.5, 6e-6, []))

    def test_health_parser_reports_exact_fatal_signature(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "run.stdout.log"
            log.write_text("--> FOAM FATAL ERROR: failure\n", encoding="utf-8")
            self.assertEqual(
                read_health(log), (None, None, ["--> FOAM FATAL ERROR"])
            )

    def test_health_parser_resets_at_latest_stage(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "run.stdout.log"
            log.write_text(
                "--> FOAM FATAL ERROR: recovered old stage\n"
                "Adaptive initial airflow: t=0.5 -> 1.5\n"
                "Region: fluid Courant Number mean: 0.2 max: 3.0\n",
                encoding="utf-8",
            )
            self.assertEqual(read_health(log), (3.0, None, []))

    def test_reads_end_time_and_numeric_checkpoints(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            (case / "system").mkdir()
            (case / "system" / "controlDict").write_text(
                "startTime 2.5;\nendTime 12.5;\n"
                "deltaT 0.01;\nwriteInterval 25;\n",
                encoding="utf-8",
            )
            processor = case / "processor0"
            processor.mkdir()
            for name in ("0.5", "2", "uniform"):
                (processor / name).mkdir()
            self.assertEqual(read_control_times(case), (2.5, 12.5))
            self.assertEqual(read_checkpoint_stride(case), 0.25)
            self.assertEqual(numeric_directories(processor), [0.5, 2.0])

    def test_selects_latest_log_and_formats_duration(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            older = case / "old.stdout.log"
            newer = case / "new.stdout.log"
            older.write_text("old", encoding="utf-8")
            newer.write_text("new", encoding="utf-8")
            older.touch()
            newer.touch()
            self.assertEqual(choose_log(case, newer), newer)
            self.assertEqual(format_duration(3661), "1h 01m 01s")


if __name__ == "__main__":
    unittest.main()
