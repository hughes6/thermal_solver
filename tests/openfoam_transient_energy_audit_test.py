import hashlib
import io
import json
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from decimal import Decimal
from pathlib import Path

from tools.openfoam_transient_energy_audit import (
    APPLIED_ENERGY_DEFINITION,
    BOUNDARY_ENERGY_DEFINITION,
    ENERGY_DEFINITION,
    OTHER_SOURCE_ENERGY_DEFINITION,
    TEMPORAL_SAMPLING,
    AuditInputError,
    audit_document,
    audit_file,
    main,
    markdown_report,
    significant_digits,
)


RUN_UUID = "2cbbfdc5-46b6-4619-91fc-5488d36fca6a"
CONFIGURATION_SHA256 = hashlib.sha256(b"synthetic configuration").hexdigest()
MESH_SHA256 = hashlib.sha256(b"synthetic mesh").hexdigest()


def _quantity(value: str, uncertainty: str = "0.0000000000010000") -> dict:
    return {
        "value": value,
        "absolute_uncertainty": uncertainty,
        "exact": False,
    }


def _exact(value: str) -> dict:
    return {"value": value, "absolute_uncertainty": "0", "exact": True}


def _source(path: Path, role: str, time_s: str | None = None) -> dict:
    payload = path.read_bytes()
    result = {
        "role": role,
        "path": path.name,
        "size_bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
    }
    if time_s is not None:
        result["time_s"] = time_s
    return result


def _file_descriptor(path: Path) -> dict:
    payload = path.read_bytes()
    return {
        "path": path.name,
        "size_bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
    }


def _checkpoint_manifest(root: Path, stem: str, time_s: str) -> Path:
    regions = []
    for name, kind, components in (
        ("fluid", "fluid", ("sensible_enthalpy", "kinetic_energy")),
        ("solid_a", "solid", ("sensible_enthalpy",)),
    ):
        component_files = {}
        for component in components:
            dependency = root / f"{stem}-{name}-{component}.dat"
            dependency.write_text(
                f"{stem} {time_s} {name} {component}\n", encoding="utf-8"
            )
            component_files[component] = [_file_descriptor(dependency)]
        regions.append(
            {
                "name": name,
                "kind": kind,
                "storage_component_files": component_files,
            }
        )
    manifest = {
        "schema_version": 1,
        "case_id": "synthetic-cht-case",
        "run_uuid": RUN_UUID,
        "time_s": time_s,
        "case_configuration_sha256": CONFIGURATION_SHA256,
        "mesh_sha256": MESH_SHA256,
        "complete_for_transient_energy_audit": True,
        "regions": regions,
    }
    path = root / f"{stem}.manifest.json"
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return path


