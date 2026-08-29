import array
import importlib.util
import json
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = ROOT / "tools" / "openfoam_stream_region_selectors.py"
SPEC = importlib.util.spec_from_file_location("selector_mapper", TOOL_PATH)
MAPPER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MAPPER)


def write_mask(path: Path, values: list[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    body = "\n".join(str(value) for value in values)
    path.write_text(
        "FoamFile\n"
        "{\n"
        "    format ascii;\n"
        "    class volScalarField;\n"
        '    location \"0\";\n'
        f"    object {path.name};\n"
        "}\n"
        "dimensions [0 0 0 0 0 0 0];\n"
        "internalField nonuniform List<scalar>\n"
        f"{len(values)}\n(\n{body}\n)\n;\n"
        "boundaryField\n{\n \".*\" { type calculated; value uniform 0; }\n}\n",
        encoding="ascii",
        newline="\n",
    )


def write_addressing(path: Path, values: list[int], binary: bool) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    format_name = "binary" if binary else "ascii"
    header = (
        "FoamFile\n"
        "{\n"
        "    version 2.0;\n"
        f"    format {format_name};\n"
        '    arch \"LSB;label=32;scalar=64\";\n'
        "    class labelList;\n"
        f'    location \"constant/{path.parents[1].name}/polyMesh\";\n'
        "    object cellRegionAddressing;\n"
        "}\n"
        f"{len(values)}\n("
    ).encode("ascii")
    if binary:
        payload = b"".join(struct.pack("<i", value) for value in values)
    else:
        payload = ("\n" + "\n".join(str(value) for value in values) + "\n").encode(
            "ascii"
        )
    path.write_bytes(header + payload + b")\n")


def write_split_inventory(case: Path) -> None:
    (case / "constant").mkdir(parents=True, exist_ok=True)
    (case / "constant" / "regionProperties").write_text(
        "FoamFile { format ascii; class dictionary; object regionProperties; }\n"
        "regions\n(\n fluid (fluid)\n solid (solid_0)\n);\n",
        encoding="ascii",
        newline="\n",
    )
    for name in MAPPER.SPLIT_ROOT_MESH_FILES:
        path = case / "constant" / "polyMesh" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"root {name}\n", encoding="ascii", newline="\n")
    for region in ("fluid", "solid_0"):
        for name in MAPPER.SPLIT_REGION_MESH_FILES:
            path = case / "constant" / region / "polyMesh" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                f"split {region} {name}\n", encoding="ascii", newline="\n"
            )


def read_ascii_values(path: Path) -> list[int]:
    text = path.read_text(encoding="ascii")
    marker = "internalField   nonuniform List<scalar>\n"
    payload = text.split(marker, 1)[1]
    count_text, payload = payload.split("\n", 1)
    count = int(count_text)
    inside = payload.split("(\n", 1)[1].split("\n)\n", 1)[0]
    values = [int(token) for token in inside.split()]
    if len(values) != count:
        raise AssertionError((count, len(values)))
    return values


class StreamingSelectorTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = Path(tempfile.mkdtemp(prefix="openfoam_selector_test_"))
        self.case = self.temp / "case"
        (self.case / "system").mkdir(parents=True)
        (self.case / "0").mkdir()
        (self.case / "prepare_regions.sh").write_text(
            "#!/usr/bin/env bash\n"
            'run_toposet -case "$case_dir" -region solid_0 -time 0 '
            '-dict "$case_dir/system/topoSetDict_heat"\n'
            'run_toposet -case "$case_dir" -region fluid -time 0 '
            '-dict "$case_dir/system/topoSetDict_fan"\n',
            encoding="ascii",
        )
        (self.case / "system" / "topoSetDict_heat").write_text(
            "actions ( { source fieldToCell; field heat_mask; min 0.5; max 1.5; } );\n",
            encoding="ascii",
        )
        (self.case / "system" / "topoSetDict_fan").write_text(
            "actions ( { source fieldToCell; field fan_mask; min 0.5; max 1.5; } );\n",
            encoding="ascii",
        )
        write_mask(self.case / "0" / "heat_mask", [0, 1, 0, 1, 0, 0])
        write_mask(self.case / "0" / "fan_mask", [1, 0, 0, 0, 1, 0])
        write_addressing(
            self.case / "constant" / "fluid" / "polyMesh" / "cellRegionAddressing",
            [0, 2, 4, 5],
            binary=True,
        )
        write_addressing(
            self.case / "constant" / "solid_0" / "polyMesh" / "cellRegionAddressing",
            [1, 3],
            binary=False,
        )
        write_split_inventory(self.case)
        # Restore the parseable addressing files overwritten by the inventory
        # fixture's generic placeholders.
        write_addressing(
            self.case / "constant" / "fluid" / "polyMesh" / "cellRegionAddressing",
            [0, 2, 4, 5],
            binary=True,
        )
        write_addressing(
            self.case / "constant" / "solid_0" / "polyMesh" / "cellRegionAddressing",
            [1, 3],
            binary=False,
        )

    def tearDown(self) -> None:
        shutil.rmtree(self.temp)

    def test_stage_and_exact_materialization(self) -> None:
        mappings = MAPPER.discover_mappings(self.case)
        self.assertEqual(
            [(item.field, item.region) for item in mappings],
            [("fan_mask", "fluid"), ("heat_mask", "solid_0")],
        )
        staged = MAPPER.stage_selectors(self.case)
        self.assertEqual(staged["selector_count"], 2)
        self.assertEqual(staged["moved_this_run"], 2)
        self.assertFalse((self.case / "0" / "heat_mask").exists())
        self.assertTrue(
            (self.case / ".openfoam_selector_fields" / "heat_mask").is_file()
        )

        result = MAPPER.materialize_selectors(self.case)
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["root_cell_count"], 6)
        self.assertEqual(result["selector_count"], 2)
        self.assertEqual(result["maximum_addressing_bytes"], 16)
        self.assertEqual(result["total_addressing_bytes"], 32)
        self.assertEqual(
            read_ascii_values(self.case / "0" / "fluid" / "fan_mask"),
            [1, 0, 1, 0],
        )
        self.assertEqual(
            read_ascii_values(self.case / "0" / "solid_0" / "heat_mask"),
            [1, 1],
        )
        audit = json.loads((self.case / "selector_mapping_audit.json").read_text())
        self.assertEqual(audit["status"], "PASS")
        self.assertEqual(
            MAPPER.verify_materialized_selectors(self.case)["status"], "PASS"
        )
        self.assertEqual(
            sorted(record["selected_cell_count"] for record in audit["records"]),
            [2, 2],
        )

        resumed = MAPPER.stage_selectors(self.case)
        self.assertEqual(resumed["moved_this_run"], 0)

    def test_interrupted_partial_stage_resumes_and_is_deterministic(self) -> None:
        expected = {
            name: hashlib.sha256((self.case / "0" / name).read_bytes()).hexdigest()
            for name in ("fan_mask", "heat_mask")
        }
        staging = self.case / ".openfoam_selector_fields"
        staging.mkdir()
        os.replace(self.case / "0" / "heat_mask", staging / "heat_mask")

        resumed = MAPPER.stage_selectors(self.case)
        self.assertEqual(resumed["moved_this_run"], 1)
        self.assertFalse((self.case / "0" / "fan_mask").exists())
        self.assertFalse((self.case / "0" / "heat_mask").exists())
        for name, digest in expected.items():
            self.assertEqual(hashlib.sha256((staging / name).read_bytes()).hexdigest(), digest)

        first = MAPPER.materialize_selectors(self.case)
        first_outputs = {
            (record["region"], record["field"]): (
                self.case / "0" / record["region"] / record["field"]
            ).read_bytes()
            for record in first["records"]
        }
        first_audit = (self.case / "selector_mapping_audit.json").read_bytes()
        second = MAPPER.materialize_selectors(self.case)
        self.assertEqual(first, second)
        self.assertEqual(first_audit, (self.case / "selector_mapping_audit.json").read_bytes())
        for key, content in first_outputs.items():
            self.assertEqual((self.case / "0" / key[0] / key[1]).read_bytes(), content)

        rerun = MAPPER.stage_selectors(self.case)
        self.assertEqual(rerun["moved_this_run"], 0)
        (staging / "fan_mask").write_bytes(b"corrupted\n")
        with self.assertRaisesRegex(MAPPER.SelectorError, "changed since the prior audit"):
            MAPPER.stage_selectors(self.case)

    def test_lossy_region_projection_refuses_output_replacement(self) -> None:
        MAPPER.stage_selectors(self.case)
        staged = self.case / ".openfoam_selector_fields" / "heat_mask"
        write_mask(staged, [1, 1, 0, 1, 0, 0])
        destination = self.case / "0" / "solid_0" / "heat_mask"
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(b"preserve prior evidence\n")
        (self.case / "selector_mapping_audit.json").write_text(
            '{"status":"PASS","stale":true}\n', encoding="ascii"
        )
        with self.assertRaisesRegex(MAPPER.SelectorError, "refusing lossy projection"):
            MAPPER.materialize_selectors(self.case)
        self.assertEqual(destination.read_bytes(), b"preserve prior evidence\n")
        self.assertFalse((self.case / "selector_mapping_audit.json").exists())

    def test_create_once_existing_equivalence_audit(self) -> None:
        MAPPER.stage_selectors(self.case)
        MAPPER.materialize_selectors(self.case)
        oom_log = self.temp / "oom.log"
        oom_log.write_text(
            'Arch   : "LSB;label=32;scalar=64"\n'
            "Region Cells\n------ -----\n0 4\n1 2\n\n"
            "Reading volScalarField: cellToRegion fan_mask heat_mask\n",
            encoding="ascii",
        )
        audit_path = self.temp / "equivalence.json"
        result = MAPPER.verify_existing_selectors(self.case, audit_path, oom_log)
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["selector_count"], 2)
        self.assertEqual(result["selected_cell_total"], 4)
        inventory = result["retained_19mm_oom_inventory"]
        self.assertEqual(inventory["selector_field_count"], 2)
        self.assertEqual(inventory["root_cell_count_from_region_table"], 6)
        self.assertEqual(inventory["selector_internal_scalar_payload_bytes"], 96)
        with self.assertRaisesRegex(MAPPER.SelectorError, "create-once"):
            MAPPER.verify_existing_selectors(self.case, audit_path, oom_log)

    def test_ambiguous_staging_refuses_before_mutation(self) -> None:
        staging = self.case / ".openfoam_selector_fields"
        staging.mkdir()
        shutil.copy2(self.case / "0" / "fan_mask", staging / "fan_mask")
        with self.assertRaisesRegex(MAPPER.SelectorError, "Ambiguous selector state"):
            MAPPER.stage_selectors(self.case)
        self.assertTrue((self.case / "0" / "heat_mask").is_file())
        self.assertTrue((self.case / "0" / "fan_mask").is_file())

    def test_unsorted_or_out_of_range_addressing_is_rejected(self) -> None:
        path = self.case / "constant" / "fluid" / "polyMesh" / "cellRegionAddressing"
        write_addressing(path, [0, 4, 2, 5], binary=True)
        with self.assertRaisesRegex(MAPPER.SelectorError, "strictly increasing"):
            MAPPER.read_cell_region_addressing(path)
        write_addressing(path, [0, 2, 4, 7], binary=True)
        MAPPER.stage_selectors(self.case)
        with self.assertRaisesRegex(MAPPER.SelectorError, "exceeds selector count"):
            MAPPER.materialize_selectors(self.case)

    def test_split_checkpoint_binds_inputs_addressing_and_output_inventory(self) -> None:
        checkpoint = MAPPER.record_split_state(self.case)
        self.assertEqual(checkpoint["status"], "PASS")
        self.assertEqual(MAPPER.verify_split_state(self.case), checkpoint)

        root_points = self.case / "constant" / "polyMesh" / "points"
        original = root_points.read_bytes()
        root_points.write_bytes(original + b"stale\n")
        with self.assertRaisesRegex(MAPPER.SelectorError, "does not match"):
            MAPPER.verify_split_state(self.case)
        root_points.write_bytes(original)
        MAPPER.record_split_state(self.case)

        missing = self.case / "constant" / "fluid" / "polyMesh" / "faces"
        missing.unlink()
        with self.assertRaisesRegex(MAPPER.SelectorError, "Missing required"):
            MAPPER.verify_split_state(self.case)

    def test_materialization_audit_binds_every_output_and_dictionary(self) -> None:
        MAPPER.stage_selectors(self.case)
        MAPPER.materialize_selectors(self.case)
        self.assertEqual(
            MAPPER.verify_materialized_selectors(self.case)["status"], "PASS"
        )
        output = self.case / "0" / "fluid" / "fan_mask"
        output.unlink()
        with self.assertRaisesRegex(MAPPER.SelectorError, "binding is missing"):
            MAPPER.verify_materialized_selectors(self.case)

        MAPPER.materialize_selectors(self.case)
        dictionary = self.case / "system" / "topoSetDict_fan"
        dictionary.write_text(
            dictionary.read_text(encoding="ascii") + "// stale\n",
            encoding="ascii",
        )
        with self.assertRaisesRegex(MAPPER.SelectorError, "stale dictionary_sha256"):
            MAPPER.verify_materialized_selectors(self.case)

    def test_wrapper_preserves_generated_validation_path(self) -> None:
        wrapper = (ROOT / "tools" / "prepare_openfoam_regions_low_memory.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn('python3 "$mapper" stage --case "$case_dir"', wrapper)
        launcher = '"$foam_launcher" splitMeshRegions'
        self.assertIn(launcher, wrapper)
        self.assertIn('command -v -- "$foam_launcher"', wrapper)
        self.assertIn('flock -n 8', wrapper)
        self.assertIn('python3 "$mapper" verify-split --case "$case_dir"', wrapper)
        self.assertIn('python3 "$mapper" record-split --case "$case_dir"', wrapper)
        self.assertIn('python3 "$mapper" materialize --case "$case_dir"', wrapper)
        self.assertIn(
            'python3 "$mapper" verify-materialized --case "$case_dir"', wrapper
        )
        generated = (
            'THERMAL_SIM_LOW_MEMORY_PREP_ACTIVE=1 '
            'bash "$case_dir/prepare_regions.sh"'
        )
        self.assertIn(generated, wrapper)
        stage_index = wrapper.index('python3 "$mapper" stage')
        split_index = wrapper.index(launcher)
        split_checkpoint_index = wrapper.index('python3 "$mapper" record-split')
        materialize_index = wrapper.index('python3 "$mapper" materialize')
        verification_index = wrapper.index('python3 "$mapper" verify-materialized')
        generated_index = wrapper.index(generated)
        self.assertLess(stage_index, split_index)
        self.assertLess(split_index, split_checkpoint_index)
        self.assertLess(split_checkpoint_index, materialize_index)
        self.assertLess(materialize_index, verification_index)
        self.assertLess(verification_index, generated_index)
        self.assertNotIn('touch "$split_checkpoint"', wrapper)
        self.assertNotIn("wsl.exe", wrapper.lower())

    def test_wrapper_recovers_after_post_split_failure_without_resplitting(self) -> None:
        if os.name == "nt":
            bash = Path(r"C:\Program Files\Git\bin\bash.exe")
            if not bash.is_file():
                self.skipTest("Git Bash is unavailable; refusing to invoke WSL bash")
        else:
            resolved_bash = shutil.which("bash")
            if not resolved_bash:
                self.skipTest("bash is unavailable")
            bash = Path(resolved_bash)

        fake_bin = self.temp / "fake_bin"
        fake_bin.mkdir()
        python_executable = Path(sys.executable).resolve().as_posix()
        fail_once = self.temp / "fail_materialize_once"
        python_shim = fake_bin / "python3"
        python_shim.write_text(
            "#!/usr/bin/env bash\n"
            f'if [[ "${{2:-}}" == materialize && ! -f "{fail_once.as_posix()}" ]]; then\n'
            f'    touch "{fail_once.as_posix()}"\n'
            "    exit 77\n"
            "fi\n"
            f'exec "{python_executable}" "$@"\n',
            encoding="utf-8",
            newline="\n",
        )
        launcher_log = self.temp / "launcher.log"
        fake_launcher = fake_bin / "fake_openfoam"
        fake_launcher.write_text(
            "#!/usr/bin/env bash\n"
            f'printf "%s\\n" "$*" >>"{launcher_log.as_posix()}"\n'
            "[[ \"${1:-}\" == splitMeshRegions ]] || exit 81\n",
            encoding="utf-8",
            newline="\n",
        )
        fake_flock = fake_bin / "flock"
        fake_flock.write_text(
            "#!/usr/bin/env bash\nexit 0\n",
            encoding="utf-8",
            newline="\n",
        )
        os.chmod(python_shim, 0o755)
        os.chmod(fake_launcher, 0o755)
        os.chmod(fake_flock, 0o755)

        shutil.copy2(TOOL_PATH, self.case / "openfoam_stream_region_selectors.py")
        shutil.copy2(
            ROOT / "tools" / "prepare_openfoam_regions_low_memory.sh",
            self.case / "prepare_regions_low_memory.sh",
        )
        # Mimic the fail-closed portion of the actual generated script. The
        # fieldToCell calls remain discoverable but are not executed here; the
        # mapper verifiers are the lifecycle gate exercised by this test.
        (self.case / "prepare_regions.sh").write_text(
            "#!/usr/bin/env bash\n"
            "set -euo pipefail\n"
            'case_dir="$(cd "$(dirname "$0")" && pwd)"\n'
            "if false; then\n"
            'run_toposet -case "$case_dir" -region solid_0 -time 0 '
            '-dict "$case_dir/system/topoSetDict_heat"\n'
            'run_toposet -case "$case_dir" -region fluid -time 0 '
            '-dict "$case_dir/system/topoSetDict_fan"\n'
            "fi\n"
            '[[ "${THERMAL_SIM_LOW_MEMORY_PREP_ACTIVE:-0}" == 1 ]]\n'
            'python3 "$case_dir/openfoam_stream_region_selectors.py" '
            'verify-split --case "$case_dir" >/dev/null\n'
            'python3 "$case_dir/openfoam_stream_region_selectors.py" '
            'verify-materialized --case "$case_dir" '
            '--audit selector_mapping_audit.json >/dev/null\n'
            'touch "$case_dir/.openfoam_regions_prepared"\n',
            encoding="utf-8",
            newline="\n",
        )
        os.chmod(self.case / "prepare_regions.sh", 0o755)

        environment = os.environ.copy()
        environment["PATH"] = str(fake_bin) + os.pathsep + environment.get("PATH", "")
        environment["OPENFOAM_LAUNCHER"] = fake_launcher.as_posix()
        wrapper = self.case / "prepare_regions_low_memory.sh"
        command = [str(bash), wrapper.as_posix(), self.case.as_posix()]
        first = subprocess.run(
            command,
            text=True,
            capture_output=True,
            env=environment,
            timeout=30,
            check=False,
        )
        self.assertEqual(first.returncode, 77, first.stdout + first.stderr)
        invocations = launcher_log.read_text(encoding="utf-8").splitlines()
        self.assertEqual(len(invocations), 1)
        self.assertTrue(invocations[0].startswith("splitMeshRegions -case "))
        self.assertIn(" -cellZonesOnly -overwrite", invocations[0])
        self.assertTrue(
            (
                self.case
                / ".openfoam_prepare_checkpoints"
                / "splitMeshRegions.state.json"
            ).is_file()
        )
        self.assertFalse((self.case / "selector_mapping_audit.json").exists())
        self.assertFalse((self.case / ".openfoam_regions_prepared").exists())

        second = subprocess.run(
            command,
            text=True,
            capture_output=True,
            env=environment,
            timeout=30,
            check=False,
        )
        self.assertEqual(second.returncode, 0, second.stdout + second.stderr)
        self.assertEqual(len(launcher_log.read_text(encoding="utf-8").splitlines()), 1)
        self.assertTrue((self.case / "selector_mapping_audit.json").is_file())
        self.assertTrue((self.case / ".openfoam_regions_prepared").is_file())

        # A stale derived field is deterministically rebuilt from the staged
        # source while the content-bound split remains reusable.
        output = self.case / "0" / "fluid" / "fan_mask"
        output.write_bytes(b"stale output\n")
        third = subprocess.run(
            command,
            text=True,
            capture_output=True,
            env=environment,
            timeout=30,
            check=False,
        )
        self.assertEqual(third.returncode, 0, third.stdout + third.stderr)
        self.assertEqual(len(launcher_log.read_text(encoding="utf-8").splitlines()), 1)
        self.assertNotEqual(output.read_bytes(), b"stale output\n")

    def test_wrapper_preflights_launcher_before_staging_or_lock_creation(self) -> None:
        if os.name == "nt":
            bash = Path(r"C:\Program Files\Git\bin\bash.exe")
            if not bash.is_file():
                self.skipTest("Git Bash is unavailable; refusing to invoke WSL bash")
        else:
            resolved_bash = shutil.which("bash")
            if not resolved_bash:
                self.skipTest("bash is unavailable")
            bash = Path(resolved_bash)
        fake_bin = self.temp / "preflight_bin"
        fake_bin.mkdir()
        python_executable = Path(sys.executable).resolve().as_posix()
        for name, body in {
            "python3": f'#!/usr/bin/env bash\nexec "{python_executable}" "$@"\n',
            "flock": "#!/usr/bin/env bash\nexit 0\n",
        }.items():
            path = fake_bin / name
            path.write_text(body, encoding="utf-8", newline="\n")
            os.chmod(path, 0o755)
        environment = os.environ.copy()
        environment["PATH"] = str(fake_bin) + os.pathsep + environment.get("PATH", "")
        environment["OPENFOAM_LAUNCHER"] = "definitely_missing_openfoam_launcher"
        wrapper = ROOT / "tools" / "prepare_openfoam_regions_low_memory.sh"
        result = subprocess.run(
            [str(bash), wrapper.as_posix(), self.case.as_posix()],
            text=True,
            capture_output=True,
            env=environment,
            timeout=30,
            check=False,
        )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("launcher is unavailable", result.stderr)
        self.assertTrue((self.case / "0" / "fan_mask").is_file())
        self.assertFalse((self.case / ".openfoam_selector_fields").exists())
        self.assertFalse((self.case / ".openfoam_prepare.lock").exists())

    def test_legacy_direct_toposet_commands_are_discovered(self) -> None:
        (self.case / "prepare_regions.sh").write_text(
            "#!/usr/bin/env bash\n"
            '\"$foam_launcher\" topoSet -case \"$case_dir\" -region solid_0 -time 0 '
            '-dict \"$case_dir/system/topoSetDict_heat\"\n'
            '\"$foam_launcher\" topoSet -case \"$case_dir\" -region fluid -time 0 '
            '-dict \"$case_dir/system/topoSetDict_fan\"\n',
            encoding="ascii",
        )
        mappings = MAPPER.discover_mappings(self.case)
        self.assertEqual(
            [(item.field, item.region) for item in mappings],
            [("fan_mask", "fluid"), ("heat_mask", "solid_0")],
        )


if __name__ == "__main__":
    unittest.main()
