#!/usr/bin/env python3
"""Audit a transient OpenFOAM first-law energy ledger.

This tool intentionally does not infer transient energy conservation from an
outlet temperature at one instant.  It consumes a high-precision interval
ledger whose sign convention is:

    residual = heat input + other equation sources
               - outward boundary energy - solver-energy storage change

All balance terms are energies integrated over the same interval.  In
particular, ``boundary_energy_out_j`` must be accumulated at every solver time
step using the solver's temporal convention; two endpoint flux samples are not
an acceptable substitute.

Input contract (schema version 1)
---------------------------------

JSON numbers used in the calculation are encoded as decimal *strings*.  Every
non-exact quantity is an object with ``value`` and
``absolute_uncertainty`` strings.  An exact configured quantity additionally
sets ``exact`` to true and has zero uncertainty.  At least 15 significant
digits are required, every non-exact value must carry exactly the declared
precision, and every non-exact term needs an explicit positive uncertainty.

The document contains:

* ``case_id``, ``generated_at_utc``, and ``numeric_precision_digits``;
* ``interval.start_time_s`` and ``interval.end_time_s``;
* interval-integrated ``applied_heat_energy_j``, signed
  ``other_source_energy_in_j``, and ``boundary_energy_out_j``;
* one start/end sensible-enthalpy integral for every expected fluid and solid
  region, plus kinetic-energy integrals for each fluid region; and
* provenance hashes for the start/end checkpoint manifests, raw energy
  ledger, exporter metadata, and extraction definition.

Each hashed checkpoint manifest is structured and must bind the case, run UUID,
endpoint time, configuration/mesh hashes, region name/kind inventory, required
storage components, and every storage-input file's byte count and SHA-256.
The raw ledger must exactly bind the envelope and contain a monotone, gap-free
start/end/delta-t row for every solver step.  Per-step heat, nonheat-source,
and external-boundary increments must sum to their interval totals.  Explicit
external-patch, volumetric-source, nonheat equation-term, and internal-interface
inventories are required; coverage booleans alone are insufficient.  Missing,
changed, endpoint-only, stale, low-precision, or structurally incomplete
evidence is rejected before any balance is reported.

Numerical first-law closure and the thermal-development storage indicator are
deliberately separate results.  A storage-dominated early transient can pass
numerical closure.  Conversely, low whole-region storage is only a necessary
development gate: it cannot prove local temperature convergence.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from decimal import Decimal, InvalidOperation, localcontext
from pathlib import Path
from typing import Any, Mapping, Sequence


SCHEMA_VERSION = 1
MIN_SIGNIFICANT_DIGITS = 15
MAX_NUMERIC_PRECISION_DIGITS = 64
MAX_DECIMAL_TOKEN_LENGTH = 128
MAX_ABSOLUTE_DECIMAL_EXPONENT = 300
CALCULATION_PRECISION_DIGITS = 1024

ENERGY_DEFINITION = (
    "integral(rho*sensibleEnthalpy)dV+"
    "integral(fluidRho*kineticEnergy)dV"
)
BOUNDARY_ENERGY_DEFINITION = (
    "timeIntegral(allExternalOutwardEnthalpyKineticConductiveFlux)"
)
APPLIED_ENERGY_DEFINITION = "timeIntegral(allVolumetricHeatSources)"
OTHER_SOURCE_ENERGY_DEFINITION = (
    "timeIntegral(allNonHeatEnergyEquationSources)"
)
TEMPORAL_SAMPLING = "solverTimeStep"

REQUIRED_SOURCE_ROLES = {
    "start_checkpoint_manifest",
    "end_checkpoint_manifest",
    "energy_ledger",
    "exporter_metadata",
    "extraction_definition",
}

REQUIRED_NONHEAT_EQUATION_TERMS = {
    "gravity_work",
    "pressure_work_or_dpdt",
    "radiation",
    "fv_options_or_model_sources",
}
ALLOWED_BOUNDARY_FLUX_COMPONENTS = {
    "advective_sensible_enthalpy",
    "advective_kinetic_energy",
    "conductive_heat",
}

SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")


class AuditInputError(ValueError):
    """Raised when evidence is incomplete, stale, ambiguous, or insufficient."""


@dataclass(frozen=True)
class Quantity:
    value: Decimal
    absolute_uncertainty: Decimal
    exact: bool


@dataclass(frozen=True)
class RegionEnergy:
    name: str
    kind: str
    start: Quantity
    end: Quantity
    start_kinetic: Quantity | None
    end_kinetic: Quantity | None


@dataclass(frozen=True)
class VerifiedSource:
    role: str
    path: Path
    sha256: str
    declaration: Mapping[str, Any]


@dataclass(frozen=True)
class CheckpointIdentity:
    run_uuid: str
    case_configuration_sha256: str
    mesh_sha256: str


@dataclass(frozen=True)
class TransientEnergyAuditResult:
    case_id: str
    generated_at_utc: str
    run_uuid: str
    case_configuration_sha256: str
    mesh_sha256: str
    evidence_content_sha256: str
    energy_ledger_sha256: str
    solver_step_count: int
    start_time_s: Decimal
    end_time_s: Decimal
    duration_s: Decimal
    applied_heat_energy_j: Decimal
    other_source_energy_in_j: Decimal
    total_source_energy_in_j: Decimal
    boundary_energy_out_j: Decimal
    stored_energy_change_j: Decimal
    sensible_enthalpy_change_j: Decimal
    gross_sensible_enthalpy_change_j: Decimal
    kinetic_energy_change_j: Decimal
    residual_j: Decimal
    residual_uncertainty_j: Decimal
    nominal_closure_fraction: Decimal
    closure_fraction_upper_bound: Decimal
    relative_input_uncertainty: Decimal
    closure_tolerance: Decimal
    max_relative_input_uncertainty: Decimal
    numerical_closure_passed: bool
    thermal_storage_fraction_of_applied_signed: Decimal
    gross_thermal_storage_fraction_upper_bound: Decimal
    boundary_removal_fraction_of_applied: Decimal
    development_storage_fraction_limit: Decimal
    thermal_storage_gate_passed: bool
    region_count: int
    numeric_precision_digits: int

    @property
    def applied_power_w(self) -> Decimal:
        return self.applied_heat_energy_j / self.duration_s

    @property
    def other_source_power_w(self) -> Decimal:
        return self.other_source_energy_in_j / self.duration_s

    @property
    def total_source_power_w(self) -> Decimal:
        return self.total_source_energy_in_j / self.duration_s

    @property
    def boundary_energy_out_power_w(self) -> Decimal:
        return self.boundary_energy_out_j / self.duration_s

    @property
    def storage_rate_w(self) -> Decimal:
        return self.stored_energy_change_j / self.duration_s

    @property
    def residual_power_w(self) -> Decimal:
        return self.residual_j / self.duration_s

    def to_dict(self) -> dict[str, Any]:
        def text(value: Decimal) -> str:
            return format(value, f".{self.numeric_precision_digits}g")

        return {
            "schema_version": SCHEMA_VERSION,
            "case_id": self.case_id,
            "provenance": {
                "generated_at_utc": self.generated_at_utc,
                "run_uuid": self.run_uuid,
                "case_configuration_sha256": self.case_configuration_sha256,
                "mesh_sha256": self.mesh_sha256,
                "evidence_content_sha256": self.evidence_content_sha256,
                "energy_ledger_sha256": self.energy_ledger_sha256,
                "solver_step_count": self.solver_step_count,
                "hash_scope": "integrity_and_consistency_not_authenticity",
            },
            "interval": {
                "start_time_s": text(self.start_time_s),
                "end_time_s": text(self.end_time_s),
                "duration_s": text(self.duration_s),
                "region_count": self.region_count,
            },
            "numerical_first_law": {
                "status": "PASS" if self.numerical_closure_passed else "FAIL",
                "applied_heat_energy_j": text(self.applied_heat_energy_j),
                "other_source_energy_in_j": text(
                    self.other_source_energy_in_j
                ),
                "total_source_energy_in_j": text(
                    self.total_source_energy_in_j
                ),
                "boundary_energy_out_j": text(self.boundary_energy_out_j),
                "stored_energy_change_j": text(self.stored_energy_change_j),
                "sensible_enthalpy_change_j": text(
                    self.sensible_enthalpy_change_j
                ),
                "gross_sensible_enthalpy_change_j": text(
                    self.gross_sensible_enthalpy_change_j
                ),
                "kinetic_energy_change_j": text(
                    self.kinetic_energy_change_j
                ),
                "residual_j": text(self.residual_j),
                "residual_uncertainty_j": text(self.residual_uncertainty_j),
                "applied_power_w": text(self.applied_power_w),
                "other_source_power_w": text(self.other_source_power_w),
                "total_source_power_w": text(self.total_source_power_w),
                "boundary_energy_out_power_w": text(
                    self.boundary_energy_out_power_w
                ),
                "storage_rate_w": text(self.storage_rate_w),
                "residual_power_w": text(self.residual_power_w),
                "nominal_closure_fraction": text(
                    self.nominal_closure_fraction
                ),
                "closure_fraction_upper_bound": text(
                    self.closure_fraction_upper_bound
                ),
                "closure_tolerance": text(self.closure_tolerance),
            },
            "thermal_development_indicator": {
                "status": (
                    "LOW_GROSS_REGION_STORAGE"
                    if self.thermal_storage_gate_passed
                    else "SIGNIFICANT_GROSS_REGION_STORAGE"
                ),
                "thermal_storage_gate_passed": (
                    self.thermal_storage_gate_passed
                ),
                "is_proof_of_local_temperature_convergence": False,
                "thermal_storage_fraction_of_applied_signed": text(
                    self.thermal_storage_fraction_of_applied_signed
                ),
                "gross_thermal_storage_fraction_upper_bound": text(
                    self.gross_thermal_storage_fraction_upper_bound
                ),
                "boundary_removal_fraction_of_applied": text(
                    self.boundary_removal_fraction_of_applied
                ),
                "development_storage_fraction_limit": text(
                    self.development_storage_fraction_limit
                ),
            },
            "input_quality": {
                "numeric_precision_digits": self.numeric_precision_digits,
                "relative_input_uncertainty": text(
                    self.relative_input_uncertainty
                ),
                "max_relative_input_uncertainty": text(
                    self.max_relative_input_uncertainty
                ),
            },
        }


def _mapping(value: Any, context: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise AuditInputError(f"{context} must be an object")
    return value


def _sequence(value: Any, context: str) -> Sequence[Any]:
    if not isinstance(value, list):
        raise AuditInputError(f"{context} must be an array")
    return value


def _nonempty_string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise AuditInputError(f"{context} must be a non-empty string")
    return value.strip()


def _decimal_string(value: Any, context: str) -> tuple[Decimal, str]:
    if not isinstance(value, str):
        raise AuditInputError(
            f"{context} must be a decimal string, not a JSON number"
        )
    token = value.strip()
    if not token:
        raise AuditInputError(f"{context} must not be empty")
    if len(token) > MAX_DECIMAL_TOKEN_LENGTH:
        raise AuditInputError(
            f"{context} exceeds the {MAX_DECIMAL_TOKEN_LENGTH}-character limit"
        )
    try:
        parsed = Decimal(token)
    except InvalidOperation as error:
        raise AuditInputError(f"{context} is not a decimal number: {value!r}") from error
    if not parsed.is_finite():
        raise AuditInputError(f"{context} must be finite")
    exponent = parsed.as_tuple().exponent
    if (
        not isinstance(exponent, int)
        or abs(exponent) > MAX_ABSOLUTE_DECIMAL_EXPONENT
        or abs(parsed.adjusted()) > MAX_ABSOLUTE_DECIMAL_EXPONENT
    ):
        raise AuditInputError(
            f"{context} exponent must be within "
            f"+/-{MAX_ABSOLUTE_DECIMAL_EXPONENT}"
        )
    return parsed, token


def significant_digits(token: str) -> int:
    """Return the reported significant-digit count of a decimal token."""

    mantissa = token.strip().lower().split("e", 1)[0].lstrip("+-")
    digits = "".join(character for character in mantissa if character.isdigit())
    nonzero = next((index for index, digit in enumerate(digits) if digit != "0"), None)
    if nonzero is not None:
        return len(digits[nonzero:])
    if "." in mantissa:
        return max(1, len(mantissa.split(".", 1)[1]))
    return len(digits)


def _quantity(
    value: Any,
    context: str,
    *,
    declared_precision: int,
    require_nonexact: bool = False,
) -> Quantity:
    item = _mapping(value, context)
    number, number_token = _decimal_string(item.get("value"), f"{context}.value")
    uncertainty, _ = _decimal_string(
        item.get("absolute_uncertainty"),
        f"{context}.absolute_uncertainty",
    )
    exact = item.get("exact", False)
    if not isinstance(exact, bool):
        raise AuditInputError(f"{context}.exact must be true or false")
    if require_nonexact and exact:
        raise AuditInputError(f"{context} is solver-derived and cannot be exact")
    if uncertainty < 0:
        raise AuditInputError(f"{context}.absolute_uncertainty must be >= 0")
    if exact:
        if uncertainty != 0:
            raise AuditInputError(f"{context} is exact but has nonzero uncertainty")
    else:
        reported_digits = significant_digits(number_token)
        if reported_digits != declared_precision:
            raise AuditInputError(
                f"{context}.value reports {reported_digits} significant digits; "
                f"numeric_precision_digits declares {declared_precision}"
            )
        if uncertainty <= 0:
            raise AuditInputError(
                f"{context} is non-exact and requires a positive uncertainty"
            )
    return Quantity(number, uncertainty, exact)


def _utc_timestamp(value: Any) -> datetime:
    token = _nonempty_string(value, "generated_at_utc")
    if not token.endswith("Z"):
        raise AuditInputError("generated_at_utc must be an ISO-8601 UTC timestamp ending in Z")
    try:
        parsed = datetime.fromisoformat(token[:-1] + "+00:00")
    except ValueError as error:
        raise AuditInputError("generated_at_utc is not a valid ISO-8601 timestamp") from error
    if parsed.utcoffset() != timezone.utc.utcoffset(parsed):
        raise AuditInputError("generated_at_utc must be UTC")
    return parsed


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _canonical_json_sha256(value: Any) -> str:
    payload = json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _reject_duplicate_json_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise AuditInputError(f"duplicate JSON object key: {key!r}")
        result[key] = value
    return result


def _read_json_object(path: Path, context: str) -> Mapping[str, Any]:
    try:
        raw = path.read_text(encoding="utf-8")
        value = json.loads(raw, object_pairs_hook=_reject_duplicate_json_keys)
    except AuditInputError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise AuditInputError(f"cannot read {context} {path}: {error}") from error
    return _mapping(value, context)


def _within_directory(path: Path, directory: Path) -> bool:
    try:
        path.relative_to(directory)
    except ValueError:
        return False
    return True


def _verify_sources(
    provenance: Mapping[str, Any],
    source_root: Path,
    start_time: Decimal,
    end_time: Decimal,
) -> dict[str, VerifiedSource]:
    sources = _sequence(provenance.get("sources"), "provenance.sources")
    by_role: dict[str, Mapping[str, Any]] = {}
    resolved_paths: set[Path] = set()
    verified: dict[str, VerifiedSource] = {}
    for index, raw_source in enumerate(sources):
        context = f"provenance.sources[{index}]"
        source = _mapping(raw_source, context)
        role = _nonempty_string(source.get("role"), f"{context}.role")
        if role in by_role:
            raise AuditInputError(f"duplicate provenance source role: {role}")
        by_role[role] = source

        path_text = _nonempty_string(source.get("path"), f"{context}.path")
        path = Path(path_text).expanduser()
        if not path.is_absolute():
            path = source_root / path
        path = path.resolve()
        if not _within_directory(path, source_root):
            raise AuditInputError(
                f"{context}.path escapes the evidence directory: {path}"
            )
        if path in resolved_paths:
            raise AuditInputError(f"duplicate provenance source path: {path}")
        resolved_paths.add(path)
        expected_size = source.get("size_bytes")
        if isinstance(expected_size, bool) or not isinstance(expected_size, int):
            raise AuditInputError(f"{context}.size_bytes must be an integer")
        expected_hash = _nonempty_string(source.get("sha256"), f"{context}.sha256")
        if not SHA256_RE.fullmatch(expected_hash):
            raise AuditInputError(f"{context}.sha256 must contain 64 hexadecimal digits")
        try:
            if not path.is_file():
                raise AuditInputError(f"missing provenance source: {path}")
            actual_size = path.stat().st_size
            actual_hash = _sha256(path)
        except AuditInputError:
            raise
        except OSError as error:
            raise AuditInputError(
                f"cannot verify provenance source {path}: {error}"
            ) from error
        if expected_size < 0 or actual_size != expected_size:
            raise AuditInputError(
                f"stale provenance source size for {path}: expected "
                f"{expected_size}, found {actual_size}"
            )
        if actual_hash.lower() != expected_hash.lower():
            raise AuditInputError(
                f"stale provenance source hash for {path}: expected "
                f"{expected_hash.lower()}, found {actual_hash}"
            )
        verified[role] = VerifiedSource(
            role=role,
            path=path,
            sha256=actual_hash,
            declaration=source,
        )

    missing_roles = sorted(REQUIRED_SOURCE_ROLES - set(by_role))
    unknown_roles = sorted(set(by_role) - REQUIRED_SOURCE_ROLES)
    if missing_roles:
        raise AuditInputError(
            "missing provenance source role(s): " + ", ".join(missing_roles)
        )
    if unknown_roles:
        raise AuditInputError(
            "unknown provenance source role(s): " + ", ".join(unknown_roles)
        )

    for role, expected_time in (
        ("start_checkpoint_manifest", start_time),
        ("end_checkpoint_manifest", end_time),
    ):
        actual_time, _ = _decimal_string(
            by_role[role].get("time_s"), f"provenance source {role}.time_s"
        )
        if actual_time != expected_time:
            raise AuditInputError(
                f"stale {role}: time {actual_time} does not match interval "
                f"endpoint {expected_time}"
            )
    return verified


def _verified_dependency_path(
    raw_file: Any,
    *,
    context: str,
    source_root: Path,
) -> Path:
    file_entry = _mapping(raw_file, context)
    path_text = _nonempty_string(file_entry.get("path"), f"{context}.path")
    path = Path(path_text).expanduser()
    if not path.is_absolute():
        path = source_root / path
    path = path.resolve()
    if not _within_directory(path, source_root):
        raise AuditInputError(f"{context}.path escapes the evidence directory: {path}")
    size = file_entry.get("size_bytes")
    if isinstance(size, bool) or not isinstance(size, int) or size < 0:
        raise AuditInputError(f"{context}.size_bytes must be a nonnegative integer")
    expected_hash = _nonempty_string(
        file_entry.get("sha256"), f"{context}.sha256"
    ).lower()
    if not SHA256_RE.fullmatch(expected_hash):
        raise AuditInputError(f"{context}.sha256 must contain 64 hexadecimal digits")
    try:
        if not path.is_file():
            raise AuditInputError(f"missing checkpoint dependency: {path}")
        actual_size = path.stat().st_size
        actual_hash = _sha256(path)
    except AuditInputError:
        raise
    except OSError as error:
        raise AuditInputError(
            f"cannot verify checkpoint dependency {path}: {error}"
        ) from error
    if actual_size != size or actual_hash != expected_hash:
        raise AuditInputError(f"stale checkpoint dependency: {path}")
    return path


def _interval_region_kinds(interval: Mapping[str, Any]) -> dict[str, str]:
    inventory: dict[str, str] = {}
    for index, raw_region in enumerate(
        _sequence(interval.get("regions"), "interval.regions")
    ):
        context = f"interval.regions[{index}]"
        region = _mapping(raw_region, context)
        name = _nonempty_string(region.get("name"), f"{context}.name")
        kind = _nonempty_string(region.get("kind"), f"{context}.kind")
        if kind not in {"fluid", "solid"}:
            raise AuditInputError(f"{context}.kind must be 'fluid' or 'solid'")
        if name in inventory:
            raise AuditInputError(f"duplicate region energy entry: {name}")
        inventory[name] = kind
    return inventory


def _verify_checkpoint_manifests(
    sources: Mapping[str, VerifiedSource],
    *,
    source_root: Path,
    case_id: str,
    start_time: Decimal,
    end_time: Decimal,
    interval_region_kinds: Mapping[str, str],
) -> CheckpointIdentity:
    identities: list[CheckpointIdentity] = []
    for role, expected_time in (
        ("start_checkpoint_manifest", start_time),
        ("end_checkpoint_manifest", end_time),
    ):
        manifest = _read_json_object(sources[role].path, role)
        schema = manifest.get("schema_version")
        if isinstance(schema, bool) or schema != SCHEMA_VERSION:
            raise AuditInputError(
                f"{role}.schema_version must be {SCHEMA_VERSION}"
            )
        if manifest.get("case_id") != case_id:
            raise AuditInputError(f"stale {role}: case_id does not match evidence")
        manifest_time, _ = _decimal_string(
            manifest.get("time_s"), f"{role}.time_s"
        )
        if manifest_time != expected_time:
            raise AuditInputError(
                f"stale {role}: manifest time {manifest_time} does not match "
                f"interval endpoint {expected_time}"
            )
        if manifest.get("complete_for_transient_energy_audit") is not True:
            raise AuditInputError(
                f"{role}.complete_for_transient_energy_audit must be true"
            )
        run_uuid = _nonempty_string(manifest.get("run_uuid"), f"{role}.run_uuid")
        config_hash = _nonempty_string(
            manifest.get("case_configuration_sha256"),
            f"{role}.case_configuration_sha256",
        ).lower()
        mesh_hash = _nonempty_string(
            manifest.get("mesh_sha256"), f"{role}.mesh_sha256"
        ).lower()
        if not SHA256_RE.fullmatch(config_hash):
            raise AuditInputError(
                f"{role}.case_configuration_sha256 must be a SHA-256 digest"
            )
        if not SHA256_RE.fullmatch(mesh_hash):
            raise AuditInputError(f"{role}.mesh_sha256 must be a SHA-256 digest")

        manifest_inventory: dict[str, str] = {}
        dependency_count = 0
        for region_index, raw_region in enumerate(
            _sequence(manifest.get("regions"), f"{role}.regions")
        ):
            region_context = f"{role}.regions[{region_index}]"
            region = _mapping(raw_region, region_context)
            name = _nonempty_string(region.get("name"), f"{region_context}.name")
            kind = _nonempty_string(region.get("kind"), f"{region_context}.kind")
            if name in manifest_inventory:
                raise AuditInputError(f"{role} contains duplicate region {name!r}")
            manifest_inventory[name] = kind
            required_components = (
                {"sensible_enthalpy", "kinetic_energy"}
                if kind == "fluid"
                else {"sensible_enthalpy"}
                if kind == "solid"
                else set()
            )
            if not required_components:
                raise AuditInputError(
                    f"{region_context}.kind must be 'fluid' or 'solid'"
                )
            components = _mapping(
                region.get("storage_component_files"),
                f"{region_context}.storage_component_files",
            )
            if set(components) != required_components:
                raise AuditInputError(
                    f"{region_context}.storage_component_files must contain "
                    f"exactly {sorted(required_components)}"
                )
            for component in sorted(required_components):
                files = _sequence(
                    components[component],
                    f"{region_context}.storage_component_files.{component}",
                )
                if not files:
                    raise AuditInputError(
                        f"{region_context}.{component} requires at least one "
                        "hashed input file"
                    )
                for file_index, raw_file in enumerate(files):
                    _verified_dependency_path(
                        raw_file,
                        context=(
                            f"{region_context}.storage_component_files."
                            f"{component}[{file_index}]"
                        ),
                        source_root=source_root,
                    )
                    dependency_count += 1
        if manifest_inventory != dict(interval_region_kinds):
            raise AuditInputError(
                f"{role} region name/kind inventory does not match the ledger"
            )
        if dependency_count == 0:
            raise AuditInputError(f"{role} has no hashed storage dependencies")
        identities.append(CheckpointIdentity(run_uuid, config_hash, mesh_hash))

    if identities[0] != identities[1]:
        raise AuditInputError(
            "checkpoint manifests do not share the same run UUID, case "
            "configuration hash, and mesh hash"
        )
    return identities[0]


def _provenance_contract(provenance: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "energy_definition": provenance.get("energy_definition"),
        "boundary_energy_definition": provenance.get(
            "boundary_energy_definition"
        ),
        "applied_energy_definition": provenance.get(
            "applied_energy_definition"
        ),
        "other_source_energy_definition": provenance.get(
            "other_source_energy_definition"
        ),
        "temporal_sampling": provenance.get("temporal_sampling"),
        "all_external_boundaries_included": provenance.get(
            "all_external_boundaries_included"
        ),
        "all_heat_sources_included": provenance.get(
            "all_heat_sources_included"
        ),
        "all_nonheat_energy_sources_included": provenance.get(
            "all_nonheat_energy_sources_included"
        ),
        "internal_interfaces_excluded": provenance.get(
            "internal_interfaces_excluded"
        ),
        "expected_regions": provenance.get("expected_regions"),
    }


def _verify_energy_ledger(
    ledger_source: VerifiedSource,
    *,
    case_id: str,
    generated_at_utc: str,
    numeric_precision_digits: int,
    interval: Mapping[str, Any],
    provenance: Mapping[str, Any],
    sources: Mapping[str, VerifiedSource],
    checkpoint_identity: CheckpointIdentity,
) -> int:
    """Bind all audited values to the independently hashed raw ledger.

    The evidence document is a review/report envelope.  The raw ledger is the
    authoritative exporter product, so exact structural equality is required;
    merely hashing an unrelated file would otherwise leave every numeric term
    and coverage declaration self-attested.
    """

    ledger = _read_json_object(ledger_source.path, "energy ledger")
    expected_envelope = {
        "schema_version": SCHEMA_VERSION,
        "case_id": case_id,
        "generated_at_utc": generated_at_utc,
        "numeric_precision_digits": numeric_precision_digits,
        "checkpoint_identity": {
            "run_uuid": checkpoint_identity.run_uuid,
            "case_configuration_sha256": (
                checkpoint_identity.case_configuration_sha256
            ),
            "mesh_sha256": checkpoint_identity.mesh_sha256,
        },
        "interval": interval,
        "provenance_contract": _provenance_contract(provenance),
        "bound_source_sha256": {
            role: sources[role].sha256
            for role in sorted(REQUIRED_SOURCE_ROLES - {"energy_ledger"})
        },
    }
    required_ledger_keys = set(expected_envelope) | {
        "quadrature",
        "solver_step_count",
        "solver_steps",
        "restart_boundaries",
        "coverage_inventory",
    }
    if set(ledger) != required_ledger_keys or any(
        ledger.get(key) != value for key, value in expected_envelope.items()
    ):
        raise AuditInputError(
            "energy ledger does not exactly match the evidence interval, "
            "precision, provenance contract, and bound source hashes"
        )
    if ledger.get("quadrature") != "solver_discrete_time_step_increment":
        raise AuditInputError(
            "energy ledger quadrature must be solver_discrete_time_step_increment"
        )

    region_kinds = _interval_region_kinds(interval)
    _verify_coverage_inventory(
        ledger.get("coverage_inventory"), region_kinds=region_kinds
    )
    step_count = _verify_solver_steps(
        ledger,
        interval=interval,
        numeric_precision_digits=numeric_precision_digits,
    )
    return step_count


def _verify_coverage_inventory(
    raw_inventory: Any,
    *,
    region_kinds: Mapping[str, str],
) -> None:
    inventory = _mapping(raw_inventory, "energy ledger coverage_inventory")
    required_keys = {
        "external_boundaries",
        "volumetric_heat_sources",
        "nonheat_equation_terms",
        "internal_interfaces",
    }
    if set(inventory) != required_keys:
        raise AuditInputError(
            "coverage_inventory must contain exactly external_boundaries, "
            "volumetric_heat_sources, nonheat_equation_terms, and "
            "internal_interfaces"
        )

    boundaries = _sequence(
        inventory["external_boundaries"],
        "coverage_inventory.external_boundaries",
    )
    if not boundaries:
        raise AuditInputError("coverage inventory has no external boundaries")
    boundary_keys: set[tuple[str, str]] = set()
    for index, raw_boundary in enumerate(boundaries):
        context = f"coverage_inventory.external_boundaries[{index}]"
        boundary = _mapping(raw_boundary, context)
        region = _nonempty_string(boundary.get("region"), f"{context}.region")
        patch = _nonempty_string(boundary.get("patch"), f"{context}.patch")
        if region not in region_kinds:
            raise AuditInputError(f"{context} names unknown region {region!r}")
        key = (region, patch)
        if key in boundary_keys:
            raise AuditInputError(f"duplicate external boundary inventory: {key}")
        boundary_keys.add(key)
        components = {
            _nonempty_string(value, f"{context}.components[{component_index}]")
            for component_index, value in enumerate(
                _sequence(boundary.get("components"), f"{context}.components")
            )
        }
        expected_components = (
            ALLOWED_BOUNDARY_FLUX_COMPONENTS
            if region_kinds[region] == "fluid"
            else {"conductive_heat"}
        )
        if components != expected_components:
            raise AuditInputError(
                f"{context}.components must be exactly "
                f"{sorted(expected_components)}"
            )

    heat_sources = _sequence(
        inventory["volumetric_heat_sources"],
        "coverage_inventory.volumetric_heat_sources",
    )
    if not heat_sources:
        raise AuditInputError("coverage inventory has no volumetric heat sources")
    heat_ids: set[str] = set()
    for index, raw_source in enumerate(heat_sources):
        context = f"coverage_inventory.volumetric_heat_sources[{index}]"
        source = _mapping(raw_source, context)
        source_id = _nonempty_string(source.get("source_id"), f"{context}.source_id")
        region = _nonempty_string(source.get("region"), f"{context}.region")
        if source_id in heat_ids:
            raise AuditInputError(f"duplicate heat-source id: {source_id}")
        heat_ids.add(source_id)
        if region not in region_kinds:
            raise AuditInputError(f"{context} names unknown region {region!r}")
        if source.get("equation_term") != "volumetric_heat":
            raise AuditInputError(
                f"{context}.equation_term must be 'volumetric_heat'"
            )

    nonheat_terms: dict[str, str] = {}
    for index, raw_term in enumerate(
        _sequence(
            inventory["nonheat_equation_terms"],
            "coverage_inventory.nonheat_equation_terms",
        )
    ):
        context = f"coverage_inventory.nonheat_equation_terms[{index}]"
        term = _mapping(raw_term, context)
        name = _nonempty_string(term.get("name"), f"{context}.name")
        state = _nonempty_string(term.get("state"), f"{context}.state")
        if name in nonheat_terms:
            raise AuditInputError(f"duplicate nonheat equation term: {name}")
        if state not in {"included", "disabled"}:
            raise AuditInputError(f"{context}.state must be 'included' or 'disabled'")
        nonheat_terms[name] = state
    if set(nonheat_terms) != REQUIRED_NONHEAT_EQUATION_TERMS:
        raise AuditInputError(
            "nonheat equation-term inventory must cover exactly "
            + ", ".join(sorted(REQUIRED_NONHEAT_EQUATION_TERMS))
        )

    interfaces = _sequence(
        inventory["internal_interfaces"],
        "coverage_inventory.internal_interfaces",
    )
    if not interfaces:
        raise AuditInputError("coverage inventory has no internal CHT interfaces")
    interface_keys: set[tuple[str, str, str, str]] = set()
    for index, raw_interface in enumerate(interfaces):
        context = f"coverage_inventory.internal_interfaces[{index}]"
        interface = _mapping(raw_interface, context)
        region_a = _nonempty_string(
            interface.get("region_a"), f"{context}.region_a"
        )
        patch_a = _nonempty_string(interface.get("patch_a"), f"{context}.patch_a")
        region_b = _nonempty_string(
            interface.get("region_b"), f"{context}.region_b"
        )
        patch_b = _nonempty_string(interface.get("patch_b"), f"{context}.patch_b")
        if region_a not in region_kinds or region_b not in region_kinds:
            raise AuditInputError(f"{context} names an unknown region")
        if region_a == region_b:
            raise AuditInputError(f"{context} must couple two different regions")
        if interface.get("treatment") != "excluded_equal_and_opposite":
            raise AuditInputError(
                f"{context}.treatment must be 'excluded_equal_and_opposite'"
            )
        key = (region_a, patch_a, region_b, patch_b)
        reverse = (region_b, patch_b, region_a, patch_a)
        if key in interface_keys or reverse in interface_keys:
            raise AuditInputError(f"duplicate internal interface inventory: {key}")
        interface_keys.add(key)


def _verify_solver_steps(
    ledger: Mapping[str, Any],
    *,
    interval: Mapping[str, Any],
    numeric_precision_digits: int,
) -> int:
    raw_count = ledger.get("solver_step_count")
    if isinstance(raw_count, bool) or not isinstance(raw_count, int) or raw_count <= 0:
        raise AuditInputError("solver_step_count must be a positive integer")
    steps = _sequence(ledger.get("solver_steps"), "energy ledger solver_steps")
    if len(steps) != raw_count:
        raise AuditInputError("solver_step_count does not match solver_steps length")

    interval_start, _ = _decimal_string(
        interval.get("start_time_s"), "interval.start_time_s"
    )
    interval_end, _ = _decimal_string(
        interval.get("end_time_s"), "interval.end_time_s"
    )
    aggregates = {
        name: _quantity(
            interval.get(name),
            f"interval.{name}",
            declared_precision=numeric_precision_digits,
            require_nonexact=(name != "applied_heat_energy_j"),
        )
        for name in (
            "applied_heat_energy_j",
            "other_source_energy_in_j",
            "boundary_energy_out_j",
        )
    }
    summed_values = {name: Decimal(0) for name in aggregates}
    summed_uncertainties = {name: Decimal(0) for name in aggregates}
    all_exact = {name: True for name in aggregates}
    previous_end = interval_start
    endpoints: set[Decimal] = {interval_start}
    with localcontext() as context:
        context.prec = CALCULATION_PRECISION_DIGITS
        for index, raw_step in enumerate(steps):
            step_context = f"energy ledger solver_steps[{index}]"
            step = _mapping(raw_step, step_context)
            required_step_keys = {
                "index",
                "start_time_s",
                "end_time_s",
                "delta_t_s",
                *aggregates.keys(),
            }
            if set(step) != required_step_keys:
                raise AuditInputError(
                    f"{step_context} must contain exactly index, times, and "
                    "the three energy increments"
                )
            if step.get("index") != index or isinstance(step.get("index"), bool):
                raise AuditInputError(f"{step_context}.index must be {index}")
            start, _ = _decimal_string(
                step.get("start_time_s"), f"{step_context}.start_time_s"
            )
            end, _ = _decimal_string(
                step.get("end_time_s"), f"{step_context}.end_time_s"
            )
            delta, _ = _decimal_string(
                step.get("delta_t_s"), f"{step_context}.delta_t_s"
            )
            if start != previous_end:
                raise AuditInputError(
                    f"{step_context} is not gap-free: start {start} != "
                    f"previous end {previous_end}"
                )
            if end <= start or delta != end - start:
                raise AuditInputError(
                    f"{step_context}.delta_t_s must equal its positive time span"
                )
            previous_end = end
            endpoints.add(end)
            for name in aggregates:
                quantity = _quantity(
                    step.get(name),
                    f"{step_context}.{name}",
                    declared_precision=numeric_precision_digits,
                    require_nonexact=(name != "applied_heat_energy_j"),
                )
                summed_values[name] += quantity.value
                summed_uncertainties[name] += quantity.absolute_uncertainty
                all_exact[name] = all_exact[name] and quantity.exact
    if previous_end != interval_end:
        raise AuditInputError(
            f"solver-step sequence ends at {previous_end}, not {interval_end}"
        )

    restart_times: set[Decimal] = set()
    for index, raw_time in enumerate(
        _sequence(ledger.get("restart_boundaries"), "restart_boundaries")
    ):
        restart, _ = _decimal_string(raw_time, f"restart_boundaries[{index}]")
        if restart in {interval_start, interval_end} or restart not in endpoints:
            raise AuditInputError(
                f"restart boundary {restart} is not an interior solver-step endpoint"
            )
        if restart in restart_times:
            raise AuditInputError(f"duplicate restart boundary: {restart}")
        restart_times.add(restart)

    for name, aggregate in aggregates.items():
        if summed_values[name] != aggregate.value:
            raise AuditInputError(
                f"solver-step {name} increments do not sum to interval value"
            )
        if summed_uncertainties[name] > aggregate.absolute_uncertainty:
            raise AuditInputError(
                f"interval {name} uncertainty is smaller than the sum of "
                "solver-step uncertainties"
            )
        if aggregate.exact != all_exact[name]:
            raise AuditInputError(
                f"interval and solver-step exactness disagree for {name}"
            )
    return raw_count


def _positive_fraction(value: Any, context: str) -> Decimal:
    try:
        parsed = Decimal(str(value))
    except InvalidOperation as error:
        raise AuditInputError(f"{context} must be numeric") from error
    exponent = parsed.as_tuple().exponent if parsed.is_finite() else None
    if (
        not parsed.is_finite()
        or not isinstance(exponent, int)
        or abs(exponent) > MAX_ABSOLUTE_DECIMAL_EXPONENT
        or abs(parsed.adjusted()) > MAX_ABSOLUTE_DECIMAL_EXPONENT
        or parsed <= 0
        or parsed >= 1
    ):
        raise AuditInputError(f"{context} must be in (0, 1)")
    return parsed


def audit_document(
    document: Mapping[str, Any],
    source_root: Path,
    *,
    expected_case_id: str | None = None,
    expected_start_time: Decimal | str | None = None,
    expected_end_time: Decimal | str | None = None,
    closure_tolerance: Decimal | str = Decimal("0.01"),
    max_relative_input_uncertainty: Decimal | str = Decimal("0.001"),
    development_storage_fraction_limit: Decimal | str = Decimal("0.05"),
) -> TransientEnergyAuditResult:
    """Validate and audit one schema-v1 transient energy document."""

    root = _mapping(document, "document")
    evidence_content_sha256 = _canonical_json_sha256(root)
    schema_version = root.get("schema_version")
    if (
        isinstance(schema_version, bool)
        or not isinstance(schema_version, int)
        or schema_version != SCHEMA_VERSION
    ):
        raise AuditInputError(
            f"schema_version must be {SCHEMA_VERSION}"
        )
    case_id = _nonempty_string(root.get("case_id"), "case_id")
    if expected_case_id is not None and case_id != expected_case_id:
        raise AuditInputError(
            f"stale case_id: expected {expected_case_id!r}, found {case_id!r}"
        )
    generated_at_utc = _nonempty_string(
        root.get("generated_at_utc"), "generated_at_utc"
    )
    _utc_timestamp(generated_at_utc)

    precision = root.get("numeric_precision_digits")
    if isinstance(precision, bool) or not isinstance(precision, int):
        raise AuditInputError("numeric_precision_digits must be an integer")
    if precision < MIN_SIGNIFICANT_DIGITS:
        raise AuditInputError(
            f"numeric_precision_digits={precision} is too low; at least "
            f"{MIN_SIGNIFICANT_DIGITS} are required"
        )
    if precision > MAX_NUMERIC_PRECISION_DIGITS:
        raise AuditInputError(
            f"numeric_precision_digits={precision} exceeds the schema maximum "
            f"of {MAX_NUMERIC_PRECISION_DIGITS}"
        )

    interval = _mapping(root.get("interval"), "interval")
    start_time, _ = _decimal_string(
        interval.get("start_time_s"), "interval.start_time_s"
    )
    end_time, _ = _decimal_string(
        interval.get("end_time_s"), "interval.end_time_s"
    )
    if end_time <= start_time:
        raise AuditInputError("interval.end_time_s must be greater than start_time_s")
    if expected_start_time is not None and start_time != Decimal(str(expected_start_time)):
        raise AuditInputError(
            f"stale start time: expected {expected_start_time}, found {start_time}"
        )
    if expected_end_time is not None and end_time != Decimal(str(expected_end_time)):
        raise AuditInputError(
            f"stale end time: expected {expected_end_time}, found {end_time}"
        )

    provenance = _mapping(root.get("provenance"), "provenance")
    required_literals = {
        "energy_definition": ENERGY_DEFINITION,
        "boundary_energy_definition": BOUNDARY_ENERGY_DEFINITION,
        "applied_energy_definition": APPLIED_ENERGY_DEFINITION,
        "other_source_energy_definition": OTHER_SOURCE_ENERGY_DEFINITION,
        "temporal_sampling": TEMPORAL_SAMPLING,
    }
    for key, expected in required_literals.items():
        if provenance.get(key) != expected:
            raise AuditInputError(
                f"provenance.{key} must be {expected!r}; "
                f"found {provenance.get(key)!r}"
            )
    for key in (
        "all_external_boundaries_included",
        "all_heat_sources_included",
        "all_nonheat_energy_sources_included",
        "internal_interfaces_excluded",
    ):
        if provenance.get(key) is not True:
            raise AuditInputError(f"provenance.{key} must be true")

    raw_expected_regions = _sequence(
        provenance.get("expected_regions"), "provenance.expected_regions"
    )
    expected_regions = [
        _nonempty_string(value, f"provenance.expected_regions[{index}]")
        for index, value in enumerate(raw_expected_regions)
    ]
    if not expected_regions:
        raise AuditInputError("provenance.expected_regions must not be empty")
    if len(set(expected_regions)) != len(expected_regions):
        raise AuditInputError("provenance.expected_regions contains duplicates")

    interval_region_kinds = _interval_region_kinds(interval)
    verified_sources = _verify_sources(
        provenance, source_root.resolve(), start_time, end_time
    )
    checkpoint_identity = _verify_checkpoint_manifests(
        verified_sources,
        source_root=source_root.resolve(),
        case_id=case_id,
        start_time=start_time,
        end_time=end_time,
        interval_region_kinds=interval_region_kinds,
    )
    solver_step_count = _verify_energy_ledger(
        verified_sources["energy_ledger"],
        case_id=case_id,
        generated_at_utc=generated_at_utc,
        numeric_precision_digits=precision,
        interval=interval,
        provenance=provenance,
        sources=verified_sources,
        checkpoint_identity=checkpoint_identity,
    )

    applied = _quantity(
        interval.get("applied_heat_energy_j"),
        "interval.applied_heat_energy_j",
        declared_precision=precision,
    )
    if applied.value <= 0:
        raise AuditInputError("interval.applied_heat_energy_j.value must be > 0")
    other_source = _quantity(
        interval.get("other_source_energy_in_j"),
        "interval.other_source_energy_in_j",
        declared_precision=precision,
        require_nonexact=True,
    )
    boundary = _quantity(
        interval.get("boundary_energy_out_j"),
        "interval.boundary_energy_out_j",
        declared_precision=precision,
        require_nonexact=True,
    )

    raw_regions = _sequence(interval.get("regions"), "interval.regions")
    regions: list[RegionEnergy] = []
    names: set[str] = set()
    kinds: set[str] = set()
    for index, raw_region in enumerate(raw_regions):
        context = f"interval.regions[{index}]"
        region = _mapping(raw_region, context)
        name = _nonempty_string(region.get("name"), f"{context}.name")
        if name in names:
            raise AuditInputError(f"duplicate region energy entry: {name}")
        names.add(name)
        kind = _nonempty_string(region.get("kind"), f"{context}.kind")
        if kind not in {"fluid", "solid"}:
            raise AuditInputError(f"{context}.kind must be 'fluid' or 'solid'")
        kinds.add(kind)
        start = _quantity(
            region.get("start_sensible_enthalpy_j"),
            f"{context}.start_sensible_enthalpy_j",
            declared_precision=precision,
            require_nonexact=True,
        )
        end = _quantity(
            region.get("end_sensible_enthalpy_j"),
            f"{context}.end_sensible_enthalpy_j",
            declared_precision=precision,
            require_nonexact=True,
        )
        # Sensible enthalpy is reference-dependent and may legitimately be
        # negative.  Only differences formed with the same thermo definition
        # are meaningful here.
        start_kinetic: Quantity | None = None
        end_kinetic: Quantity | None = None
        if kind == "fluid":
            start_kinetic = _quantity(
                region.get("start_kinetic_energy_j"),
                f"{context}.start_kinetic_energy_j",
                declared_precision=precision,
                require_nonexact=True,
            )
            end_kinetic = _quantity(
                region.get("end_kinetic_energy_j"),
                f"{context}.end_kinetic_energy_j",
                declared_precision=precision,
                require_nonexact=True,
            )
            if start_kinetic.value < 0 or end_kinetic.value < 0:
                raise AuditInputError(f"{context} kinetic energies must be >= 0")
        elif (
            "start_kinetic_energy_j" in region
            or "end_kinetic_energy_j" in region
        ):
            raise AuditInputError(
                f"{context} is solid and must not contain kinetic energy"
            )
        regions.append(
            RegionEnergy(name, kind, start, end, start_kinetic, end_kinetic)
        )

    if names != set(expected_regions):
        missing = sorted(set(expected_regions) - names)
        unexpected = sorted(names - set(expected_regions))
        detail = []
        if missing:
            detail.append("missing: " + ", ".join(missing))
        if unexpected:
            detail.append("unexpected: " + ", ".join(unexpected))
        raise AuditInputError("region inventory mismatch (" + "; ".join(detail) + ")")
    if kinds != {"fluid", "solid"}:
        raise AuditInputError("region inventory must contain fluid and solid regions")

    closure_tol = _positive_fraction(closure_tolerance, "closure_tolerance")
    max_uncertainty = _positive_fraction(
        max_relative_input_uncertainty, "max_relative_input_uncertainty"
    )
    development_limit = _positive_fraction(
        development_storage_fraction_limit,
        "development_storage_fraction_limit",
    )

    with localcontext() as context:
        context.prec = CALCULATION_PRECISION_DIGITS
        duration = end_time - start_time
        start_enthalpy = sum((region.start.value for region in regions), Decimal(0))
        end_enthalpy = sum((region.end.value for region in regions), Decimal(0))
        sensible_enthalpy_change = end_enthalpy - start_enthalpy
        gross_sensible_enthalpy_change = sum(
            (abs(region.end.value - region.start.value) for region in regions),
            Decimal(0),
        )
        sensible_enthalpy_uncertainty = sum(
            (
                region.start.absolute_uncertainty
                + region.end.absolute_uncertainty
                for region in regions
            ),
            Decimal(0),
        )
        start_kinetic = sum(
            (
                region.start_kinetic.value
                for region in regions
                if region.start_kinetic is not None
            ),
            Decimal(0),
        )
        end_kinetic = sum(
            (
                region.end_kinetic.value
                for region in regions
                if region.end_kinetic is not None
            ),
            Decimal(0),
        )
        kinetic_energy_change = end_kinetic - start_kinetic
        kinetic_uncertainty = sum(
            (
                region.start_kinetic.absolute_uncertainty
                + region.end_kinetic.absolute_uncertainty
                for region in regions
                if region.start_kinetic is not None
                and region.end_kinetic is not None
            ),
            Decimal(0),
        )
        storage_change = sensible_enthalpy_change + kinetic_energy_change
        storage_uncertainty = (
            sensible_enthalpy_uncertainty + kinetic_uncertainty
        )
        total_source = applied.value + other_source.value
        total_source_uncertainty = (
            applied.absolute_uncertainty
            + other_source.absolute_uncertainty
        )
        residual = total_source - boundary.value - storage_change
        residual_uncertainty = (
            total_source_uncertainty
            + boundary.absolute_uncertainty
            + storage_uncertainty
        )

        # Half of the L1 throughput is invariant to cancellation between
        # independently signed terms.  For a closed balance it equals the
        # energy transferred from the positive to the negative side.
        scale_lower = (
            max(abs(applied.value) - applied.absolute_uncertainty, Decimal(0))
            + max(
                abs(other_source.value) - other_source.absolute_uncertainty,
                Decimal(0),
            )
            + max(abs(boundary.value) - boundary.absolute_uncertainty, Decimal(0))
            + max(abs(storage_change) - storage_uncertainty, Decimal(0))
        ) / Decimal(2)
        if scale_lower <= 0:
            raise AuditInputError("input uncertainty leaves no positive closure scale")
        nominal_scale = (
            abs(applied.value)
            + abs(other_source.value)
            + abs(boundary.value)
            + abs(storage_change)
        ) / Decimal(2)
        nominal_closure = abs(residual) / nominal_scale
        closure_upper = (abs(residual) + residual_uncertainty) / scale_lower
        relative_uncertainty = residual_uncertainty / scale_lower
        closure_passed = (
            closure_upper <= closure_tol
            and relative_uncertainty <= max_uncertainty
        )

        applied_lower = abs(applied.value) - applied.absolute_uncertainty
        if applied_lower <= 0:
            raise AuditInputError("applied-energy uncertainty includes zero")
        storage_fraction_signed = sensible_enthalpy_change / applied.value
        storage_fraction_upper = (
            gross_sensible_enthalpy_change + sensible_enthalpy_uncertainty
        ) / applied_lower
        boundary_fraction = boundary.value / applied.value
        developed = storage_fraction_upper <= development_limit

    return TransientEnergyAuditResult(
        case_id=case_id,
        generated_at_utc=generated_at_utc,
        run_uuid=checkpoint_identity.run_uuid,
        case_configuration_sha256=(
            checkpoint_identity.case_configuration_sha256
        ),
        mesh_sha256=checkpoint_identity.mesh_sha256,
        evidence_content_sha256=evidence_content_sha256,
        energy_ledger_sha256=verified_sources["energy_ledger"].sha256,
        solver_step_count=solver_step_count,
        start_time_s=start_time,
        end_time_s=end_time,
        duration_s=duration,
        applied_heat_energy_j=applied.value,
        other_source_energy_in_j=other_source.value,
        total_source_energy_in_j=total_source,
        boundary_energy_out_j=boundary.value,
        stored_energy_change_j=storage_change,
        sensible_enthalpy_change_j=sensible_enthalpy_change,
        gross_sensible_enthalpy_change_j=gross_sensible_enthalpy_change,
        kinetic_energy_change_j=kinetic_energy_change,
        residual_j=residual,
        residual_uncertainty_j=residual_uncertainty,
        nominal_closure_fraction=nominal_closure,
        closure_fraction_upper_bound=closure_upper,
        relative_input_uncertainty=relative_uncertainty,
        closure_tolerance=closure_tol,
        max_relative_input_uncertainty=max_uncertainty,
        numerical_closure_passed=closure_passed,
        thermal_storage_fraction_of_applied_signed=storage_fraction_signed,
        gross_thermal_storage_fraction_upper_bound=storage_fraction_upper,
        boundary_removal_fraction_of_applied=boundary_fraction,
        development_storage_fraction_limit=development_limit,
        thermal_storage_gate_passed=developed,
        region_count=len(regions),
        numeric_precision_digits=precision,
    )


def _load_evidence_document(path: Path) -> Mapping[str, Any]:
    try:
        document = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=_reject_duplicate_json_keys,
        )
    except AuditInputError:
        raise
    except FileNotFoundError as error:
        raise AuditInputError(f"missing energy evidence document: {path}") from error
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise AuditInputError(f"cannot read energy evidence document {path}: {error}") from error
    return _mapping(document, "document")


def audit_file(path: Path, **kwargs: Any) -> TransientEnergyAuditResult:
    document = _load_evidence_document(path)
    return audit_document(document, path.resolve().parent, **kwargs)


def _validate_output_paths(
    evidence_path: Path,
    document: Mapping[str, Any],
    json_path: Path | None,
    markdown_path: Path | None,
) -> None:
    evidence_path = evidence_path.resolve()
    requested = [
        (name, path.resolve())
        for name, path in (("--json", json_path), ("--markdown", markdown_path))
        if path is not None
    ]
    if len({path for _, path in requested}) != len(requested):
        raise AuditInputError("--json and --markdown must use different paths")

    protected = {evidence_path: "evidence document"}
    source_paths: dict[str, Path] = {}
    provenance = _mapping(document.get("provenance"), "provenance")
    for index, raw_source in enumerate(
        _sequence(provenance.get("sources"), "provenance.sources")
    ):
        context = f"provenance.sources[{index}]"
        source = _mapping(raw_source, context)
        path_text = _nonempty_string(source.get("path"), f"{context}.path")
        path = Path(path_text).expanduser()
        if not path.is_absolute():
            path = evidence_path.parent / path
        role = _nonempty_string(source.get("role"), f"{context}.role")
        resolved = path.resolve()
        protected[resolved] = f"provenance source {role}"
        source_paths[role] = resolved

    for role in ("start_checkpoint_manifest", "end_checkpoint_manifest"):
        manifest = _read_json_object(source_paths[role], role)
        for region_index, raw_region in enumerate(
            _sequence(manifest.get("regions"), f"{role}.regions")
        ):
            region_context = f"{role}.regions[{region_index}]"
            region = _mapping(raw_region, region_context)
            components = _mapping(
                region.get("storage_component_files"),
                f"{region_context}.storage_component_files",
            )
            for component, raw_files in components.items():
                files = _sequence(
                    raw_files,
                    f"{region_context}.storage_component_files.{component}",
                )
                for file_index, raw_file in enumerate(files):
                    file_context = (
                        f"{region_context}.storage_component_files."
                        f"{component}[{file_index}]"
                    )
                    file_entry = _mapping(raw_file, file_context)
                    path_text = _nonempty_string(
                        file_entry.get("path"), f"{file_context}.path"
                    )
                    path = Path(path_text).expanduser()
                    if not path.is_absolute():
                        path = evidence_path.parent / path
                    protected[path.resolve()] = "checkpoint storage dependency"

    for option, path in requested:
        if path in protected:
            raise AuditInputError(
                f"{option} output would overwrite the {protected[path]}: {path}"
            )


def markdown_report(result: TransientEnergyAuditResult) -> str:
    def number(value: Decimal) -> str:
        return format(value, f".{result.numeric_precision_digits}g")

    closure_status = "PASS" if result.numerical_closure_passed else "FAIL"
    storage_status = (
        "LOW GROSS REGION STORAGE"
        if result.thermal_storage_gate_passed
        else "SIGNIFICANT GROSS REGION STORAGE"
    )
    return f"""# OpenFOAM transient first-law audit