def _ledger_payload(evidence: dict) -> dict:
    provenance = evidence["provenance"]
    interval = evidence["interval"]
    contract_keys = (
        "energy_definition",
        "boundary_energy_definition",
        "applied_energy_definition",
        "other_source_energy_definition",
        "temporal_sampling",
        "all_external_boundaries_included",
        "all_heat_sources_included",
        "all_nonheat_energy_sources_included",
        "internal_interfaces_excluded",
        "expected_regions",
    )
    return {
        "schema_version": 1,
        "case_id": evidence["case_id"],
        "generated_at_utc": evidence["generated_at_utc"],
        "numeric_precision_digits": evidence["numeric_precision_digits"],
        "checkpoint_identity": {
            "run_uuid": RUN_UUID,
            "case_configuration_sha256": CONFIGURATION_SHA256,
            "mesh_sha256": MESH_SHA256,
        },
        "interval": interval,
        "provenance_contract": {
            key: provenance[key] for key in contract_keys
        },
        "bound_source_sha256": {
            source["role"]: source["sha256"]
            for source in sorted(
                provenance["sources"], key=lambda item: item["role"]
            )
            if source["role"] != "energy_ledger"
        },
        "quadrature": "solver_discrete_time_step_increment",
        "solver_step_count": 1,
        "solver_steps": [
            {
                "index": 0,
                "start_time_s": interval["start_time_s"],
                "end_time_s": interval["end_time_s"],
                "delta_t_s": format(
                    Decimal(interval["end_time_s"])
                    - Decimal(interval["start_time_s"]),
                    "f",
                ),
                "applied_heat_energy_j": interval["applied_heat_energy_j"],
                "other_source_energy_in_j": interval[
                    "other_source_energy_in_j"
                ],
                "boundary_energy_out_j": interval["boundary_energy_out_j"],
            }
        ],
        "restart_boundaries": [],
        "coverage_inventory": {
            "external_boundaries": [
                {
                    "region": "fluid",
                    "patch": "outlet",
                    "components": [
                        "advective_sensible_enthalpy",
                        "advective_kinetic_energy",
                        "conductive_heat",
                    ],
                },
                {
                    "region": "solid_a",
                    "patch": "ambient",
                    "components": ["conductive_heat"],
                },
            ],
            "volumetric_heat_sources": [
                {
                    "source_id": "solid_a_heat",
                    "region": "solid_a",
                    "equation_term": "volumetric_heat",
                }
            ],
            "nonheat_equation_terms": [
                {"name": "gravity_work", "state": "included"},
                {"name": "pressure_work_or_dpdt", "state": "disabled"},
                {"name": "radiation", "state": "disabled"},
                {
                    "name": "fv_options_or_model_sources",
                    "state": "disabled",
                },
            ],
            "internal_interfaces": [
                {
                    "region_a": "fluid",
                    "patch_a": "fluid_to_solid",
                    "region_b": "solid_a",
                    "patch_b": "solid_to_fluid",
                    "treatment": "excluded_equal_and_opposite",
                }
            ],
        },
    }


def _sync_ledger(evidence: dict, root: Path) -> None:
    sources = evidence["provenance"]["sources"]
    sources[:] = [source for source in sources if source["role"] != "energy_ledger"]
    path = root / "openfoam-energy-ledger.json"
    path.write_text(
        json.dumps(_ledger_payload(evidence), indent=2) + "\n",
        encoding="utf-8",
    )
    sources.append(_source(path, "energy_ledger"))


def _mutate_raw_ledger(evidence: dict, root: Path, mutation) -> None:
    path = root / "openfoam-energy-ledger.json"
    ledger = json.loads(path.read_text(encoding="utf-8"))
    mutation(ledger)
    path.write_text(json.dumps(ledger, indent=2) + "\n", encoding="utf-8")
    sources = evidence["provenance"]["sources"]
    for index, source in enumerate(sources):
        if source["role"] == "energy_ledger":
            sources[index] = _source(path, "energy_ledger")
            break


def _evidence(root: Path) -> dict:
    sources = []
    start_manifest = _checkpoint_manifest(root, "start", "1.5")
    end_manifest = _checkpoint_manifest(root, "end", "1.6")
    sources.append(_source(start_manifest, "start_checkpoint_manifest", "1.5"))
    sources.append(_source(end_manifest, "end_checkpoint_manifest", "1.6"))
    for filename, role in (
        ("openfoamExportProperties", "exporter_metadata"),
        ("energy-extraction.dict", "extraction_definition"),
    ):
        path = root / filename
        path.write_text(f"{role}\n", encoding="utf-8")
        sources.append(_source(path, role))

    evidence = {
        "schema_version": 1,
        "case_id": "synthetic-cht-case",
        "generated_at_utc": "2026-08-26T12:00:00Z",
        "numeric_precision_digits": 17,
        "interval": {
            "start_time_s": "1.5",
            "end_time_s": "1.6",
            "applied_heat_energy_j": _exact("100"),
            "other_source_energy_in_j": _quantity(
                "0.00000000000000000"
            ),
            "boundary_energy_out_j": _quantity("20.000000000000000"),
            "regions": [
                {
                    "name": "fluid",
                    "kind": "fluid",
                    "start_sensible_enthalpy_j": _quantity("1000.0000000000000"),
                    "end_sensible_enthalpy_j": _quantity("1010.0000000000000"),
                    "start_kinetic_energy_j": _quantity("10.000000000000000"),
                    "end_kinetic_energy_j": _quantity("10.000000000000000"),
                },
                {
                    "name": "solid_a",
                    "kind": "solid",
                    "start_sensible_enthalpy_j": _quantity("2000.0000000000000"),
                    "end_sensible_enthalpy_j": _quantity("2070.0000000000000"),
                },
            ],
        },
        "provenance": {
            "energy_definition": ENERGY_DEFINITION,
            "boundary_energy_definition": BOUNDARY_ENERGY_DEFINITION,
            "applied_energy_definition": APPLIED_ENERGY_DEFINITION,
            "other_source_energy_definition": OTHER_SOURCE_ENERGY_DEFINITION,
            "temporal_sampling": TEMPORAL_SAMPLING,
            "all_external_boundaries_included": True,
            "all_heat_sources_included": True,
            "all_nonheat_energy_sources_included": True,
            "internal_interfaces_excluded": True,
            "expected_regions": ["fluid", "solid_a"],
            "sources": sources,
        },
    }
    _sync_ledger(evidence, root)
    return evidence


