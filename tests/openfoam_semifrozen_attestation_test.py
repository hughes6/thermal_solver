import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = REPO_ROOT / "tools" / "openfoam_semifrozen_attestation.py"
SPEC = importlib.util.spec_from_file_location("semifrozen_attestation", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
attestation = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = attestation
SPEC.loader.exec_module(attestation)


class SemiFrozenAttestationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(
            prefix="thermal_sim_attestation_test_"
        )
        self.temp_root = Path(self.temporary.name)
        self.repo = self.temp_root / "repo"
        for relative in attestation.SOURCE_INPUTS:
            source = REPO_ROOT / relative
            destination = self.repo / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
        self.binary = self.temp_root / "semiFrozenChtMultiRegionFoam"
        self.binary.write_bytes(b"synthetic solver binary\n")
        if os.name != "nt":
            self.binary.chmod(0o755)
        self.source_sha, _ = attestation.source_fingerprint(self.repo)
        self.expected_identity = {
            "expected_foam_api": "2606",
            "expected_wm_project_version": "v2606",
            "expected_wm_options": "linux64GccDPInt32Opt",
        }

    def tearDown(self):
        self.temporary.cleanup()

    def line(self, **overrides):
        fields = {
            "solver": attestation.EXPECTED_SOLVER,
            "project_source_sha256": self.source_sha,
            "policy": attestation.EXPECTED_POLICY,
            "foam_api": "2606",
            "wm_project_version": "v2606",
            "wm_options": "linux64GccDPInt32Opt",
        }
        fields.update(overrides)
        return attestation.ATTESTATION_SCHEMA + " " + " ".join(
            f"{key}={fields[key]}" for key in attestation.ATTESTATION_KEYS
        ) + "\n"

    def runner(self, stdout=None, stderr="", returncode=0):
        response = self.line() if stdout is None else stdout

        def invoke(command, cwd, timeout_seconds):
            self.assertEqual(command, [str(self.binary.resolve()), "--thermal-sim-attest"])
            self.assertEqual(cwd, self.repo.resolve())
            self.assertGreater(timeout_seconds, 0)
            return subprocess.CompletedProcess(
                command, returncode, stdout=response, stderr=stderr
            )

        return invoke

    def verify(self, **kwargs):
        values = dict(
            repo_root=self.repo,
            binary=self.binary,
            command_runner=self.runner(),
            **self.expected_identity,
        )
        values.update(kwargs)
        return attestation.verify_runtime_attestation(**values)

    def test_exact_runtime_source_and_build_identity_passes(self):
        result = self.verify()
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["project_source_sha256"], self.source_sha)
        self.assertEqual(
            result["binary_sha256"], hashlib.sha256(self.binary.read_bytes()).hexdigest()
        )
        self.assertFalse(result["case_accessed"])
        self.assertEqual(len(result["project_source_inputs"]), 3)

    def test_each_repository_local_input_is_bound_into_source_fingerprint(self):
        original = self.source_sha
        for relative in attestation.SOURCE_INPUTS:
            path = self.repo / relative
            saved = path.read_bytes()
            path.write_bytes(saved + b"\nattestation-change")
            changed, _ = attestation.source_fingerprint(self.repo)
            self.assertNotEqual(changed, original, relative)
            path.write_bytes(saved)
            restored, _ = attestation.source_fingerprint(self.repo)
            self.assertEqual(restored, original, relative)

    def test_source_change_after_build_fails_closed(self):
        stale_line = self.line()
        source = self.repo / attestation.SOURCE_INPUTS[-1]
        source.write_bytes(source.read_bytes() + b"\n// post-build change\n")
        with self.assertRaisesRegex(
            attestation.AttestationError,
            "does not match current project source/build identity",
        ):
            self.verify(command_runner=self.runner(stdout=stale_line))

    def test_binary_or_source_change_during_handshake_fails_closed(self):
        def mutate_binary(command, cwd, timeout_seconds):
            self.binary.write_bytes(b"replacement during attestation\n")
            return subprocess.CompletedProcess(
                command, 0, stdout=self.line(), stderr=""
            )

        with self.assertRaisesRegex(attestation.AttestationError, "binary changed"):
            self.verify(command_runner=mutate_binary)

        self.binary.write_bytes(b"synthetic solver binary\n")
        source = self.repo / attestation.SOURCE_INPUTS[-1]

        def mutate_source(command, cwd, timeout_seconds):
            source.write_bytes(source.read_bytes() + b"\n// raced change\n")
            return subprocess.CompletedProcess(
                command, 0, stdout=self.line(), stderr=""
            )

        with self.assertRaisesRegex(
            attestation.AttestationError, "source changed during"
        ):
            self.verify(command_runner=mutate_source)

    def test_original_binary_symlink_is_rejected_before_execution(self):
        link = self.temp_root / "solver-link"
        try:
            link.symlink_to(self.binary)
        except OSError as exc:
            self.skipTest(f"symlink creation unavailable: {exc}")
        invoked = False

        def must_not_run(command, cwd, timeout_seconds):
            nonlocal invoked
            invoked = True
            raise AssertionError("symlink target must not execute")

        with self.assertRaisesRegex(attestation.AttestationError, "must not be a symlink"):
            self.verify(binary=link, command_runner=must_not_run)
        self.assertFalse(invoked)

    def test_marker_only_stale_binary_fails_closed(self):
        stale = attestation.EXPECTED_POLICY + "\n"
        with self.assertRaisesRegex(attestation.AttestationError, "schema mismatch"):
            self.verify(command_runner=self.runner(stdout=stale))

    def test_nonzero_or_stderr_runtime_response_fails_closed(self):
        with self.assertRaisesRegex(attestation.AttestationError, "exited 97"):
            self.verify(command_runner=self.runner(stdout="", returncode=97))
        with self.assertRaisesRegex(attestation.AttestationError, "unexpected stderr"):
            self.verify(command_runner=self.runner(stderr="loader warning\n"))

    def test_wrong_policy_or_openfoam_identity_fails_closed(self):
        with self.assertRaisesRegex(attestation.AttestationError, "policy"):
            self.verify(
                command_runner=self.runner(
                    stdout=self.line(policy="THERMAL_SIM_OLD_POLICY")
                )
            )
        with self.assertRaisesRegex(attestation.AttestationError, "foam_api"):
            self.verify(
                command_runner=self.runner(stdout=self.line(foam_api="2312"))
            )

    def test_extra_duplicate_or_malformed_fields_fail_closed(self):
        with self.assertRaisesRegex(attestation.AttestationError, "keys/order mismatch"):
            self.verify(
                command_runner=self.runner(
                    stdout=self.line().rstrip("\n") + " extra=value\n"
                )
            )
        duplicate = self.line().rstrip("\n") + " solver=duplicate\n"
        with self.assertRaisesRegex(attestation.AttestationError, "duplicate"):
            self.verify(command_runner=self.runner(stdout=duplicate))
        with self.assertRaisesRegex(attestation.AttestationError, "malformed"):
            self.verify(
                command_runner=self.runner(
                    stdout=attestation.ATTESTATION_SCHEMA + " not-a-pair\n"
                )
            )

    def test_binary_digest_pin_fails_before_runtime_on_mismatch(self):
        invoked = False

        def must_not_run(command, cwd, timeout_seconds):
            nonlocal invoked
            invoked = True
            raise AssertionError("runtime must not execute after binary hash mismatch")

        with self.assertRaisesRegex(attestation.AttestationError, "binary SHA-256 mismatch"):
            self.verify(
                expected_binary_sha256="0" * 64,
                command_runner=must_not_run,
            )
        self.assertFalse(invoked)

    def test_attestation_option_is_no_case_and_precedes_openfoam(self):
        source = (
            REPO_ROOT
            / "openfoam_semifrozen_solver"
            / "semiFrozenChtMultiRegionFoam.C"
        ).read_text(encoding="utf-8")
        option = source.index('"--thermal-sim-attest"')
        note = source.index("argList::addNote")
        postprocess = source.index('#include "postProcess.H"')
        root_case = source.index('#include "setRootCaseLists.H"')
        self.assertLess(option, note)
        self.assertLess(option, postprocess)
        self.assertLess(option, root_case)
        self.assertIn("argc != 2", source[option:note])
        self.assertIn("return 64;", source[option:note])
        self.assertIn("return 0;", source[option:note])
        self.assertIn('#include "generated/solverBuildAttestation.H"', source)

    def test_exported_runner_pin_matches_current_project_source(self):
        exporter = (REPO_ROOT / "src" / "openfoam_exporter.hpp").read_text(
            encoding="utf-8"
        )
        match = re.search(
            r'semi_frozen_solver_project_source_sha256\[\]\s*=\s*'
            r'"([0-9a-f]{64})"',
            exporter,
        )
        self.assertIsNotNone(match)
        current, _ = attestation.source_fingerprint(REPO_ROOT)
        self.assertEqual(match.group(1), current)
        self.assertIn("--thermal-sim-attest", exporter)
        self.assertNotIn('grep -aFq -- "$solver_mode_policy_marker"', exporter)

    def _write_microcase_template(self):
        template = self.temp_root / "microcase"
        (template / "0").mkdir(parents=True)
        (template / "constant").mkdir()
        (template / "system" / "fluid").mkdir(parents=True)
        (template / "constant" / "regionProperties").write_text(
            "regions ( fluid (fluid) solid (solid) );\n", encoding="utf-8"
        )
        (template / "system" / "controlDict").write_text(
            "startFrom latestTime;\n"
            "startTime 0;\n"
            "stopAt endTime;\n"
            "endTime 5;\n",
            encoding="utf-8",
        )
        (template / "system" / "fluid" / "fvSolution").write_text(
            "PIMPLE\n{\n thermalOnlyFlow false;\n}\n", encoding="utf-8"
        )
        (template / "0" / "T").write_text("untouched\n", encoding="utf-8")
        return template

    def test_negative_runtime_microcase_is_copy_only_and_zero_step(self):
        template = self._write_microcase_template()
        before, _ = attestation._tree_fingerprint(template)

        def reject_forbidden_mode(command, cwd, timeout_seconds):
            self.assertEqual(command[0], str(self.binary.resolve()))
            self.assertEqual(command[1], "-case")
            copied_case = Path(command[2])
            self.assertEqual(cwd, copied_case)
            control = (copied_case / "system" / "controlDict").read_text()
            solution = (copied_case / "system" / "fluid" / "fvSolution").read_text()
            self.assertIn("startFrom startTime;", control)
            self.assertIn("endTime 0;", control)
            self.assertIn("thermalOnlyFlow false;", solution)
            self.assertIn("isothermalAirflow true;", solution)
            return subprocess.CompletedProcess(
                command,
                1,
                stdout="policy initialized\n",
                stderr=attestation.ISOTHERMAL_REJECTION + ". Physical time was not advanced.\n",
            )

        result = attestation.run_negative_isothermal_microcase(
            binary=self.binary,
            template=template,
            scratch_root=self.temp_root,
            command_runner=reject_forbidden_mode,
        )
        after, _ = attestation._tree_fingerprint(template)
        self.assertEqual(before, after)
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["physical_time_advance_lines"], 0)
        self.assertEqual(result["copied_case_numeric_times_after"], ["0"])

    def test_preserved_twenty_cell_template_is_compatible_and_unchanged(self):
        template = (
            REPO_ROOT
            / "validation"
            / "openfoam_solver_attestation_microcase_template_2026-08-27"
        )
        owner = (template / "constant" / "polyMesh" / "owner").read_text(
            encoding="utf-8"
        )
        self.assertIn("// nCells: 20 nFaces: 84", owner)
        before, times = attestation._tree_fingerprint(template)
        self.assertEqual(times, ["0"])

        def reject_forbidden_mode(command, cwd, timeout_seconds):
            copied_case = Path(command[2])
            solution = (copied_case / "system" / "fluid" / "fvSolution").read_text()
            control = (copied_case / "system" / "controlDict").read_text()
            self.assertIn("isothermalAirflow true;", solution)
            self.assertIn("endTime         0;", control)
            return subprocess.CompletedProcess(
                command,
                1,
                stdout="",
                stderr=attestation.ISOTHERMAL_REJECTION + "\n",
            )

        result = attestation.run_negative_isothermal_microcase(
            binary=self.binary,
            template=template,
            scratch_root=self.temp_root,
            command_runner=reject_forbidden_mode,
        )
        after, _ = attestation._tree_fingerprint(template)
        self.assertEqual(after, before)
        self.assertEqual(result["status"], "PASS")

    def test_negative_microcase_rejects_false_success_or_time_advance(self):
        template = self._write_microcase_template()

        def false_success(command, cwd, timeout_seconds):
            return subprocess.CompletedProcess(
                command,
                0,
                stdout="Time = 0.1\n" + attestation.ISOTHERMAL_REJECTION,
                stderr="",
            )

        with self.assertRaisesRegex(
            attestation.AttestationError, "accepted.*Time = advancement"
        ):
            attestation.run_negative_isothermal_microcase(
                binary=self.binary,
                template=template,
                scratch_root=self.temp_root,
                command_runner=false_success,
            )

    def test_negative_microcase_detects_scientific_time_and_time_zero_mutation(self):
        template = self._write_microcase_template()

        def create_scientific_time(command, cwd, timeout_seconds):
            copied_case = Path(command[2])
            (copied_case / "1e-05").mkdir()
            return subprocess.CompletedProcess(
                command,
                1,
                stdout="",
                stderr=attestation.ISOTHERMAL_REJECTION + "\n",
            )

        with self.assertRaisesRegex(
            attestation.AttestationError, "numeric time directories"
        ):
            attestation.run_negative_isothermal_microcase(
                binary=self.binary,
                template=template,
                scratch_root=self.temp_root,
                command_runner=create_scientific_time,
            )

        def mutate_time_zero(command, cwd, timeout_seconds):
            copied_case = Path(command[2])
            (copied_case / "0" / "T").write_text("mutated\n", encoding="utf-8")
            return subprocess.CompletedProcess(
                command,
                1,
                stdout="",
                stderr=attestation.ISOTHERMAL_REJECTION + "\n",
            )

        with self.assertRaisesRegex(attestation.AttestationError, "changed the copied"):
            attestation.run_negative_isothermal_microcase(
                binary=self.binary,
                template=template,
                scratch_root=self.temp_root,
                command_runner=mutate_time_zero,
            )

    def test_evidence_is_create_only(self):
        evidence = self.temp_root / "evidence.json"
        payload = {"status": "PASS", "project_source_sha256": self.source_sha}
        attestation._write_json_exclusive(evidence, payload)
        self.assertEqual(json.loads(evidence.read_text()), payload)
        with self.assertRaises(FileExistsError):
            attestation._write_json_exclusive(evidence, {"status": "FAIL"})

    def test_build_wrapper_is_clean_fail_closed_and_does_not_start_wsl(self):
        wrapper = (REPO_ROOT / "tools" / "build_openfoam_semifrozen_solver.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn('FOAM_USER_APPBIN="$temporary_appbin" wclean', wrapper)
        self.assertIn('if [[ -e "$built_target" || -L "$built_target" ]]', wrapper)
        self.assertIn('FOAM_USER_APPBIN="$temporary_appbin" wmake', wrapper)
        self.assertIn("mktemp mkdir rm basename cmp", wrapper)
        self.assertIn("thermal_sim_semifrozen_build.", wrapper)
        self.assertIn('--print-source-sha', wrapper)
        self.assertIn('--expected-source-sha)', wrapper)
        self.assertIn('bundled solver source does not match the exported case pin', wrapper)
        self.assertIn('--expected-foam-api "$FOAM_API"', wrapper)
        self.assertIn('--evidence "$evidence_path"', wrapper)
        self.assertIn("preinstall_verify_command", wrapper)
        self.assertIn("final_verify_command", wrapper)
        self.assertIn('--expected-binary-sha256 "$built_binary_sha256"', wrapper)
        self.assertIn('cmp -s -- "$built_target" "$deployed_target"', wrapper)
        self.assertIn("restoring the previous", wrapper)
        self.assertNotIn("wsl.exe", wrapper.lower())
        self.assertNotIn("openfoam_resource_gate.ps1", wrapper)
        self.assertNotIn('$solver_dir/generated', wrapper)

    def _bash_for_wrapper_test(self):
        if os.name != "nt":
            return shutil.which("bash")
        git = shutil.which("git")
        if not git:
            return None
        candidate = Path(git).resolve().parents[1] / "bin" / "bash.exe"
        return str(candidate) if candidate.is_file() else None

    def _to_bash_path(self, bash, path):
        if os.name != "nt":
            return str(Path(path).resolve())
        completed = subprocess.run(
            [bash, "-lc", 'cygpath -u "$1"', "--", str(Path(path).resolve())],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        return completed.stdout.strip()

    def test_build_wrapper_stages_and_rolls_back_with_fake_commands(self):
        bash = self._bash_for_wrapper_test()
        if not bash:
            self.skipTest("Bash is unavailable for wrapper integration testing")
        base_path_result = subprocess.run(
            [bash, "-lc", 'printf "%s" "$PATH"'],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if base_path_result.returncode != 0:
            self.skipTest(f"Unable to query Bash PATH: {base_path_result.stderr}")
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("A native g++ is unavailable for the fake solver fixture")

        integration = self.temp_root / "wrapper integration with spaces"
        fake_bin = integration / "fake bin"
        app_bin = integration / "deployed bin"
        scratch = integration / "scratch"
        for directory in (fake_bin, app_bin, scratch):
            directory.mkdir(parents=True)

        fake_wclean = fake_bin / "wclean"
        fake_wclean.write_text(
            "#!/usr/bin/env bash\n"
            "set -euo pipefail\n"
            "rm -f -- \"$FOAM_USER_APPBIN/semiFrozenChtMultiRegionFoam\"\n",
            encoding="utf-8",
            newline="\n",
        )
        fake_python3 = fake_bin / "python3"
        fake_python3.write_text(
            "#!/usr/bin/env bash\n"
            f"exec \"{self._to_bash_path(bash, sys.executable)}\" \"$@\"\n",
            encoding="utf-8",
            newline="\n",
        )
        fake_wmake = fake_bin / "wmake"
        fake_wmake.write_text(
            "#!/usr/bin/env bash\n"
            "set -euo pipefail\n"
            "if [[ \"${FAKE_WMAKE_MODE:-}\" == build_fail ]]; then exit 33; fi\n"
            "target=\"$FOAM_USER_APPBIN/semiFrozenChtMultiRegionFoam\"\n"
            "cp -- \"$FAKE_SOLVER_BINARY\" \"$target\"\n"
            "chmod +x \"$target\"\n",
            encoding="utf-8",
            newline="\n",
        )
        fake_source = integration / "fake_solver.cpp"
        project_source_sha, _ = attestation.source_fingerprint(REPO_ROOT)
        fake_source.write_text(
            "#include <cstdlib>\n#include <cstring>\n#include <iostream>\n#include <string>\n"
            "int main(int argc, char** argv) {\n"
            " const char* mode = std::getenv(\"FAKE_WMAKE_MODE\");\n"
            " if (mode && std::string(mode) == \"preinstall_fail\") return 91;\n"
            " if (mode && std::string(mode) == \"final_fail\" && "
            "std::string(argv[0]).find(\"thermal_sim_semifrozen_build\") "
            "== std::string::npos) return 92;\n"
            " if (argc != 2 || std::strcmp(argv[1], \"--thermal-sim-attest\")) "
            "return 93;\n"
            " std::cout << \"THERMAL_SIM_SOLVER_ATTESTATION_V1\""
            " << \" solver=semiFrozenChtMultiRegionFoam\""
            f" << \" project_source_sha256={project_source_sha}\""
            " << \" policy=THERMAL_SIM_SEMIFROZEN_MODE_POLICY_V1\""
            " << \" foam_api=2606\""
            " << \" wm_project_version=v2606\""
            " << \" wm_options=linux64GccDPInt32Opt\""
            " << std::endl; return 0;\n}\n",
            encoding="utf-8",
            newline="\n",
        )
        fake_solver_binary = integration / "fake_solver_fixture.exe"
        compile_result = subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-static-libgcc",
                "-static-libstdc++",
                str(fake_source),
                "-o",
                str(fake_solver_binary),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(compile_result.returncode, 0, compile_result.stderr)

        fake_bin_bash = self._to_bash_path(bash, fake_bin)
        app_bin_bash = self._to_bash_path(bash, app_bin)
        scratch_bash = self._to_bash_path(bash, scratch)
        binary_fixture_bash = self._to_bash_path(bash, fake_solver_binary)
        wrapper_bash = self._to_bash_path(
            bash, REPO_ROOT / "tools" / "build_openfoam_semifrozen_solver.sh"
        )
        chmod_result = subprocess.run(
            [
                bash,
                "-lc",
                'chmod +x "$1" "$2" "$3"',
                "--",
                self._to_bash_path(bash, fake_wclean),
                self._to_bash_path(bash, fake_wmake),
                self._to_bash_path(bash, fake_python3),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(chmod_result.returncode, 0, chmod_result.stderr)

        old_bytes = b"previous deployed solver bytes\n"
        target = app_bin / "semiFrozenChtMultiRegionFoam"
        scenarios = (
            ("source_pin_mismatch", False, False),
            ("build_fail", False, False),
            ("preinstall_fail", False, False),
            ("final_fail", False, True),
            ("success", True, True),
        )
        for mode, should_succeed, evidence_expected in scenarios:
            with self.subTest(mode=mode):
                if target.exists():
                    target.chmod(target.stat().st_mode | 0o200)
                target.write_bytes(old_bytes)
                evidence = integration / f"{mode}.json"
                if evidence.exists():
                    evidence.unlink()
                environment = os.environ.copy()
                environment.update(
                    {
                        "PATH": fake_bin_bash + ":" + base_path_result.stdout,
                        "FOAM_API": "2606",
                        "WM_PROJECT_VERSION": "v2606",
                        "WM_OPTIONS": "linux64GccDPInt32Opt",
                        "FOAM_USER_APPBIN": app_bin_bash,
                        "TMPDIR": scratch_bash,
                        "FAKE_WMAKE_MODE": mode,
                        "FAKE_SOLVER_BINARY": binary_fixture_bash,
                    }
                )
                command = [
                        bash,
                        wrapper_bash,
                        "--evidence",
                        self._to_bash_path(bash, evidence),
                    ]
                if mode == "source_pin_mismatch":
                    command.extend(["--expected-source-sha", "0" * 64])
                completed = subprocess.run(
                    command,
                    cwd=str(REPO_ROOT),
                    env=environment,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    check=False,
                    timeout=60,
                )
                self.assertEqual(
                    completed.returncode == 0,
                    should_succeed,
                    f"rc={completed.returncode}\nstdout={completed.stdout}\n"
                    f"stderr={completed.stderr}",
                )
                self.assertEqual(evidence.exists(), evidence_expected)
                if should_succeed:
                    installed_bytes = target.read_bytes()
                    fixture_bytes = fake_solver_binary.read_bytes()
                    self.assertEqual(installed_bytes, fixture_bytes)
                    payload = json.loads(evidence.read_text())
                    self.assertEqual(payload["status"], "PASS")
                    self.assertEqual(
                        payload["attestation"]["binary_sha256"],
                        hashlib.sha256(fixture_bytes).hexdigest(),
                    )
                else:
                    self.assertEqual(target.read_bytes(), old_bytes)
                    if evidence_expected:
                        self.assertEqual(json.loads(evidence.read_text())["status"], "FAIL")


if __name__ == "__main__":
    unittest.main(verbosity=2)