- Case: `{result.case_id}`
- Interval: {result.start_time_s} to {result.end_time_s} s
- Evidence generated: `{result.generated_at_utc}`
- Run UUID: `{result.run_uuid}`
- Solver-step increments: {result.solver_step_count}
- Configuration SHA-256: `{result.case_configuration_sha256}`
- Mesh SHA-256: `{result.mesh_sha256}`
- Evidence-content SHA-256: `{result.evidence_content_sha256}`
- Energy-ledger SHA-256: `{result.energy_ledger_sha256}`

## Numerical first-law closure: **{closure_status}**

| Term | Interval energy (J) | Mean power (W) |
| --- | ---: | ---: |
| Applied volumetric heat | {number(result.applied_heat_energy_j)} | {number(result.applied_power_w)} |
| Other signed energy-equation sources | {number(result.other_source_energy_in_j)} | {number(result.other_source_power_w)} |
| Total signed equation sources | {number(result.total_source_energy_in_j)} | {number(result.total_source_power_w)} |
| Net energy leaving external boundaries | {number(result.boundary_energy_out_j)} | {number(result.boundary_energy_out_power_w)} |
| Solver-energy storage increase | {number(result.stored_energy_change_j)} | {number(result.storage_rate_w)} |
| Of which: sensible-enthalpy increase | {number(result.sensible_enthalpy_change_j)} | {number(result.sensible_enthalpy_change_j / result.duration_s)} |
| Of which: fluid kinetic-energy increase | {number(result.kinetic_energy_change_j)} | {number(result.kinetic_energy_change_j / result.duration_s)} |
| Residual | {number(result.residual_j)} | {number(result.residual_power_w)} |