class OpenFoamTransientEnergyAuditTest(unittest.TestCase):
    def test_numerical_closure_passes_while_transient_is_still_developing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = audit_document(_evidence(root), root)

        self.assertTrue(result.numerical_closure_passed)
        self.assertFalse(result.thermal_storage_gate_passed)
        self.assertEqual(result.stored_energy_change_j, Decimal("80"))
        self.assertEqual(result.residual_j, Decimal("0"))
        self.assertEqual(result.thermal_storage_fraction_of_applied_signed, Decimal("0.8"))
        self.assertEqual(result.boundary_removal_fraction_of_applied, Decimal("0.2"))
        report = markdown_report(result)
        self.assertIn("Numerical first-law closure: **PASS**", report)
        self.assertIn(
            "Thermal-development storage indicator: **SIGNIFICANT GROSS REGION STORAGE**",
            report,
        )
        self.assertIn("not part of the numerical closure verdict", report)

    def test_low_storage_gate_is_independent_and_can_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = _evidence(root)
            evidence["interval"]["boundary_energy_out_j"] = _quantity(
                "99.000000000000000"
            )
            evidence["interval"]["regions"][0]["end_sensible_enthalpy_j"] = _quantity(
                "1001.0000000000000"
            )
            evidence["interval"]["regions"][1]["end_sensible_enthalpy_j"] = _quantity(
                "2000.0000000000000"
            )
            _sync_ledger(evidence, root)
            result = audit_document(evidence, root)

        self.assertTrue(result.numerical_closure_passed)
        self.assertTrue(result.thermal_storage_gate_passed)
        self.assertEqual(
            result.gross_thermal_storage_fraction_upper_bound.quantize(
                Decimal("0.001")
            ),
            Decimal("0.010"),
        )

    def test_nonclosing_balance_fails_numerical_check(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = _evidence(root)
            evidence["interval"]["boundary_energy_out_j"] = _quantity(
                "5.0000000000000000"
            )
            _sync_ledger(evidence, root)
            result = audit_document(evidence, root)

        self.assertFalse(result.numerical_closure_passed)
        self.assertEqual(result.residual_j, Decimal("15"))
        self.assertGreater(result.closure_fraction_upper_bound, Decimal("0.14"))

    def test_kinetic_storage_and_nonheat_sources_enter_only_full_closure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = _evidence(root)
            evidence["interval"]["other_source_energy_in_j"] = _quantity(
                "10.000000000000000"
            )
            evidence["interval"]["regions"][0]["end_kinetic_energy_j"] = _quantity(
                "20.000000000000000"
            )
            _sync_ledger(evidence, root)
            result = audit_document(evidence, root)

        self.assertTrue(result.numerical_closure_passed)
        self.assertEqual(result.total_source_energy_in_j, Decimal("110"))
        self.assertEqual(result.sensible_enthalpy_change_j, Decimal("80"))
        self.assertEqual(result.kinetic_energy_change_j, Decimal("10"))
        self.assertEqual(result.stored_energy_change_j, Decimal("90"))
        self.assertEqual(
            result.thermal_storage_fraction_of_applied_signed,
            Decimal("0.8"),
        )

    def test_fluid_kinetic_storage_is_mandatory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = _evidence(root)
            del evidence["interval"]["regions"][0]["end_kinetic_energy_j"]
            _sync_ledger(evidence, root)
            with self.assertRaisesRegex(AuditInputError, "end_kinetic_energy_j"):
                audit_document(evidence, root)

    def test_reference_relative_sensible_enthalpy_may_be_negative(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = _evidence(root)
            fluid, solid = evidence["interval"]["regions"]
            fluid["start_sensible_enthalpy_j"] = _quantity(
                "-1000.0000000000000"
            )
            fluid["end_sensible_enthalpy_j"] = _quantity(
                "-990.00000000000000"
            )
            solid["start_sensible_enthalpy_j"] = _quantity(
                "-2000.0000000000000"
            )
            solid["end_sensible_enthalpy_j"] = _quantity(
                "-1930.0000000000000"
            )
            _sync_ledger(evidence, root)
            result = audit_document(evidence, root)

        self.assertTrue(result.numerical_closure_passed)
        self.assertEqual(result.sensible_enthalpy_change_j, Decimal("80"))

    def test_low_declared_or_actual_precision_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            low_declared = _evidence(root)
            low_declared["numeric_precision_digits"] = 6
            _sync_ledger(low_declared, root)
            with self.assertRaisesRegex(AuditInputError, "too low"):
                audit_document(low_declared, root)

            low_actual = _evidence(root)
            low_actual["interval"]["boundary_energy_out_j"] = _quantity(
                "8.92700"
            )
            _sync_ledger(low_actual, root)
            with self.assertRaisesRegex(AuditInputError, "significant digits"):
                audit_document(low_actual, root)

            excessive_precision = _evidence(root)
            excessive_precision["numeric_precision_digits"] = 10**100
            _sync_ledger(excessive_precision, root)
            with self.assertRaisesRegex(AuditInputError, "schema maximum"):
                audit_document(excessive_precision, root)

            excessive_exponent = _evidence(root)
            excessive_exponent["interval"]["applied_heat_energy_j"] = _exact(
                "1e1000000"
            )
            _sync_ledger(excessive_exponent, root)
            with self.assertRaisesRegex(AuditInputError, "exponent"):
                audit_document(excessive_exponent, root)

    def test_missing_and_changed_sources_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            missing = _evidence(root)
            (root / "openfoam-energy-ledger.json").unlink()
            with self.assertRaisesRegex(AuditInputError, "missing provenance source"):
                audit_document(missing, root)

            stale = _evidence(root)
            (root / "openfoam-energy-ledger.json").write_text(
                "changed\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(AuditInputError, "stale provenance source"):
                audit_document(stale, root)

    def test_stale_case_and_checkpoint_times_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = _evidence(root)
            with self.assertRaisesRegex(AuditInputError, "stale case_id"):
                audit_document(evidence, root, expected_case_id="another-case")
            with self.assertRaisesRegex(AuditInputError, "stale end time"):
                audit_document(evidence, root, expected_end_time="1.7")

            evidence["provenance"]["sources"][1]["time_s"] = "1.7"
            with self.assertRaisesRegex(AuditInputError, "stale end_checkpoint_manifest"):
                audit_document(evidence, root)

            embedded_time = _evidence(root)
            embedded_time["interval"]["start_time_s"] = "1.4"
            embedded_time["provenance"]["sources"][0]["time_s"] = "1.4"
            _sync_ledger(embedded_time, root)
            with self.assertRaisesRegex(
                AuditInputError, "manifest time 1.5 does not match"
            ):
                audit_document(embedded_time, root)

    def test_endpoint_only_flux_sampling_and_incomplete_regions_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            endpoint_only = _evidence(root)
            endpoint_only["provenance"]["temporal_sampling"] = "endpointsOnly"
            with self.assertRaisesRegex(AuditInputError, "temporal_sampling"):
                audit_document(endpoint_only, root)

            incomplete = _evidence(root)
            incomplete["interval"]["regions"].pop()
            _sync_ledger(incomplete, root)
            with self.assertRaisesRegex(AuditInputError, "region.*inventory"):
                audit_document(incomplete, root)

    def test_high_uncertainty_cannot_pass_a_nominally_closed_balance(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = _evidence(root)
            evidence["interval"]["boundary_energy_out_j"] = _quantity(
                "20.000000000000000", "0.20000000000000000"
            )
            _sync_ledger(evidence, root)
            result = audit_document(evidence, root)

        self.assertEqual(result.residual_j, Decimal("0"))
        self.assertGreater(result.relative_input_uncertainty, Decimal("0.001"))
        self.assertFalse(result.numerical_closure_passed)

    def test_opposing_sources_do_not_collapse_the_closure_scale(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = _evidence(root)
            interval = evidence["interval"]
            interval["other_source_energy_in_j"] = _quantity(
                "-100.00000000000000"
            )
            interval["boundary_energy_out_j"] = _quantity(
                "0.00000000000000000"
            )
            for region in interval["regions"]:
                region["end_sensible_enthalpy_j"] = dict(
                    region["start_sensible_enthalpy_j"]
                )
            interval["regions"][0]["end_kinetic_energy_j"] = dict(
                interval["regions"][0]["start_kinetic_energy_j"]
            )
            _sync_ledger(evidence, root)
            result = audit_document(evidence, root)

        self.assertTrue(result.numerical_closure_passed)
        self.assertEqual(result.total_source_energy_in_j, Decimal("0"))
        self.assertEqual(result.nominal_closure_fraction, Decimal("0"))
        self.assertTrue(result.thermal_storage_gate_passed)

    def test_regional_heating_and_cooling_cannot_cancel_development_metric(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = _evidence(root)
            interval = evidence["interval"]
            interval["boundary_energy_out_j"] = _quantity(
                "100.00000000000000"
            )
            fluid, solid = interval["regions"]
            fluid["end_sensible_enthalpy_j"] = _quantity(
                "1100.0000000000000"
            )
            solid["end_sensible_enthalpy_j"] = _quantity(
                "1900.0000000000000"
            )
            _sync_ledger(evidence, root)
            result = audit_document(evidence, root)

        self.assertTrue(result.numerical_closure_passed)
        self.assertEqual(result.sensible_enthalpy_change_j, Decimal("0"))
        self.assertEqual(
            result.gross_sensible_enthalpy_change_j, Decimal("200")
        )
        self.assertFalse(result.thermal_storage_gate_passed)

    def test_raw_ledger_must_exactly_bind_every_audited_value(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = _evidence(root)
            evidence["interval"]["boundary_energy_out_j"] = _quantity(
                "99.000000000000000"
            )
            with self.assertRaisesRegex(
                AuditInputError, "energy ledger does not exactly match"
            ):
                audit_document(evidence, root)

    def test_solver_step_rows_and_term_inventories_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            gapped = _evidence(root)

            def introduce_gap(ledger):
                ledger["solver_steps"][0]["start_time_s"] = "1.5005"
                ledger["solver_steps"][0]["delta_t_s"] = "0.0995"

            _mutate_raw_ledger(gapped, root, introduce_gap)
            with self.assertRaisesRegex(AuditInputError, "not gap-free"):
                audit_document(gapped, root)

            incomplete_terms = _evidence(root)

            def omit_radiation(ledger):
                terms = ledger["coverage_inventory"]["nonheat_equation_terms"]
                terms[:] = [term for term in terms if term["name"] != "radiation"]

            _mutate_raw_ledger(incomplete_terms, root, omit_radiation)
            with self.assertRaisesRegex(
                AuditInputError, "must cover exactly"
            ):
                audit_document(incomplete_terms, root)

            bad_sum = _evidence(root)

            def change_step_increment(ledger):
                ledger["solver_steps"][0]["boundary_energy_out_j"] = _quantity(
                    "21.000000000000000"
                )

            _mutate_raw_ledger(bad_sum, root, change_step_increment)
            with self.assertRaisesRegex(AuditInputError, "do not sum"):
                audit_document(bad_sum, root)

    def test_deep_cancellation_is_not_rounded_to_a_false_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = _evidence(root)
            evidence["numeric_precision_digits"] = 64
            interval = evidence["interval"]
            interval["applied_heat_energy_j"] = _exact("1")
            interval["other_source_energy_in_j"] = _quantity(
                "0." + "0" * 64, "1e-80"
            )
            interval["boundary_energy_out_j"] = _quantity(
                "0." + "9" * 64, "1e-80"
            )

            def precise(value: str) -> str:
                number = Decimal(value)
                if number == 0:
                    return "0." + "0" * 64
                return format(number, ".63E")

            for region in interval["regions"]:
                start = precise(region["start_sensible_enthalpy_j"]["value"])
                region["start_sensible_enthalpy_j"] = _quantity(start, "1e-80")
                region["end_sensible_enthalpy_j"] = _quantity(start, "1e-80")
                if region["kind"] == "fluid":
                    kinetic = precise(region["start_kinetic_energy_j"]["value"])
                    region["start_kinetic_energy_j"] = _quantity(
                        kinetic, "1e-80"
                    )
                    region["end_kinetic_energy_j"] = _quantity(
                        kinetic, "1e-80"
                    )
            _sync_ledger(evidence, root)
            result = audit_document(
                evidence,
                root,
                closure_tolerance="1e-65",
            )

        self.assertEqual(result.residual_j, Decimal("1e-64"))
        self.assertEqual(
            result.to_dict()["numerical_first_law"]["residual_j"], "1e-64"
        )
        self.assertFalse(result.numerical_closure_passed)

    def test_evidence_and_sources_cannot_be_overwritten_by_cli_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence_path = root / "evidence.json"
            evidence_path.write_text(
                json.dumps(_evidence(root), indent=2) + "\n", encoding="utf-8"
            )
            ledger_path = root / "openfoam-energy-ledger.json"
            dependency_path = root / "start-fluid-sensible_enthalpy.dat"
            same_output = root / "result.out"
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                self.assertEqual(
                    main([str(evidence_path), "--json", str(evidence_path)]),
                    2,
                )
                self.assertEqual(
                    main([str(evidence_path), "--markdown", str(ledger_path)]),
                    2,
                )
                self.assertEqual(
                    main([str(evidence_path), "--json", str(dependency_path)]),
                    2,
                )
                self.assertEqual(
                    main(
                        [
                            str(evidence_path),
                            "--json",
                            str(same_output),
                            "--markdown",
                            str(same_output),
                        ]
                    ),
                    2,
                )

    def test_invalid_utf8_and_output_io_errors_return_input_error_status(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            invalid = root / "invalid.json"
            invalid.write_bytes(b"\xff")
            with self.assertRaisesRegex(AuditInputError, "cannot read"):
                audit_file(invalid)

            evidence_path = root / "evidence.json"
            evidence_path.write_text(
                json.dumps(_evidence(root), indent=2) + "\n", encoding="utf-8"
            )
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                self.assertEqual(
                    main([str(evidence_path), "--json", str(root)]),
                    2,
                )

    def test_audit_file_json_and_cli_exit_codes_keep_statuses_separate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence_path = root / "evidence.json"
            evidence_path.write_text(
                json.dumps(_evidence(root), indent=2) + "\n", encoding="utf-8"
            )
            result = audit_file(evidence_path)
            self.assertEqual(result.to_dict()["numerical_first_law"]["status"], "PASS")
            provenance = result.to_dict()["provenance"]
            self.assertEqual(provenance["run_uuid"], RUN_UUID)
            self.assertEqual(
                provenance["case_configuration_sha256"],
                CONFIGURATION_SHA256,
            )
            self.assertEqual(provenance["mesh_sha256"], MESH_SHA256)
            self.assertEqual(provenance["solver_step_count"], 1)
            self.assertEqual(len(provenance["energy_ledger_sha256"]), 64)
            self.assertEqual(len(provenance["evidence_content_sha256"]), 64)
            self.assertEqual(
                result.to_dict()["thermal_development_indicator"]["status"],
                "SIGNIFICANT_GROSS_REGION_STORAGE",
            )
            with redirect_stdout(io.StringIO()):
                self.assertEqual(main([str(evidence_path)]), 0)
                self.assertEqual(
                    main(
                        [str(evidence_path), "--require-low-storage"]
                    ),
                    3,
                )

    def test_decimal_precision_counter_handles_scientific_and_zero_tokens(self):
        self.assertEqual(significant_digits("2.931771e+02"), 7)
        self.assertEqual(significant_digits("293.17710000000000"), 17)
        self.assertEqual(significant_digits("0.0000000000000000"), 16)


if __name__ == "__main__":
    unittest.main()
