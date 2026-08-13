import tempfile
import unittest
from unittest import mock
from pathlib import Path

from tools.openfoam_progress import (
    choose_log,
    courant_timestep_headroom,
    current_checkpoint_series_count,
    directory_size,
    format_bytes,
    format_duration,
    numeric_directories,
    processor_checkpoints,
    read_control_times,
    read_checkpoint_stride,
    read_thermal_only_flow,
    read_health,
    read_latest_temperature_ranges,
    read_latest_run_request,
    read_latest_thermal_metrics,
    read_initial_airflow_progress,
    read_samples,
    recent_slope,
    recent_slope_or_none,
    is_stale_run,
    storage_usage,
    low_space_warning,
    temperature_warning,
    format_initial_airflow_stage,
)


class OpenFoamProgressTest(unittest.TestCase):
    def test_temperature_warning_is_diagnostic_threshold(self):
        self.assertIsNone(temperature_warning(423.15, 150.0))
        warning = temperature_warning(424.15, 150.0)
        self.assertIn("150 C", warning)
        self.assertIn("component airflow", warning)

    def test_reads_temperature_ranges_from_latest_completed_timestep(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "run.stdout.log"
            log.write_text(
                "Time = 10\nSolving thermal-only fluid region fluid\n"
                "Min/max T:293.15 350\nSolving for solid region server\n"
                "Min/max T:300 400\nExecutionTime = 3 s ClockTime = 4 s\n"
                "Time = 20\nSolving thermal-only fluid region fluid\n"
                "Min/max T:293.15 360\nSolving for solid region server\n"
                "Min/max T:301 410\nExecutionTime = 5 s ClockTime = 6 s\n"
                "Time = 30\nSolving thermal-only fluid region fluid\n"
                "Min/max T:293.15 999\n",
                encoding="utf-8",
            )
            self.assertEqual(
                read_latest_temperature_ranges(log),
                {"fluid": (293.15, 360.0), "server": (301.0, 410.0)},
            )

    def test_reads_normalized_thermal_metrics_and_runner_limits(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            (case / "run_summary.log").write_text(
                "now | thermal time=7200 maxInternalCellChange=5.83854 "
                "maxComponentAverageChange=3.4787 "
                "controllingPeakRegion=Trenton "
                "controllingAverageRegion=Trenton elapsed=2400\n",
                encoding="utf-8",
            )
            (case / "run_parallel.sh").write_text(
                'if ! awk -v v="$scaled_delta" -v limit="0.25"; then :; fi\n'
                'if ! awk -v v="$scaled_average_delta" -v limit="0.1"; then :; fi\n',
                encoding="utf-8",
            )
            self.assertEqual(
                read_latest_thermal_metrics(case),
                (7200.0, 5.83854, 3.4787, "Trenton", "Trenton", 2400.0,
                 0.25, 0.1),
            )

    def test_fast_storage_usage_skips_recursive_case_walk(self):
        fake_usage = mock.Mock(free=12345)
        with mock.patch(
            "tools.openfoam_progress.directory_size"
        ) as case_size, mock.patch(
            "tools.openfoam_progress.shutil.disk_usage", return_value=fake_usage
        ):
            self.assertEqual(storage_usage(Path("case"), fast=True), (None, 12345))
            case_size.assert_not_called()

    def test_low_disk_warning_threshold(self):
        self.assertIsNone(low_space_warning(5 * 1024 ** 3, 5.0))
        warning = low_space_warning(4 * 1024 ** 3, 5.0)
        self.assertIn("4.00 GiB available", warning)
        self.assertIn("threshold 5 GiB", warning)

    def test_current_checkpoint_series_excludes_prior_stage_writes(self):
        checkpoints = [0.43, 0.44, 0.45, 3.205, 3.305, 3.405]
        self.assertEqual(
            current_checkpoint_series_count(checkpoints, 0.1), 3
        )
        self.assertEqual(current_checkpoint_series_count(checkpoints, None), 6)
        self.assertEqual(current_checkpoint_series_count([], 0.1), 0)

    def test_reports_diagnostic_courant_timestep_headroom(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            (case / "system").mkdir()
            (case / "system" / "controlDict").write_text(
                "deltaT 0.001;\nmaxCo 5;\nmaxDeltaT 0.001;\n"
            )
            current, cap, safe, multiplier = courant_timestep_headroom(
                case, 2.5
            )
            self.assertEqual(current, 0.001)
            self.assertEqual(cap, 0.001)
            self.assertAlmostEqual(safe, 0.0016)
            self.assertAlmostEqual(multiplier, 1.6)

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

    def test_rate_prefers_clock_time_over_execution_time(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "run.stdout.log"
            log.write_text(
                "Time = 1\nExecutionTime = 5 s ClockTime = 10 s\n"
                "Time = 2\nExecutionTime = 10 s ClockTime = 30 s\n",
                encoding="utf-8",
            )
            self.assertEqual(read_samples(log), [(1.0, 10.0), (2.0, 30.0)])

    def test_rate_uses_newest_clock_segment_after_solver_restart(self):
        samples = [
            (5.26, 46000.0),
            (5.271, 46088.0),
            (5.272, 10.0),
            (5.273, 20.0),
            (5.274, 30.0),
        ]
        self.assertAlmostEqual(recent_slope(samples, 100), 10000.0, places=3)

    def test_rate_warms_up_with_one_sample_after_solver_restart(self):
        samples = [(5.26, 46000.0), (5.271, 46088.0), (5.272, 10.0)]
        self.assertIsNone(recent_slope_or_none(samples, 100))

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
                "writeControl timeStep;\n"
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

    def test_adjustable_runtime_interval_is_already_physical_time(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            (case / "system").mkdir()
            (case / "system" / "controlDict").write_text(
                "writeControl adjustableRunTime;\n"
                "deltaT 0.01;\nwriteInterval 30;\n",
                encoding="utf-8",
            )
            self.assertEqual(read_checkpoint_stride(case), 30.0)

    def test_reads_thermal_only_solver_mode(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            solution = case / "system" / "fluid" / "fvSolution"
            solution.parent.mkdir(parents=True)
            solution.write_text(
                "PIMPLE\n{\n    thermalOnlyFlow true;\n}\n",
                encoding="utf-8",
            )
            self.assertTrue(read_thermal_only_flow(case))
            solution.write_text(
                "PIMPLE\n{\n    thermalOnlyFlow false;\n}\n",
                encoding="utf-8",
            )
            self.assertFalse(read_thermal_only_flow(case))

    def test_detects_mismatched_parallel_checkpoints(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            for rank in (0, 1):
                (case / f"processor{rank}" / "1").mkdir(parents=True)
                (case / f"processor{rank}" / "1" / "T").write_text("field")
                (case / f"processor{rank}" / "1" / "U").write_text("field")
            (case / "processor0" / "2").mkdir()
            (case / "processor0" / "2" / "T").write_text("field")
            self.assertEqual(
                processor_checkpoints(case), ([1.0], 2, True, [1, 0], [2.0])
            )

    def test_reports_aligned_parallel_checkpoint_file_counts(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            for rank in (0, 1):
                latest = case / f"processor{rank}" / "1.5"
                latest.mkdir(parents=True)
                (latest / "T").write_text("field")
                (latest / "U").write_text("field")
            self.assertEqual(
                processor_checkpoints(case), ([1.5], 2, True, [2, 2], [])
            )

    def test_rejects_aligned_yplus_only_diagnostic_times(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            for rank in (0, 1):
                diagnostic = case / f"processor{rank}" / "300" / "fluid"
                diagnostic.mkdir(parents=True)
                (diagnostic / "yPlus").write_text("diagnostic")
            self.assertEqual(
                processor_checkpoints(case), ([], 2, True, [1, 1], [300.0])
            )

    def test_detects_restart_checkpoint_missing_from_one_rank(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            for rank in (0, 1):
                first = case / f"processor{rank}" / "1" / "fluid"
                first.mkdir(parents=True)
                (first / "T").write_text("field")
                (first / "U").write_text("field")
            second = case / "processor0" / "2" / "fluid"
            second.mkdir(parents=True)
            (second / "T").write_text("field")
            (second / "U").write_text("field")
            self.assertEqual(
                processor_checkpoints(case),
                ([1.0], 2, False, [2, 0], [2.0]),
            )

    def test_accepts_restart_checkpoint_just_above_nominal_log_time(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            for rank in (0, 1):
                checkpoint = case / f"processor{rank}" / "4800.0000000000045" / "fluid"
                checkpoint.mkdir(parents=True)
                (checkpoint / "T").write_text("field")
                (checkpoint / "U").write_text("field")
            self.assertEqual(
                processor_checkpoints(case, maximum_completed_time=4800.0),
                ([4800.000000000005], 2, True, [2, 2], []),
            )

    def test_rejects_equal_rank_partial_checkpoint_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            for rank in (0, 1):
                complete = case / f"processor{rank}" / "1.0"
                complete.mkdir(parents=True)
                (complete / "T").write_text("field")
                (complete / "U").write_text("field")
                partial = case / f"processor{rank}" / "2.0"
                partial.mkdir()
                (partial / "T").write_text("field")
            self.assertEqual(
                processor_checkpoints(case, maximum_completed_time=1.5),
                ([1.0], 2, True, [1, 1], [2.0]),
            )

    def test_detects_mismatched_parallel_checkpoint_file_counts(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            for rank in (0, 1):
                latest = case / f"processor{rank}" / "1.5"
                latest.mkdir(parents=True)
                (latest / "T").write_text("field")
            (case / "processor0" / "1.5" / "U").write_text("field")
            self.assertEqual(
                processor_checkpoints(case), ([], 2, False, [2, 1], [1.5])
            )

    def test_detects_equal_count_but_different_parallel_manifests(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            first = case / "processor0" / "1.5" / "fluid"
            second = case / "processor1" / "1.5" / "fluid"
            first.mkdir(parents=True)
            second.mkdir(parents=True)
            (first / "T").write_text("field")
            (second / "U").write_text("field")
            self.assertEqual(
                processor_checkpoints(case), ([], 2, False, [1, 1], [1.5])
            )

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

    def test_reports_directory_size_and_formats_gibibytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "nested").mkdir()
            (root / "first").write_bytes(b"1234")
            (root / "nested" / "second").write_bytes(b"56789")
            self.assertEqual(directory_size(root), 9)
            self.assertEqual(format_bytes(3 * 1024 ** 3), "3.00 GiB")

    def test_stale_warning_requires_incomplete_old_log(self):
        self.assertTrue(is_stale_run(301.0, 5.0, 10.0, 300.0))
        self.assertFalse(is_stale_run(299.0, 5.0, 10.0, 300.0))
        self.assertFalse(is_stale_run(301.0, 10.0, 10.0, 300.0))

    def test_reads_latest_overall_run_request(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            (case / "run_summary.log").write_text(
                "2026-01-01T00:00:00Z | run_start mode=--multirate "
                "processes=2 requestedEnd=18000 airflowRefreshInterval=2400\n"
                "2026-01-01T01:00:00Z | stage label=test\n"
                "2026-01-02T00:00:00Z | run_start mode=--multirate "
                "processes=4 requestedEnd=100000 airflowRefreshInterval=2400\n",
                encoding="utf-8",
            )
            self.assertEqual(
                read_latest_run_request(case), ("--multirate", 100000.0)
            )

    def test_missing_run_summary_has_no_overall_request(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertIsNone(read_latest_run_request(Path(directory)))

    def test_reads_pending_initial_air_exchange_stage(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            (case / ".initial_airflow_pending").write_text(
                "0.05\n", encoding="utf-8"
            )
            (case / "run_summary.log").write_text(
                "2026-01-01T00:00:00Z | initial_air_exchange_advance "
                "current=0.45 target=5.27144 requiredElapsed=5.22144\n",
                encoding="utf-8",
            )
            self.assertEqual(
                read_initial_airflow_progress(case),
                (0.05, 5.27144, 5.22144, None),
            )
            (case / ".initial_airflow_converged").touch()
            self.assertIsNone(read_initial_airflow_progress(case))

    def test_reads_current_cumulative_initial_air_exchange_stage(self):
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            (case / ".initial_airflow_pending").write_text(
                "0.05\n", encoding="utf-8"
            )
            (case / "run_summary.log").write_text(
                "2026-01-01T00:00:00Z | initial_air_exchange_advance "
                "current=1.25 target=1.35 completedFraction=0.42\n",
                encoding="utf-8",
            )
            self.assertEqual(
                read_initial_airflow_progress(case),
                (0.05, 1.35, None, 0.42),
            )

    def test_initial_exchange_stage_distinguishes_reached_target(self):
        progress = (0.05, 5.27144, 5.22144, None)
        self.assertNotIn(
            "target reached", format_initial_airflow_stage(progress, 5.0)
        )
        self.assertIn(
            "target reached", format_initial_airflow_stage(progress, 5.27144)
        )


if __name__ == "__main__":
    unittest.main()