- Nominal closure error: {number(100 * result.nominal_closure_fraction)}%
- Conservative closure-error upper bound: {number(100 * result.closure_fraction_upper_bound)}%
- Closure tolerance: {number(100 * result.closure_tolerance)}%
- Relative input-uncertainty bound: {number(100 * result.relative_input_uncertainty)}%

## Thermal-development storage indicator: **{storage_status}**

- Signed sensible-enthalpy storage fraction of applied heat: {number(100 * result.thermal_storage_fraction_of_applied_signed)}%
- Gross regional sensible-storage fraction upper bound: {number(100 * result.gross_thermal_storage_fraction_upper_bound)}%
- Net boundary-removal fraction of applied energy: {number(100 * result.boundary_removal_fraction_of_applied)}%
- Low-storage gate limit: {number(100 * result.development_storage_fraction_limit)}%

This indicator is not part of the numerical closure verdict. Low gross
whole-region storage is necessary but not sufficient for thermal development:
heating and cooling can cancel within a region, and one interval can miss
oscillation or hotspot migration. Require independent multi-window local-field
or temperature convergence before design-temperature conclusions.

Hashes establish bundle integrity and cross-file consistency; they are not a
cryptographic authenticity claim unless the bundle is separately signed.
"""


def _decimal_argument(value: str) -> Decimal:
    if len(value) > MAX_DECIMAL_TOKEN_LENGTH:
        raise argparse.ArgumentTypeError(
            f"value exceeds the {MAX_DECIMAL_TOKEN_LENGTH}-character limit"
        )
    try:
        parsed = Decimal(value)
    except InvalidOperation as error:
        raise argparse.ArgumentTypeError(f"invalid decimal value: {value}") from error
    exponent = parsed.as_tuple().exponent if parsed.is_finite() else None
    if (
        not parsed.is_finite()
        or not isinstance(exponent, int)
        or abs(exponent) > MAX_ABSOLUTE_DECIMAL_EXPONENT
        or abs(parsed.adjusted()) > MAX_ABSOLUTE_DECIMAL_EXPONENT
    ):
        raise argparse.ArgumentTypeError(
            "value must be finite with a bounded decimal exponent"
        )
    return parsed


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--expected-case-id")
    parser.add_argument("--expected-start-time", type=_decimal_argument)
    parser.add_argument("--expected-end-time", type=_decimal_argument)
    parser.add_argument(
        "--closure-tolerance", type=_decimal_argument, default=Decimal("0.01")
    )
    parser.add_argument(
        "--max-relative-input-uncertainty",
        type=_decimal_argument,
        default=Decimal("0.001"),
    )
    parser.add_argument(
        "--development-storage-fraction-limit",
        "--developed-storage-fraction",
        dest="development_storage_fraction_limit",
        type=_decimal_argument,
        default=Decimal("0.05"),
    )
    parser.add_argument("--json", type=Path, help="write machine-readable results")
    parser.add_argument("--markdown", type=Path, help="write the rendered report")
    parser.add_argument(
        "--require-low-storage",
        "--require-thermally-developed",
        dest="require_low_storage",
        action="store_true",
        help=(
            "return exit status 3 when closure passes but the gross-region "
            "storage gate fails"
        ),
    )
    args = parser.parse_args(argv)

    try:
        document = _load_evidence_document(args.evidence)
        result = audit_document(
            document,
            args.evidence.resolve().parent,
            expected_case_id=args.expected_case_id,
            expected_start_time=args.expected_start_time,
            expected_end_time=args.expected_end_time,
            closure_tolerance=args.closure_tolerance,
            max_relative_input_uncertainty=args.max_relative_input_uncertainty,
            development_storage_fraction_limit=(
                args.development_storage_fraction_limit
            ),
        )
        _validate_output_paths(
            args.evidence, document, args.json, args.markdown
        )
    except AuditInputError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    rendered = markdown_report(result)
    try:
        if args.json:
            args.json.parent.mkdir(parents=True, exist_ok=True)
            args.json.write_text(
                json.dumps(result.to_dict(), indent=2) + "\n",
                encoding="utf-8",
            )
        if args.markdown:
            args.markdown.parent.mkdir(parents=True, exist_ok=True)
            args.markdown.write_text(rendered, encoding="utf-8")
    except OSError as error:
        print(f"ERROR: cannot write audit output: {error}", file=sys.stderr)
        return 2
    print(rendered)

    if not result.numerical_closure_passed:
        return 1
    if args.require_low_storage and not result.thermal_storage_gate_passed:
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
