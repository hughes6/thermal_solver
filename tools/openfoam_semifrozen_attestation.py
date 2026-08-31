#!/usr/bin/env python3
"""Fail-closed source/runtime attestation for semiFrozenChtMultiRegionFoam.

The ordinary generated runner historically searched the executable for a
static policy marker.  This tool instead invokes a no-case executable
handshake and requires the repository-local project-source fingerprint,
policy, and declared OpenFOAM build identity to match explicit expectations.
It does not hash external OpenFOAM sources, shared libraries, or the compiler.
An optional negative-mode
microcase copies an existing prepared case to a temporary directory and proves
that an isothermal request is rejected before physical time advances.

This script never starts WSL or initializes an OpenFOAM environment.  Run it
inside an already initialized environment, after the host/WSL resource gate.
"""

from __future__ import annotations

import argparse
import datetime as dt
from decimal import Decimal, InvalidOperation
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Callable, Iterable, Sequence


ATTESTATION_SCHEMA = "THERMAL_SIM_SOLVER_ATTESTATION_V1"
EXPECTED_SOLVER = "semiFrozenChtMultiRegionFoam"
EXPECTED_POLICY = "THERMAL_SIM_SEMIFROZEN_MODE_POLICY_V1"
SOURCE_FINGERPRINT_ALGORITHM = "thermal-sim-repo-local-solver-source-v1"
SOURCE_INPUTS = (
    "openfoam_semifrozen_solver/Make/files",
    "openfoam_semifrozen_solver/Make/options",
    "openfoam_semifrozen_solver/semiFrozenChtMultiRegionFoam.C",
)
ATTESTATION_KEYS = (
    "solver",
    "project_source_sha256",
    "policy",
    "foam_api",
    "wm_project_version",
    "wm_options",
)
IDENTITY_VALUE_RE = re.compile(r"^[A-Za-z0-9._+\-]+$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
TIME_NAME_RE = re.compile(
    r"^[+-]?(?:(?:[0-9]+(?:\.[0-9]*)?)|(?:\.[0-9]+))(?:[eE][+-]?[0-9]+)?$"
)
TIME_ADVANCE_RE = re.compile(r"(?m)^Time\s*=\s*")
ISOTHERMAL_REJECTION = (
    "isothermalAirflow is disabled because its previous implementation "
    "advanced physical time"
)


class AttestationError(RuntimeError):
    """A fail-closed attestation or microcase failure."""


CommandRunner = Callable[
    [Sequence[str], Path, float], subprocess.CompletedProcess[str]
]


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _canonical_solver_source_bytes(data: bytes) -> bytes:
    """Make the source identity independent of Git's host line-ending mode."""

    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def source_fingerprint(repo_root: Path) -> tuple[str, list[dict[str, object]]]:
    """Hash fixed repository-local inputs using a path-bound byte stream."""

    root = repo_root.resolve()
    records: list[dict[str, object]] = []
    canonical = bytearray()
    for relative in sorted(SOURCE_INPUTS):
        path = root / Path(relative)
        if not path.is_file() or path.is_symlink():
            raise AttestationError(
                f"required regular source input is missing or a symlink: {relative}"
            )
        raw = _canonical_solver_source_bytes(path.read_bytes())
        file_sha = _sha256_bytes(raw)
        relative_bytes = relative.encode("utf-8")
        canonical.extend(relative_bytes)
        canonical.extend(b"\0")
        canonical.extend(str(len(raw)).encode("ascii"))
        canonical.extend(b"\0")
        canonical.extend(file_sha.encode("ascii"))
        canonical.extend(b"\n")
        records.append(
            {"path": relative, "bytes": len(raw), "sha256": file_sha}
        )
    return _sha256_bytes(bytes(canonical)), records


def _default_command_runner(
    command: Sequence[str], cwd: Path, timeout_seconds: float
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        list(command),
        cwd=str(cwd),
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=timeout_seconds,
    )


def _parse_runtime_attestation(
    completed: subprocess.CompletedProcess[str],
) -> dict[str, str]:
    if completed.returncode != 0:
        raise AttestationError(
            "runtime attestation exited "
            f"{completed.returncode}; stdout={completed.stdout!r}; "
            f"stderr={completed.stderr!r}"
        )
    if completed.stderr != "":
        raise AttestationError(
            f"runtime attestation emitted unexpected stderr: {completed.stderr!r}"
        )
    lines = completed.stdout.splitlines()
    if len(lines) != 1 or completed.stdout not in (lines[0], lines[0] + "\n"):
        raise AttestationError(
            "runtime attestation must emit exactly one newline-terminated or "
            f"single line; stdout={completed.stdout!r}"
        )
    tokens = lines[0].split(" ")
    if not tokens or tokens[0] != ATTESTATION_SCHEMA:
        raise AttestationError(
            f"runtime attestation schema mismatch: {lines[0]!r}"
        )
    fields: dict[str, str] = {}
    for token in tokens[1:]:
        if token.count("=") != 1:
            raise AttestationError(f"malformed attestation token: {token!r}")
        key, value = token.split("=", 1)
        if key in fields:
            raise AttestationError(f"duplicate attestation key: {key}")
        fields[key] = value
    if tuple(fields) != ATTESTATION_KEYS:
        raise AttestationError(
            "attestation keys/order mismatch: "
            f"observed={tuple(fields)!r} expected={ATTESTATION_KEYS!r}"
        )
    for key, value in fields.items():
        if not IDENTITY_VALUE_RE.fullmatch(value):
            raise AttestationError(
                f"attestation value for {key} contains unsupported bytes: {value!r}"
            )
    if not SHA256_RE.fullmatch(fields["project_source_sha256"]):
        raise AttestationError(
            "attested project_source_sha256 is not lowercase SHA-256"
        )
    return fields


def verify_runtime_attestation(
    *,
    repo_root: Path,
    binary: Path,
    expected_foam_api: str,
    expected_wm_project_version: str,
    expected_wm_options: str,
    expected_binary_sha256: str | None = None,
    timeout_seconds: float = 10.0,
    command_runner: CommandRunner = _default_command_runner,
) -> dict[str, object]:
    """Invoke and verify the no-case handshake; raise on every mismatch."""

    root = repo_root.resolve()
    supplied_binary = binary.absolute()
    if supplied_binary.is_symlink():
        raise AttestationError(f"solver binary must not be a symlink: {supplied_binary}")
    executable = supplied_binary.resolve()
    if not executable.is_file():
        raise AttestationError(
            f"solver binary is missing, not regular, or a symlink: {executable}"
        )
    if os.name != "nt" and not os.access(executable, os.X_OK):
        raise AttestationError(f"solver binary is not executable: {executable}")
    for label, value in (
        ("expected_foam_api", expected_foam_api),
        ("expected_wm_project_version", expected_wm_project_version),
        ("expected_wm_options", expected_wm_options),
    ):
        if not IDENTITY_VALUE_RE.fullmatch(value):
            raise AttestationError(f"{label} has an unsupported value: {value!r}")
    if expected_binary_sha256 is not None:
        expected_binary_sha256 = expected_binary_sha256.lower()
        if not SHA256_RE.fullmatch(expected_binary_sha256):
            raise AttestationError("expected binary SHA-256 is malformed")

    current_source_sha, source_records = source_fingerprint(root)
    binary_sha = _sha256_file(executable)
    if expected_binary_sha256 is not None and binary_sha != expected_binary_sha256:
        raise AttestationError(
            "binary SHA-256 mismatch: "
            f"observed={binary_sha} expected={expected_binary_sha256}"
        )

    try:
        completed = command_runner(
            [str(executable), "--thermal-sim-attest"], root, timeout_seconds
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise AttestationError(f"unable to execute runtime attestation: {exc}") from exc
    fields = _parse_runtime_attestation(completed)
    binary_sha_after = _sha256_file(executable)
    if binary_sha_after != binary_sha:
        raise AttestationError(
            "solver binary changed during runtime attestation: "
            f"before={binary_sha} after={binary_sha_after}"
        )
    source_sha_after, source_records_after = source_fingerprint(root)
    if source_sha_after != current_source_sha or source_records_after != source_records:
        raise AttestationError(
            "repository-local solver source changed during runtime attestation: "
            f"before={current_source_sha} after={source_sha_after}"
        )
    expected = {
        "solver": EXPECTED_SOLVER,
        "project_source_sha256": current_source_sha,
        "policy": EXPECTED_POLICY,
        "foam_api": expected_foam_api,
        "wm_project_version": expected_wm_project_version,
        "wm_options": expected_wm_options,
    }
    mismatches = [
        f"{key}: observed={fields[key]!r} expected={value!r}"
        for key, value in expected.items()
        if fields[key] != value
    ]
    if mismatches:
        raise AttestationError(
            "runtime attestation does not match current project source/build identity: "
            + "; ".join(mismatches)
        )
    return {
        "status": "PASS",
        "schema": ATTESTATION_SCHEMA,
        "project_source_fingerprint_algorithm": SOURCE_FINGERPRINT_ALGORITHM,
        "project_source_sha256": current_source_sha,
        "project_source_inputs": source_records,
        "binary": str(executable),
        "binary_bytes": executable.stat().st_size,
        "binary_sha256": binary_sha,
        "runtime_fields": fields,
        "runtime_stdout_sha256": _sha256_bytes(completed.stdout.encode("utf-8")),
        "runtime_stderr_sha256": _sha256_bytes(completed.stderr.encode("utf-8")),
        "case_accessed": False,
    }


def _time_value(name: str) -> Decimal | None:
    if not TIME_NAME_RE.fullmatch(name):
        return None
    try:
        value = Decimal(name)
    except InvalidOperation:
        return None
    return value if value.is_finite() else None


def _tree_fingerprint(root: Path) -> tuple[str, list[str]]:
    records = bytearray()
    time_names: list[str] = []
    for path in sorted(root.rglob("*"), key=lambda item: item.as_posix()):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            raise AttestationError(f"microcase template contains a symlink: {relative}")
        if path.is_dir():
            records.extend(f"D\0{relative}\n".encode("utf-8"))
            if path.parent == root and _time_value(path.name) is not None:
                time_names.append(path.name)
        elif path.is_file():
            records.extend(
                f"F\0{relative}\0{path.stat().st_size}\0{_sha256_file(path)}\n".encode(
                    "utf-8"
                )
            )
    return _sha256_bytes(bytes(records)), sorted(
        time_names, key=lambda name: (_time_value(name), name)
    )


def _replace_dictionary_scalar(text: str, key: str, value: str) -> str:
    pattern = re.compile(rf"(?m)^(\s*{re.escape(key)}\s+)[^;\r\n]+(\s*;\s*)$")
    replaced, count = pattern.subn(rf"\g<1>{value}\g<2>", text)
    if count != 1:
        raise AttestationError(
            f"microcase expected exactly one {key} dictionary entry, found {count}"
        )
    return replaced


def run_negative_isothermal_microcase(
    *,
    binary: Path,
    template: Path,
    timeout_seconds: float = 30.0,
    scratch_root: Path | None = None,
    command_runner: CommandRunner = _default_command_runner,
) -> dict[str, object]:
    """Prove runtime rejection without advancing or changing the template."""

    supplied_binary = binary.absolute()
    if supplied_binary.is_symlink():
        raise AttestationError(f"solver binary must not be a symlink: {supplied_binary}")
    executable = supplied_binary.resolve()
    if not executable.is_file():
        raise AttestationError(f"solver binary is not a regular file: {executable}")
    binary_sha_before = _sha256_file(executable)
    supplied_template = template.absolute()
    if supplied_template.is_symlink():
        raise AttestationError(
            f"microcase template root must not be a symlink: {supplied_template}"
        )
    template_root = supplied_template.resolve()
    required = (
        template_root / "0",
        template_root / "constant" / "regionProperties",
        template_root / "system" / "controlDict",
        template_root / "system" / "fluid" / "fvSolution",
    )
    if not template_root.is_dir() or not all(path.exists() for path in required):
        raise AttestationError(
            f"microcase template is not a prepared multi-region case: {template_root}"
        )
    template_sha_before, template_times_before = _tree_fingerprint(template_root)
    if template_times_before != ["0"]:
        raise AttestationError(
            "negative microcase template must contain exactly time 0; "
            f"observed={template_times_before!r}"
        )

    scratch = scratch_root.resolve() if scratch_root else None
    if scratch is not None and not scratch.is_dir():
        raise AttestationError(f"scratch root does not exist: {scratch}")
    with tempfile.TemporaryDirectory(
        prefix="thermal_sim_solver_microcase_", dir=str(scratch) if scratch else None
    ) as temporary:
        case = Path(temporary) / "case"
        shutil.copytree(template_root, case)

        control_path = case / "system" / "controlDict"
        control_text = control_path.read_text(encoding="utf-8")
        control_text = _replace_dictionary_scalar(control_text, "startFrom", "startTime")
        control_text = _replace_dictionary_scalar(control_text, "startTime", "0")
        control_text = _replace_dictionary_scalar(control_text, "stopAt", "endTime")
        control_text = _replace_dictionary_scalar(control_text, "endTime", "0")
        control_path.write_text(control_text, encoding="utf-8", newline="\n")

        solution_path = case / "system" / "fluid" / "fvSolution"
        solution_text = solution_path.read_text(encoding="utf-8")
        if re.search(r"(?m)^\s*isothermalAirflow\s+", solution_text):
            raise AttestationError(
                "microcase template unexpectedly already declares isothermalAirflow"
            )
        solution_text = _replace_dictionary_scalar(
            solution_text, "thermalOnlyFlow", "false"
        )
        insertion = re.sub(
            r"(?m)^(\s*thermalOnlyFlow\s+false\s*;\s*)$",
            r"\1\n isothermalAirflow true;",
            solution_text,
            count=1,
        )
        if insertion == solution_text:
            raise AttestationError("unable to inject isothermalAirflow microcase flag")
        solution_path.write_text(insertion, encoding="utf-8", newline="\n")

        injected_case_sha_before, injected_times_before = _tree_fingerprint(case)
        try:
            completed = command_runner(
                [str(executable), "-case", str(case)],
                case,
                timeout_seconds,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise AttestationError(f"negative runtime microcase could not run: {exc}") from exc
        combined = completed.stdout + "\n" + completed.stderr
        binary_sha_after = _sha256_file(executable)
        injected_case_sha_after, copied_times_after = _tree_fingerprint(case)
        failures: list[str] = []
        if completed.returncode == 0:
            failures.append("solver accepted the forbidden isothermal mode")
        if ISOTHERMAL_REJECTION not in combined:
            failures.append("expected fail-closed isothermal diagnostic is absent")
        if TIME_ADVANCE_RE.search(combined):
            failures.append("solver log contains a physical Time = advancement")
        if binary_sha_after != binary_sha_before:
            failures.append(
                "solver binary changed during the negative runtime microcase"
            )
        if copied_times_after != ["0"]:
            failures.append(
                f"solver created or removed numeric time directories: {copied_times_after!r}"
            )
        if injected_case_sha_after != injected_case_sha_before:
            failures.append(
                "solver changed the copied zero-step case before rejecting the mode"
            )
        template_sha_after, template_times_after = _tree_fingerprint(template_root)
        if (
            template_sha_after != template_sha_before
            or template_times_after != template_times_before
        ):
            failures.append("source microcase template changed")
        if failures:
            raise AttestationError("; ".join(failures))
        return {
            "status": "PASS",
            "kind": "negative_isothermal_zero_step",
            "template": str(template_root),
            "template_tree_sha256": template_sha_before,
            "template_numeric_times": template_times_before,
            "solver_exit_code": completed.returncode,
            "binary_sha256": binary_sha_before,
            "expected_diagnostic": ISOTHERMAL_REJECTION,
            "physical_time_advance_lines": 0,
            "copied_case_numeric_times_after": copied_times_after,
            "copied_case_tree_sha256_before": injected_case_sha_before,
            "copied_case_tree_sha256_after": injected_case_sha_after,
            "copied_case_unchanged": True,
            "template_unchanged": True,
            "stdout_sha256": _sha256_bytes(completed.stdout.encode("utf-8")),
            "stderr_sha256": _sha256_bytes(completed.stderr.encode("utf-8")),
            "stdout": completed.stdout,
            "stderr": completed.stderr,
        }


def _write_json_exclusive(path: Path, payload: dict[str, object]) -> None:
    destination = path.resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    encoded = (json.dumps(payload, indent=2, sort_keys=True) + "\n").encode("utf-8")
    descriptor = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
    except BaseException:
        try:
            destination.unlink()
        except OSError:
            pass
        raise


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="repository root containing the fixed solver source inputs",
    )
    parser.add_argument(
        "--print-source-sha",
        action="store_true",
        help="print the canonical source fingerprint and perform no execution",
    )
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--expected-foam-api")
    parser.add_argument("--expected-wm-project-version")
    parser.add_argument("--expected-wm-options")
    parser.add_argument("--expected-binary-sha256")
    parser.add_argument("--timeout-seconds", type=float, default=10.0)
    parser.add_argument(
        "--negative-mode-case",
        type=Path,
        help="prepared time-zero case to copy for a forbidden-mode runtime microcase",
    )
    parser.add_argument("--microcase-timeout-seconds", type=float, default=30.0)
    parser.add_argument("--scratch-root", type=Path)
    parser.add_argument(
        "--evidence",
        type=Path,
        help="create, never overwrite, a structured JSON result",
    )
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    args = _parser().parse_args(list(argv) if argv is not None else None)
    if args.print_source_sha:
        if any(
            value is not None
            for value in (
                args.binary,
                args.expected_foam_api,
                args.expected_wm_project_version,
                args.expected_wm_options,
                args.expected_binary_sha256,
                args.negative_mode_case,
                args.evidence,
            )
        ):
            _parser().error("--print-source-sha cannot be combined with runtime options")
        digest, _ = source_fingerprint(args.repo_root)
        print(digest)
        return 0

    required = {
        "--binary": args.binary,
        "--expected-foam-api": args.expected_foam_api,
        "--expected-wm-project-version": args.expected_wm_project_version,
        "--expected-wm-options": args.expected_wm_options,
        "--evidence": args.evidence,
    }
    missing = [name for name, value in required.items() if value is None]
    if missing:
        _parser().error("runtime attestation requires " + ", ".join(missing))
    if args.timeout_seconds <= 0 or args.microcase_timeout_seconds <= 0:
        _parser().error("timeouts must be positive")

    result: dict[str, object] = {
        "status": "FAIL",
        "schema": ATTESTATION_SCHEMA,
        "generated_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "expected": {
            "solver": EXPECTED_SOLVER,
            "policy": EXPECTED_POLICY,
            "foam_api": args.expected_foam_api,
            "wm_project_version": args.expected_wm_project_version,
            "wm_options": args.expected_wm_options,
            "binary_sha256": args.expected_binary_sha256,
        },
        "observed_preflight": {},
        "error": None,
    }
    exit_code = 3
    try:
        preflight_source_sha, preflight_source_inputs = source_fingerprint(
            args.repo_root
        )
        result["observed_preflight"] = {
            "project_source_fingerprint_algorithm": SOURCE_FINGERPRINT_ALGORITHM,
            "project_source_sha256": preflight_source_sha,
            "project_source_inputs": preflight_source_inputs,
            "binary_supplied_path": str(args.binary.absolute()),
            "binary_supplied_path_is_symlink": args.binary.absolute().is_symlink(),
        }
        if args.binary.absolute().is_file() and not args.binary.absolute().is_symlink():
            result["observed_preflight"].update(
                {
                    "binary_bytes": args.binary.absolute().stat().st_size,
                    "binary_sha256": _sha256_file(args.binary.absolute()),
                }
            )
        attestation = verify_runtime_attestation(
            repo_root=args.repo_root,
            binary=args.binary,
            expected_foam_api=args.expected_foam_api,
            expected_wm_project_version=args.expected_wm_project_version,
            expected_wm_options=args.expected_wm_options,
            expected_binary_sha256=args.expected_binary_sha256,
            timeout_seconds=args.timeout_seconds,
        )
        result["attestation"] = attestation
        if args.negative_mode_case is not None:
            result["microcase"] = run_negative_isothermal_microcase(
                binary=args.binary,
                template=args.negative_mode_case,
                timeout_seconds=args.microcase_timeout_seconds,
                scratch_root=args.scratch_root,
            )
        result["status"] = "PASS"
        result["error"] = None
        exit_code = 0
    except (AttestationError, OSError) as exc:
        result["error"] = str(exc)

    try:
        _write_json_exclusive(args.evidence, result)
    except FileExistsError:
        print(f"ERROR: evidence path already exists: {args.evidence}", file=sys.stderr)
        return 4
    except OSError as exc:
        print(f"ERROR: unable to write evidence: {exc}", file=sys.stderr)
        return 4
    print(json.dumps(result, sort_keys=True))
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
