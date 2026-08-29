"""Fail-closed release gate for provisional research-lab model inputs.

This gate does not decide whether CFD results are accurate.  It prevents an
open assumption ledger from being mistaken for an industry-ready input set.
Only rows explicitly marked ``verified`` and bound to existing evidence files
are closed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import tomllib
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path


LEDGER_SCHEMA = "thermal-sim-research-lab-assumptions-v1"
REPORT_SCHEMA_VERSION = 1
CLOSED_STATUS = "verified"
KNOWN_OPEN_STATUSES = {
    "provisional",
    "missing",
    "provisional_conservative",
    "estimated",
    "provisional_uncalibrated",
    "known_model_reduction",
}
KNOWN_STATUSES = KNOWN_OPEN_STATUSES | {CLOSED_STATUS}
ID_PATTERN = re.compile(r"^[a-z0-9]+(?:_[a-z0-9]+)*$")


class ReadinessInputError(ValueError):
    """Raised when the ledger cannot support a trustworthy verdict."""


def _sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _file_descriptor(path: Path, workspace: Path) -> dict:
    payload = path.read_bytes()
    return {
        "path": path.relative_to(workspace).as_posix(),
        "bytes": len(payload),
        "sha256": _sha256_bytes(payload),
    }


def _workspace_file(workspace: Path, value: object, label: str) -> Path:
    if not isinstance(value, str) or not value.strip():
        raise ReadinessInputError(f"{label} must be a nonempty workspace-relative path")
    supplied = Path(value)
    if supplied.is_absolute():
        raise ReadinessInputError(f"{label} must be workspace-relative")
    candidate = (workspace / supplied).resolve()
    try:
        candidate.relative_to(workspace)
    except ValueError as error:
        raise ReadinessInputError(f"{label} escapes the workspace: {value}") from error
    if not candidate.is_file():
        raise ReadinessInputError(f"{label} does not exist as a file: {value}")
    return candidate


def _required_text(row: dict, key: str, label: str) -> str:
    value = row.get(key)
    if not isinstance(value, str) or not value.strip():
        raise ReadinessInputError(f"{label}.{key} must be nonempty text")
    return value.strip()


def audit_release_readiness(ledger_path: Path, workspace_root: Path) -> dict:
    """Return a machine-readable verdict or raise on an invalid ledger."""

    workspace = workspace_root.resolve()
    ledger = ledger_path.resolve()
    try:
        ledger.relative_to(workspace)
    except ValueError as error:
        raise ReadinessInputError("assumption ledger is outside the workspace") from error
    if not ledger.is_file():
        raise ReadinessInputError(f"assumption ledger is missing: {ledger}")

    ledger_payload = ledger.read_bytes()
    try:
        document = tomllib.loads(ledger_payload.decode("utf-8-sig"))
    except (UnicodeDecodeError, tomllib.TOMLDecodeError) as error:
        raise ReadinessInputError(f"assumption ledger is not valid UTF-8 TOML: {error}") from error

    if document.get("schema") != LEDGER_SCHEMA:
        raise ReadinessInputError(
            f"unsupported assumption-ledger schema: {document.get('schema')!r}"
        )
    model_path = _workspace_file(workspace, document.get("model"), "model")
    rows = document.get("assumption")
    if not isinstance(rows, list) or not rows:
        raise ReadinessInputError("assumption ledger must contain at least one [[assumption]] row")

    seen_ids: set[str] = set()
    report_rows = []
    category_counts: Counter[str] = Counter()
    status_counts: Counter[str] = Counter()
    for index, raw_row in enumerate(rows, 1):
        if not isinstance(raw_row, dict):
            raise ReadinessInputError(f"assumption row {index} must be a table")
        label = f"assumption[{index}]"
        assumption_id = _required_text(raw_row, "id", label)
        if not ID_PATTERN.fullmatch(assumption_id):
            raise ReadinessInputError(
                f"{label}.id must use lowercase letters, digits, and single underscores"
            )
        if assumption_id in seen_ids:
            raise ReadinessInputError(f"duplicate assumption id: {assumption_id}")
        seen_ids.add(assumption_id)

        category = _required_text(raw_row, "category", label)
        status = _required_text(raw_row, "status", label)
        closure = _required_text(raw_row, "closure", label)
        if status not in KNOWN_STATUSES:
            raise ReadinessInputError(
                f"{label}.status {status!r} is not recognized by schema v1"
            )

        raw_evidence = raw_row.get("evidence", [])
        if not isinstance(raw_evidence, list) or any(
            not isinstance(item, str) or not item.strip() for item in raw_evidence
        ):
            raise ReadinessInputError(f"{label}.evidence must be a list of nonempty paths")
        evidence = [
            _file_descriptor(
                _workspace_file(workspace, item, f"{label}.evidence"), workspace
            )
            for item in raw_evidence
        ]
        if status == CLOSED_STATUS and not evidence:
            raise ReadinessInputError(
                f"{label} is verified but has no hashable evidence files"
            )

        category_counts[category] += 1
        status_counts[status] += 1
        report_rows.append(
            {
                "id": assumption_id,
                "category": category,
                "status": status,
                "closed": status == CLOSED_STATUS,
                "closure": closure,
                "evidence": evidence,
            }
        )

    open_rows = [row for row in report_rows if not row["closed"]]
    return {
        "schema_version": REPORT_SCHEMA_VERSION,
        "tool": "model_release_readiness",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "status": "PASS" if not open_rows else "FAIL",
        "release_ready": not open_rows,
        "criterion": (
            "Every assumption must have status 'verified' and at least one "
            "existing workspace evidence file bound by byte count and SHA-256."
        ),
        "ledger": {
            "path": ledger.relative_to(workspace).as_posix(),
            "bytes": len(ledger_payload),
            "sha256": _sha256_bytes(ledger_payload),
            "schema": LEDGER_SCHEMA,
        },
        "model": _file_descriptor(model_path, workspace),
        "summary": {
            "assumption_count": len(report_rows),
            "verified_count": len(report_rows) - len(open_rows),
            "open_count": len(open_rows),
            "category_counts": dict(sorted(category_counts.items())),
            "status_counts": dict(sorted(status_counts.items())),
            "open_ids": [row["id"] for row in open_rows],
        },
        "assumptions": report_rows,
        "claim_boundary": (
            "A PASS closes only the declared input-assumption ledger. It does "
            "not prove mesh/time independence, solver convergence, first-law "
            "closure, uncertainty bounds, or agreement with measurements."
        ),
    }


def _output_path(workspace: Path, supplied: Path) -> Path:
    candidate = supplied if supplied.is_absolute() else workspace / supplied
    candidate = candidate.resolve()
    try:
        candidate.relative_to(workspace)
    except ValueError as error:
        raise ReadinessInputError("output path is outside the workspace") from error
    return candidate


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Fail closed while research-lab model assumptions remain open."
    )
    parser.add_argument(
        "--ledger",
        type=Path,
        default=Path("validation/UPDATED_MODEL_ASSUMPTIONS.toml"),
    )
    parser.add_argument("--workspace", type=Path, default=Path.cwd())
    parser.add_argument(
        "--output",
        type=Path,
        help="Create a new JSON evidence file; an existing path is never overwritten.",
    )
    args = parser.parse_args(argv)

    workspace = args.workspace.resolve()
    ledger = args.ledger if args.ledger.is_absolute() else workspace / args.ledger
    try:
        report = audit_release_readiness(ledger, workspace)
        rendered = json.dumps(report, indent=2) + "\n"
        if args.output is not None:
            output = _output_path(workspace, args.output)
            output.parent.mkdir(parents=True, exist_ok=True)
            with output.open("x", encoding="utf-8", newline="\n") as destination:
                destination.write(rendered)
        sys.stdout.write(rendered)
        return 0 if report["release_ready"] else 1
    except (OSError, ReadinessInputError) as error:
        payload = {
            "schema_version": REPORT_SCHEMA_VERSION,
            "tool": "model_release_readiness",
            "status": "ERROR",
            "release_ready": False,
            "error": str(error),
        }
        sys.stderr.write(json.dumps(payload, indent=2) + "\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
