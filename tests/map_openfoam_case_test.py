import tempfile
import unittest
from pathlib import Path

from tools.map_openfoam_case import (
    build_commands,
    exact_time_path,
    regions,
    validate_cases,
)


def make_case(root: Path, name: str, time_name: str = "0") -> Path:
    case = root / name
    (case / "system").mkdir(parents=True)
    (case / "system" / "controlDict").write_text("endTime 1;\n", encoding="utf-8")
    (case / "constant").mkdir()
    (case / "constant" / "regionProperties").write_text(
        "regions\n(\n fluid (fluid)\n solid (solid_a solid_b)\n);\n",
        encoding="utf-8",
    )
    checkpoint = case / time_name
    (case / "prepare_regions.sh").write_text("#!/bin/bash\n")
    (case / "run_parallel.sh").write_text("#!/bin/bash\n")
    for region in ("fluid", "solid_a", "solid_b"):
        (checkpoint / region).mkdir(parents=True)
        (checkpoint / region / "T").write_text("internalField uniform 300;\n")
    (checkpoint / "fluid" / "U").write_text("internalField nonuniform;\n")
    return case


class MapOpenFoamCaseTest(unittest.TestCase):
    def test_region_discovery_and_time_tolerance(self):
        with tempfile.TemporaryDirectory() as directory:
            case = make_case(Path(directory), "source", "28800.009999999991")
            self.assertEqual(regions(case), ["fluid", "solid_a", "solid_b"])
            self.assertEqual(
                exact_time_path(case, 28800.01).name, "28800.009999999991")

    def test_validation_accepts_only_fresh_distinct_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = make_case(root, "source", "10")
            target = make_case(root, "target")
            self.assertEqual(validate_cases(source, target, 10.0).name, "10")
            (target / "1" / "fluid").mkdir(parents=True)
            with self.assertRaisesRegex(ValueError, "nonzero reconstructed"):
                validate_cases(source, target, 10.0)

    def test_validation_rejects_existing_partitions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = make_case(root, "source", "10")
            target = make_case(root, "target")
            (target / "processor0").mkdir()
            with self.assertRaisesRegex(ValueError, "processor partitions"):
                validate_cases(source, target, 10.0)

    def test_commands_prepare_then_map_every_region_then_warm_start(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = make_case(root, "source", "10")
            target = make_case(root, "target")
            commands = build_commands(source, target, 10.0, 0.03, 4, "foam")
            self.assertEqual(len(commands), 6)
            self.assertIn("prepare_regions.sh", commands[0])
            self.assertIn("-sourceRegion fluid -targetRegion fluid", commands[1])
            self.assertIn("-sourceRegion solid_a -targetRegion solid_a", commands[2])
            self.assertIn("-sourceRegion solid_b -targetRegion solid_b", commands[3])
            self.assertIn("mv -- 0 10", commands[4])
            self.assertIn("run_parallel.sh 4 --warm-start 0.029999999999999999", commands[5])


if __name__ == "__main__":
    unittest.main()
