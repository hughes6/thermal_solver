[CmdletBinding()]
param(
    [string]$WorkspaceRoot = "",
    [string]$OutputPath =
        "validation/revised_native_regression_2026-08-26/" +
        "runtime_acceleration_runner_state_gates_2026-08-27.manifest.json",
    [string[]]$AdditionalRequiredFile = @(),
    [switch]$VerifyOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-FullWorkspacePath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd([char[]]@("\", "/"))
    $candidate = if ([IO.Path]::IsPathRooted($Path)) {
        [IO.Path]::GetFullPath($Path)
    }
    else {
        [IO.Path]::GetFullPath((Join-Path $Root $Path))
    }
    $rootPrefix = $rootFull +
        [IO.Path]::DirectorySeparatorChar
    if (-not $candidate.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside the workspace: $candidate"
    }
    return $candidate
}

function Get-WorkspaceRelativePath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$FullPath
    )

    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd([char[]]@("\", "/"))
    $candidate = [IO.Path]::GetFullPath($FullPath)
    $rootPrefix = $rootFull + [IO.Path]::DirectorySeparatorChar
    if (-not $candidate.StartsWith(
        $rootPrefix,
        [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside the workspace: $FullPath"
    }
    $relative = $candidate.Substring($rootPrefix.Length).Replace("\", "/")
    return $relative
}

function Get-OrdinalSortedStrings {
    param([Parameter(Mandatory = $true)][string[]]$Values)

    $copy = [string[]]$Values.Clone()
    [Array]::Sort($copy, [StringComparer]::Ordinal)
    return $copy
}

function Get-FileEvidence {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$FullPath
    )

    if (-not (Test-Path -LiteralPath $FullPath -PathType Leaf)) {
        throw "Required manifest input is missing: $FullPath"
    }

    $stream = [IO.File]::Open(
        $FullPath,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    try {
        $length = $stream.Length
        $sha256 = [Security.Cryptography.SHA256]::Create()
        try {
            $digestBytes = $sha256.ComputeHash($stream)
        }
        finally {
            $sha256.Dispose()
        }
        if ($stream.Length -ne $length) {
            throw "File length changed while hashing: $FullPath"
        }
    }
    finally {
        $stream.Dispose()
    }

    $digest = -join ($digestBytes | ForEach-Object { $_.ToString("x2") })
    return [pscustomobject][ordered]@{
        path = Get-WorkspaceRelativePath -Root $Root -FullPath $FullPath
        bytes = [long]$length
        sha256 = $digest
    }
}

function Invoke-TextCommand {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $lines = @(& $Executable @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed ($LASTEXITCODE): $Executable $($Arguments -join ' ')"
    }
    return (($lines | ForEach-Object { [string]$_ }) -join "`n").Trim()
}

function Read-JsonEvidence {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $fullPath = Get-FullWorkspacePath -Root $Root -Path $Path
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Required JSON evidence is missing: $Path"
    }
    try {
        return Get-Content -LiteralPath $fullPath -Raw | ConvertFrom-Json
    }
    catch {
        throw "Failed to parse JSON evidence '$Path': $($_.Exception.Message)"
    }
}

function Format-InvariantNumber {
    param(
        [Parameter(Mandatory = $true)][double]$Value,
        [Parameter(Mandatory = $true)][string]$Format
    )

    return $Value.ToString(
        $Format,
        [Globalization.CultureInfo]::InvariantCulture)
}

function Get-RegressionRunSummary {
    param([Parameter(Mandatory = $true)]$Run)

    $elapsed = Format-InvariantNumber `
        -Value ([double]$Run.elapsed_seconds) `
        -Format "0.###"
    return (
        "status $($Run.status); exit $($Run.exit_code) in $elapsed s; " +
        "stdout $($Run.stdout.bytes) bytes SHA-256 $($Run.stdout.sha256); " +
        "stderr $($Run.stderr.bytes) bytes SHA-256 $($Run.stderr.sha256); " +
        "affinity mask $($Run.processor_affinity_mask) " +
        "($($Run.logical_processors_selected) logical processors), priority " +
        "$($Run.priority_class); $($Run.stdout.last_line)")
}

function Assert-RegressionRunEvidence {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)]$Run
    )

    if ([string]$Run.status -cne "PASS" -or [int]$Run.exit_code -ne 0) {
        throw "The last complete regression evidence is not an exit-zero PASS."
    }
    foreach ($streamName in @("stdout", "stderr")) {
        $stream = $Run.$streamName
        $fullPath = Get-FullWorkspacePath -Root $Root -Path ([string]$stream.path)
        $actual = Get-FileEvidence -Root $Root -FullPath $fullPath
        if ([long]$stream.bytes -ne [long]$actual.bytes -or
            [string]$stream.sha256 -cne [string]$actual.sha256) {
            throw "Final regression $streamName metadata does not match its file."
        }
    }
}

function Get-ResourceGateSummary {
    param([Parameter(Mandatory = $true)]$Gate)

    $process = $Gate.host.process_check
    $disk = $Gate.host.disk_check
    $memory = $Gate.host.memory_check
    $competitorCount = @($process.competitors).Count
    $processSummary = if ([bool]$process.passed) {
        "process gate passed with $competitorCount competitors"
    }
    else {
        "process gate failed with $competitorCount competitors"
    }
    $diskSummary = if ($null -ne $disk.skipped_reason) {
        "disk skipped ($($disk.skipped_reason))"
    }
    elseif ([bool]$disk.passed) {
        "disk passed at $($disk.free_bytes) free bytes versus $($disk.minimum_bytes) required"
    }
    else {
        "disk failed at $($disk.free_bytes) free bytes versus $($disk.minimum_bytes) required"
    }
    $memorySummary = if ($null -ne $memory.skipped_reason) {
        "memory skipped ($($memory.skipped_reason))"
    }
    elseif ([bool]$memory.passed) {
        "memory passed at $($memory.minimum_observed_bytes) minimum observed bytes " +
            "versus $($memory.minimum_bytes) required"
    }
    else {
        "memory failed at $($memory.minimum_observed_bytes) minimum observed bytes " +
            "versus $($memory.minimum_bytes) required"
    }
    $wslSummary = if ([bool]$Gate.wsl.queried) {
        "WSL queried; passed=$($Gate.wsl.passed)"
    }
    else {
        "WSL not queried"
    }
    return (
        "status $($Gate.status); exit $($Gate.exit_code); $processSummary; " +
        "$diskSummary; $memorySummary; $wslSummary")
}

function Assert-ManifestValid {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$ManifestFullPath
    )

    if (-not (Test-Path -LiteralPath $ManifestFullPath -PathType Leaf)) {
        throw "Manifest is missing: $ManifestFullPath"
    }
    $manifest = Get-Content -LiteralPath $ManifestFullPath -Raw | ConvertFrom-Json
    if ([int]$manifest.schema_version -ne 1) {
        throw "Unsupported runtime-acceleration manifest schema version."
    }

    $manifestRelative = Get-WorkspaceRelativePath -Root $Root -FullPath $ManifestFullPath
    if ([string]$manifest.integrity.manifest_path -cne $manifestRelative) {
        throw "Manifest integrity path does not match the file being verified."
    }
    $rows = @($manifest.authoritative_files)
    if ($rows.Count -eq 0) {
        throw "Manifest has no authoritative file rows."
    }
    $paths = [string[]]@($rows | ForEach-Object { [string]$_.path })
    $sortedPaths = Get-OrdinalSortedStrings -Values $paths
    for ($index = 0; $index -lt $paths.Count; ++$index) {
        if ($paths[$index] -cne $sortedPaths[$index]) {
            throw "Manifest paths are not in stable ordinal order."
        }
        if ($index -gt 0 -and $paths[$index] -ceq $paths[$index - 1]) {
            throw "Manifest contains a duplicate path: $($paths[$index])"
        }
        if ($paths[$index] -ceq $manifestRelative) {
            throw "Manifest incorrectly includes a self-referential hash."
        }
    }

    $authoritativePathSet =
        [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($path in $paths) {
        [void]$authoritativePathSet.Add($path)
    }
    if (-not [bool]$manifest.integrity.generator_is_hashed) {
        throw "Manifest does not declare its generator as hashed."
    }
    $generatorPath = [string]$manifest.integrity.generator_path
    $runningGeneratorPath = Get-WorkspaceRelativePath `
        -Root $Root `
        -FullPath $PSCommandPath
    if ([string]::IsNullOrWhiteSpace($generatorPath) -or
        $generatorPath -cne $runningGeneratorPath -or
        -not $authoritativePathSet.Contains($generatorPath)) {
        throw "Manifest generator path is not an authoritative hashed row."
    }

    $commandRows = @($manifest.commands)
    if ($commandRows.Count -eq 0) {
        throw "Manifest has no command/evidence rows."
    }
    foreach ($commandRow in $commandRows) {
        $evidenceProperty = $commandRow.PSObject.Properties["evidence"]
        if ($null -eq $evidenceProperty) {
            throw "Manifest command row has no evidence reference."
        }
        $evidencePaths = @($commandRow.evidence)
        if ($evidencePaths.Count -eq 0) {
            throw "Manifest command row has an empty evidence reference."
        }
        foreach ($evidence in $evidencePaths) {
            $evidencePath = [string]$evidence
            if ([string]::IsNullOrWhiteSpace($evidencePath)) {
                throw "Manifest command row has a blank evidence reference."
            }
            if ($evidencePath -ceq $manifestRelative) {
                continue
            }
            if (-not $authoritativePathSet.Contains($evidencePath)) {
                throw "Manifest command evidence is not an authoritative row: $evidencePath"
            }
        }
    }

    $verifiedBytes = 0L
    foreach ($row in $rows) {
        $fullPath = Get-FullWorkspacePath -Root $Root -Path ([string]$row.path)
        $actual = Get-FileEvidence -Root $Root -FullPath $fullPath
        if ([long]$row.bytes -ne [long]$actual.bytes) {
            throw "Byte-count mismatch for $($row.path)."
        }
        if ([string]$row.sha256 -cne [string]$actual.sha256) {
            throw "SHA-256 mismatch for $($row.path)."
        }
        $verifiedBytes += [long]$actual.bytes
    }
    if ([long]$manifest.file_summary.file_count -ne $rows.Count) {
        throw "Manifest file-count summary does not match its rows."
    }
    if ([long]$manifest.file_summary.total_bytes -ne $verifiedBytes) {
        throw "Manifest byte-count summary does not match its rows."
    }
    if (-not [bool]$manifest.integrity.self_hash_excluded) {
        throw "Manifest does not declare its self-hash exclusion."
    }

    return [pscustomobject][ordered]@{
        manifest = $manifestRelative
        files_verified = $rows.Count
        bytes_verified = $verifiedBytes
        self_hash_excluded = $true
    }
}

if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorkspaceRoot = Split-Path -Parent $PSScriptRoot
}
$workspaceFull = [IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd([char[]]@("\", "/"))
if (-not (Test-Path -LiteralPath $workspaceFull -PathType Container)) {
    throw "Workspace root is missing: $workspaceFull"
}
$manifestFull = Get-FullWorkspacePath -Root $workspaceFull -Path $OutputPath

if ($VerifyOnly) {
    $verification = Assert-ManifestValid `
        -Root $workspaceFull `
        -ManifestFullPath $manifestFull
    $verification | ConvertTo-Json -Depth 4
    return
}

$requiredFiles = @(
    "README.md",
    "NEW_LAB_MODEL_SCREENING.md",
    "UPDATED_MODEL_INTAKE.md",
    "UPDATED_MODEL_STATIC_AUDIT.md",
    "model.exe",
    "main.cpp",
    "model_runner.cpp",
    "output.txt",
    "openfoam_semifrozen_solver/Make/files",
    "openfoam_semifrozen_solver/Make/options",
    "openfoam_semifrozen_solver/semiFrozenChtMultiRegionFoam.C",
    "src/air_properties.hpp",
    "src/cell.hpp",
    "src/collision.hpp",
    "src/component_grapher.hpp",
    "src/component.hpp",
    "src/convection.hpp",
    "src/environment.hpp",
    "src/fan.hpp",
    "src/flow_solver.hpp",
    "src/grapher.hpp",
    "src/input/input_types.hpp",
    "src/input/model_loader.hpp",
    "src/logger.hpp",
    "src/mesh.hpp",
    "src/mesh_refinement_planner.hpp",
    "src/openfoam_exporter.hpp",
    "src/porous_region.hpp",
    "src/rack.hpp",
    "src/run_metadata.hpp",
    "src/solver.hpp",
    "src/thermal_estimator.hpp",
    "src/toml.hpp",
    "src/vent.hpp",
    "src/workload.hpp",
    "library/openfoam_cfg/default_foam_cfg.toml",
    "library/openfoam_cfg/indepth_foam_cfg.toml",
    "library/openfoam_cfg/screening_foam_cfg.toml",
    "library/openfoam_cfg/validation_foam_cfg.toml",
    "library/models/new_model_updated.toml",
    "library/models/new_model_updated_native_regression.toml",
    "library/models/new_model_updated_native_smoke.toml",
    "library/models/new_model_updated_openfoam_export_test.toml",
    "library/fan_curves/fan_curves.toml",
    "library/fan_curves/fan_curves_updated_2026_08_24.toml",
    "library/components/updated_eaton_UPS.toml",
    "library/components/updated_DELL_R360.toml",
    "library/components/updated_DELL_R470.toml",
    "library/components/updated_Keysight_N5766A.toml",
    "library/components/updated_keysight_N6701C.toml",
    "library/components/updated_trenton_3u_bam.toml",
    "library/components/eaton_KVM.toml",
    "library/components/updated_Thruster_Load_Box.toml",
    "library/components/updated_cisco_catalyst_9300_24.toml",
    "library/components/EATON_PDU_PDUMNH30.toml",
    "library/components/Fan_control_kit_PS.toml",
    "library/components/updated_NI_PXIe_Chassis.toml",
    "library/components/updated_NI_PXIe_Chassis_minimum_separator_sensitivity.toml",
    "library/models/validation_ni_minimum_separator_sensitivity.toml",
    "validation/UPDATED_MODEL_ASSUMPTIONS.toml",
    "plot/__init__.py",
    "plot/coarse_heat_animation.py",
    "plot/coarse_heat_io.py",
    "plot/exhaust_recirculation_matrix.py",
    "plot/fluid_results.py",
    "plot/heat_animation.py",
    "plot/plot.py",
    "plot/plot_component.py",
    "plot/recirculation_report.py",
    "plot/run_metadata.py",
    "plot_outlet_flow.py",
    "tools/archive_openfoam_checkpoint.ps1",
    "tools/audit_component_geometry.py",
    "tools/benchmark_openfoam_thermal_timestep.sh",
    "tools/build_openfoam_semifrozen_solver.sh",
    "tools/create_runtime_acceleration_manifest.ps1",
    "tools/exhaust_recirculation_tracer.py",
    "tools/fan_curve_fitter.py",
    "tools/generic_server_characterizer.py",
    "tools/heat_load_estimator.py",
    "tools/map_openfoam_case.py",
    "tools/model_release_readiness.py",
    "tools/openfoam_boundary_mass_balance_audit.py",
    "tools/openfoam_component_report.py",
    "tools/openfoam_cross_case_comparison.py",
    "tools/openfoam_fan_flow_audit.py",
    "tools/openfoam_fan_operating_domain_audit.py",
    "tools/openfoam_field_convergence.py",
    "tools/openfoam_field_delta.py",
    "tools/openfoam_heat_source_audit.py",
    "tools/openfoam_mesh_comparison.py",
    "tools/openfoam_partition_audit.py",
    "tools/openfoam_progress.py",
    "tools/openfoam_resource_gate.ps1",
    "tools/openfoam_semifrozen_attestation.py",
    "tools/openfoam_stream_region_selectors.py",
    "tools/openfoam_temperature_comparison.py",
    "tools/openfoam_transient_energy_audit.py",
    "tools/openfoam_yplus_report.py",
    "tools/porous_obstruction_calculator.py",
    "tools/prepare_openfoam_regions_low_memory.sh",
    "tools/rack_system_curve.py",
    "tools/render_component_plots.py",
    "tools/validate_openfoam_case.py",
    "tests/air_heat_source_parser_test.cpp",
    "tests/archive_openfoam_checkpoint_test.ps1",
    "tests/component_geometry_audit_test.py",
    "tests/adaptive_convection_conservation_test.cpp",
    "tests/advection_subcycling_test.cpp",
    "tests/boundary_fan_curve_discretization_test.cpp",
    "tests/coarse_heat_io_test.py",
    "tests/convection_stability_guard_test.cpp",
    "tests/engineering_tools_test.py",
    "tests/exhaust_recirculation_matrix_plot_test.py",
    "tests/exhaust_recirculation_tracer_test.py",
    "tests/face_wall_test.cpp",
    "tests/fan_active_set_test.cpp",
    "tests/flow_network_grounding_test.cpp",
    "tests/flow_nonlinear_convergence_test.cpp",
    "tests/fluid_results_test.py",
    "tests/generated_runner_solver_provenance_test.ps1",
    "tests/generated_runner_timestep_policy_test.ps1",
    "tests/generated_runner_thermal_outer_test.ps1",
    "tests/internal_fan_face_topology_test.cpp",
    "tests/internal_fan_thermal_transfer_test.cpp",
    "tests/lab_model_contract_test.py",
    "tests/map_openfoam_case_test.py",
    "tests/mesh_preflight_guard_test.cpp",
    "tests/model_config_test.cpp",
    "tests/model_release_readiness_test.py",
    "tests/model_runner_tracer_commands_test.cpp",
    "tests/native_multiaxis_persistent_ledger_test.cpp",
    "tests/native_template_microcase_test.cpp",
    "tests/native_thermal_energy_ledger_test.cpp",
    "tests/ni_separator_mesh_ladder_test.cpp",
    "tests/openfoam_animation_test.py",
    "tests/openfoam_component_report_test.py",
    "tests/openfoam_connected_volume_test.cpp",
    "tests/openfoam_cross_case_comparison_test.py",
    "tests/openfoam_device_report_test.cpp",
    "tests/openfoam_export_test.cpp",
    "tests/openfoam_fan_flow_audit_test.py",
    "tests/openfoam_fan_operating_domain_audit_test.py",
    "tests/openfoam_field_convergence_test.py",
    "tests/openfoam_field_delta_test.py",
    "tests/openfoam_heat_source_audit_test.py",
    "tests/openfoam_mesh_comparison_test.py",
    "tests/openfoam_partition_audit_test.py",
    "tests/openfoam_profile_options_test.cpp",
    "tests/openfoam_profile_policy_test.py",
    "tests/openfoam_progress_test.py",
    "tests/openfoam_resource_gate_test.ps1",
    "tests/openfoam_semifrozen_attestation_test.py",
    "tests/openfoam_stream_region_selectors_test.py",
    "tests/openfoam_temperature_comparison_test.py",
    "tests/openfoam_transient_energy_audit_test.py",
    "tests/openfoam_validation_test.py",
    "tests/openfoam_yplus_report_test.py",
    "tests/pcg_flow_test.cpp",
    "tests/plot_geometry_test.py",
    "tests/porous_obstruction_calculator_test.py",
    "tests/porous_region_test.cpp",
    "tests/rack_system_curve_test.py",
    "tests/recirculation_report_test.py",
    "tests/run_added_feature_tests.ps1",
    "tests/run_metadata_test.cpp",
    "tests/run_metadata_test.py",
    "tests/semifrozen_solver_policy_test.py",
    "tests/solver_logger_timestep_test.cpp",
    "tests/test_openfoam_boundary_mass_balance_audit.py",
    "tests/updated_lab_model_contract_test.py",
    "validation/README.md",
    "validation/OPENFOAM_CORRECTED_RUN_READINESS_2026-08-26.md",
    "validation/revised_native_regression_2026-08-26/CONVECTION_AND_NI_HARDENING_2026-08-26.md",
    "validation/revised_native_regression_2026-08-26/CORRECTED_RUN_ACCELERATION_GATES_2026-08-27.md",
    "validation/revised_native_regression_2026-08-26/ENERGY_PLOTTING_PROVENANCE_AND_NI_LADDER_2026-08-26.md",
    "validation/revised_native_regression_2026-08-26/FULL_RACK_NATIVE_FEASIBILITY_2026-08-26.md",
    "validation/revised_native_regression_2026-08-26/GOAL_COMPLETION_AUDIT_2026-08-27.md",
    "validation/revised_native_regression_2026-08-26/HOST_RESOURCE_CLEANUP_2026-08-27.md",
    "validation/revised_native_regression_2026-08-26/NI_SEPARATOR_MESH_LADDER_2026-08-26.md",
    "validation/revised_native_regression_2026-08-26/LOW_MEMORY_OPENFOAM_REGION_PREPARATION_2026-08-27.md",
    "validation/revised_native_regression_2026-08-26/OPENFOAM_SOLVER_RUNTIME_ATTESTATION_2026-08-27.md",
    "validation/revised_native_regression_2026-08-26/RUN_REPORT.md",
    "validation/revised_native_regression_2026-08-26/WORKLOAD_MEMORY_RUNTIME_HARDENING_2026-08-26.md",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_final_runtime_hardening_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_interrupted_before_exporter_preflight_fix_2026-08-26.stderr.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_interrupted_before_exporter_preflight_fix_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_portable_io_hardening_interrupted_pretransaction_fix_2026-08-26.stderr.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_portable_io_hardening_interrupted_pretransaction_fix_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stderr.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_acceleration_gates_2026-08-27.run.json",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_acceleration_gates_2026-08-27.stderr.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_acceleration_gates_2026-08-27.stdout.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_runner_state_gates_2026-08-27.run.json",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_runner_state_gates_2026-08-27.stderr.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_runner_state_gates_2026-08-27.stdout.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_resource_gate_2026-08-27.stderr.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_resource_gate_2026-08-27.stdout.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_resource_gate_launch_quoting_failure_2026-08-27.stderr.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_resource_gate_launch_quoting_failure_2026-08-27.stdout.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_runtime_hardening_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_convection_hardening_2026-08-26.failed_initial.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_convection_hardening_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_energy_provenance_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/added_feature_regression_runtime_accel_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/geometry_audit_canonical_hardening_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/goal_completion_geometry_audit_2026-08-27.stdout.log",
    "validation/revised_native_regression_2026-08-26/component_campaign_shelf_contract_2026-08-27.stderr.log",
    "validation/revised_native_regression_2026-08-26/component_campaign_shelf_contract_2026-08-27.stdout.log",
    "validation/revised_native_regression_2026-08-26/goal_completion_material_fidelity_2026-08-27.stdout.log",
    "validation/revised_native_regression_2026-08-26/goal_completion_static_tests_2026-08-27.stderr.log",
    "validation/revised_native_regression_2026-08-26/goal_completion_static_tests_2026-08-27.stdout.log",
    "validation/revised_native_regression_2026-08-26/geometry_only_runtime_hardening_2026-08-26.stderr.log",
    "validation/revised_native_regression_2026-08-26/geometry_only_runtime_hardening_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/geometry_only_final_runtime_hardening_2026-08-26.stderr.log",
    "validation/revised_native_regression_2026-08-26/geometry_only_final_runtime_hardening_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/geometry_only_post_audit_runtime_hardening_2026-08-26.stderr.log",
    "validation/revised_native_regression_2026-08-26/geometry_only_post_audit_runtime_hardening_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/full_rack_native_preflight_final_runtime_hardening_2026-08-26.stderr.log",
    "validation/revised_native_regression_2026-08-26/full_rack_native_preflight_final_runtime_hardening_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/full_rack_native_preflight_post_audit_runtime_hardening_2026-08-26.stderr.log",
    "validation/revised_native_regression_2026-08-26/full_rack_native_preflight_post_audit_runtime_hardening_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/full_rack_native_preflight_runtime_hardening_2026-08-26.stderr.log",
    "validation/revised_native_regression_2026-08-26/full_rack_native_preflight_runtime_hardening_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/installed_openfoam_solver_provenance_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/model_config_thermal_outer_test.exe",
    "validation/revised_native_regression_2026-08-26/model_post_audit_runtime_hardening_2026-08-26.exe",
    "validation/revised_native_regression_2026-08-26/native_conservative_flow_active_set_2026-08-26.manifest.json",
    "validation/revised_native_regression_2026-08-26/native_template_microcase_final_verified_2026-08-26.manifest.json",
    "validation/revised_native_regression_2026-08-26/native_template_microcase_convection_hardening_2026-08-26.exe",
    "validation/revised_native_regression_2026-08-26/native_template_microcase_convection_hardening_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/native_template_microcase_energy_provenance_2026-08-26.exe",
    "validation/revised_native_regression_2026-08-26/native_template_microcase_energy_provenance_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/native_template_microcase_final_runtime_hardening_2026-08-26.exe",
    "validation/revised_native_regression_2026-08-26/native_template_microcase_final_runtime_hardening_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/native_template_campaign_drift_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/native_template_microcase_post_audit_runtime_hardening_2026-08-26.exe",
    "validation/revised_native_regression_2026-08-26/native_template_microcase_post_audit_runtime_hardening_2026-08-26.stderr.log",
    "validation/revised_native_regression_2026-08-26/native_template_microcase_post_audit_runtime_hardening_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/native_template_microcase_runtime_hardening_2026-08-26.exe",
    "validation/revised_native_regression_2026-08-26/native_template_microcase_runtime_hardening_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/native_template_microcase_ni_separator_mesh_diagnosis_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/native_template_microcase_ni_separator_sensitivity_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/native_template_microcase_runtime_accel_2026-08-26.exe",
    "validation/revised_native_regression_2026-08-26/native_template_microcase_runtime_accel_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/native_thermal_energy_ledger_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/ni_separator_mesh_ladder_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/ni_separator_mesh_ladder_test_2026-08-26.exe",
    "validation/revised_native_regression_2026-08-26/openfoam_provenance_gate_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/model_release_readiness_20260827T070322145Z.json",
    "validation/revised_native_regression_2026-08-26/model_release_readiness_goal_completion_2026-08-27.json",
    "validation/revised_native_regression_2026-08-26/model_release_readiness_test_2026-08-27.stderr.log",
    "validation/revised_native_regression_2026-08-26/model_release_readiness_test_2026-08-27.stdout.log",
    "validation/revised_native_regression_2026-08-26/openfoam_export_write_precision_2026-08-27.stdout.log",
    "validation/revised_native_regression_2026-08-26/openfoam_low_memory_selector_equivalence_2026-08-27.json",
    "validation/revised_native_regression_2026-08-26/openfoam_resource_gate_goal_resume_host_20260827T065641249Z.json",
    "validation/revised_native_regression_2026-08-26/openfoam_resource_gate_post_acceleration_20260827T081233978Z.json",
    "validation/revised_native_regression_2026-08-26/openfoam_resource_gate_post_temp_cleanup_20260827T083908417Z.json",
    "validation/revised_native_regression_2026-08-26/openfoam_resource_gate_final_idle_20260827T095617107Z.json",
    "validation/revised_native_regression_2026-08-26/openfoam_resource_gate_goal_completion_20260827T1020Z.json",
    "validation/revised_native_regression_2026-08-26/openfoam_solver_runtime_attestation_nonwsl_2026-08-27.stdout.log",
    "validation/revised_native_regression_2026-08-26/openfoam_resource_gate_pre_export_20260827T0038MDT.json",
    "validation/revised_native_regression_2026-08-26/openfoam_resource_gate_synthetic_2026-08-27.stdout.log",
    "validation/revised_native_regression_2026-08-26/openfoam_resource_recheck_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/openfoam_export_thermal_outer_test.exe",
    "validation/revised_native_regression_2026-08-26/plot_geometry_hardening_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/plotting_current_manifest_integrity_ansys_2026-08-27.stderr.log",
    "validation/revised_native_regression_2026-08-26/plotting_current_manifest_integrity_ansys_2026-08-27.stdout.log",
    "validation/revised_native_regression_2026-08-26/plotting_scientific_ansys_core_combined_order_failure_2026-08-27.stderr.log",
    "validation/revised_native_regression_2026-08-26/plotting_scientific_ansys_core_combined_order_failure_2026-08-27.stdout.log",
    "validation/revised_native_regression_2026-08-26/plotting_scientific_ansys_core_launcher_ctypes_failure_2026-08-27.stderr.log",
    "validation/revised_native_regression_2026-08-26/plotting_scientific_ansys_core_launcher_ctypes_failure_2026-08-27.stdout.log",
    "validation/revised_native_regression_2026-08-26/plotting_scientific_dependency_complete_ansys_core_2026-08-27.stderr.log",
    "validation/revised_native_regression_2026-08-26/plotting_scientific_dependency_complete_ansys_core_2026-08-27.stdout.log",
    "validation/revised_native_regression_2026-08-26/plotting_scientific_dependency_complete_ansys_pyvista_2026-08-27.stderr.log",
    "validation/revised_native_regression_2026-08-26/plotting_scientific_dependency_complete_ansys_pyvista_2026-08-27.stdout.log",
    "validation/revised_native_regression_2026-08-26/post_regression_resource_recheck_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/production_geometry_rebuild_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/model_runtime_hardening_2026-08-26.exe",
    "validation/revised_native_regression_2026-08-26/model_final_runtime_hardening_2026-08-26.exe",
    "validation/revised_native_regression_2026-08-26/semifrozen_solver_policy_hardening_2026-08-26.stdout.log",
    "validation/revised_native_regression_2026-08-26/temp_cleanup_post_runner_state_20260827T083422537Z.json",
    "validation/revised_native_regression_2026-08-26/runtime_acceleration_final_runtime_hardening_2026-08-26.manifest.json",
    "validation/revised_openfoam_19mm_oom_2026-08-24/prepare_manual.stdout.log",
    "validation/revised_openfoam_22p5mm_2026-08-25/MESH_QUALITY_AUDIT.md",
    "validation/revised_openfoam_22p5mm_2026-08-25/OPENFOAM_ARCHIVE_COMPLETION_2026-08-26.md",
    "validation/revised_openfoam_22p5mm_2026-08-25/PERFORMANCE_AUDIT.md",
    "validation/revised_openfoam_22p5mm_2026-08-25/RUN_STATUS.md",
    "validation/revised_openfoam_22p5mm_2026-08-25/RUNTIME_ACCELERATION_POLICY_2026-08-26.md",
    "validation/revised_openfoam_22p5mm_2026-08-25/retained_case_post_archive_integrity_2026-08-27.json",
    "validation/revised_openfoam_22p5mm_2026-08-25/THERMAL_TIMESTEP_ACCELERATION_2026-08-26.md",
    "validation/revised_openfoam_22p5mm_2026-08-25/TRANSIENT_FIRST_LAW_AUDIT_STATUS_1P50_1P60.md",
    "validation/revised_openfoam_22p5mm_2026-08-25/actual_case_audit_evidence_manifest.csv",
    "validation/revised_openfoam_22p5mm_2026-08-25/boundary_mass_balance_through_1p60.csv",
    "validation/revised_openfoam_22p5mm_2026-08-25/fan_operating_domain_1p60.csv",
    "validation/revised_openfoam_22p5mm_2026-08-25/fan_operating_domain_1p60.md",
    "validation/revised_openfoam_22p5mm_2026-08-25/field_convergence_1p40_1p50_to_1p60.csv",
    "validation/revised_openfoam_22p5mm_2026-08-25/openfoam_archive_remote_reverification_2026-08-26.json",
    "validation/revised_openfoam_22p5mm_2026-08-25/validation_1p60.json",
    "validation/revised_openfoam_22p5mm_2026-08-25/validation_1p60.md"
) + $AdditionalRequiredFile

$requiredTrees = @(
    "library/components",
    "library/fan_curves",
    "library/loggers",
    "library/models",
    "library/openfoam_cfg",
    "library/tests",
    "validation/current_geometry_export_2026-08-26",
    "validation/ni_minimum_separator_sensitivity_2026-08-26",
    "validation/openfoam_solver_attestation_microcase_template_2026-08-27",
    "validation/revised_openfoam_22p5mm_2026-08-25/field_plots_1p60",
    "validation/thermal_outer_microcase_2026-08-26",
    "validation/updated_component_plots_current_2026-08-26"
)
$requiredFlatTrees = @(
    "validation/revised_openfoam_22p5mm_2026-08-25/performance_benchmarks_1p60"
)

$pathSet = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($relativePath in $requiredFiles) {
    $fullPath = Get-FullWorkspacePath -Root $workspaceFull -Path $relativePath
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Required manifest input is missing: $relativePath"
    }
    [void]$pathSet.Add((Get-WorkspaceRelativePath -Root $workspaceFull -FullPath $fullPath))
}
foreach ($relativeTree in $requiredTrees) {
    $fullTree = Get-FullWorkspacePath -Root $workspaceFull -Path $relativeTree
    if (-not (Test-Path -LiteralPath $fullTree -PathType Container)) {
        throw "Required manifest tree is missing: $relativeTree"
    }
    $treeFiles = @(Get-ChildItem -LiteralPath $fullTree -Recurse -Force -File)
    if ($treeFiles.Count -eq 0) {
        throw "Required manifest tree is empty: $relativeTree"
    }
    foreach ($file in $treeFiles) {
        [void]$pathSet.Add((
            Get-WorkspaceRelativePath -Root $workspaceFull -FullPath $file.FullName))
    }
}
foreach ($relativeTree in $requiredFlatTrees) {
    $fullTree = Get-FullWorkspacePath -Root $workspaceFull -Path $relativeTree
    if (-not (Test-Path -LiteralPath $fullTree -PathType Container)) {
        throw "Required manifest tree is missing: $relativeTree"
    }
    $treeFiles = @(Get-ChildItem -LiteralPath $fullTree -Force -File)
    if ($treeFiles.Count -eq 0) {
        throw "Required manifest tree is empty: $relativeTree"
    }
    foreach ($file in $treeFiles) {
        [void]$pathSet.Add((
            Get-WorkspaceRelativePath -Root $workspaceFull -FullPath $file.FullName))
    }
}

$manifestRelative = Get-WorkspaceRelativePath -Root $workspaceFull -FullPath $manifestFull
if ($pathSet.Contains($manifestRelative)) {
    throw "The output manifest must be excluded from its own authoritative files."
}

$relativePaths = Get-OrdinalSortedStrings -Values ([string[]]$pathSet)
$evidenceRows = [Collections.Generic.List[object]]::new()
$totalBytes = 0L
foreach ($relativePath in $relativePaths) {
    $fullPath = Get-FullWorkspacePath -Root $workspaceFull -Path $relativePath
    $row = Get-FileEvidence -Root $workspaceFull -FullPath $fullPath
    $evidenceRows.Add($row)
    $totalBytes += [long]$row.bytes
}

$gitHead = Invoke-TextCommand -Executable "git" -Arguments @("rev-parse", "HEAD")
$gitBranch = Invoke-TextCommand -Executable "git" -Arguments @("branch", "--show-current")
$gitStatus = Invoke-TextCommand -Executable "git" -Arguments @(
    "status", "--porcelain=v1", "--untracked-files=normal")
$statusLines = @(
    $gitStatus -split "\r?\n" |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
)
$compilerVersion = (Invoke-TextCommand -Executable "g++" -Arguments @("--version")).Split("`n")[0]
$pythonVersion = Invoke-TextCommand -Executable "python" -Arguments @("--version")
$gitVersion = Invoke-TextCommand -Executable "git" -Arguments @("--version")
$gitBashFull = [IO.Path]::GetFullPath("C:/Program Files/Git/bin/bash.exe")
if (-not (Test-Path -LiteralPath $gitBashFull -PathType Leaf)) {
    throw "Required Git Bash executable is missing: $gitBashFull"
}
$bashVersion = (Invoke-TextCommand -Executable $gitBashFull -Arguments @("--version")).Split("`n")[0]

$finalRegressionPath =
    "validation/revised_native_regression_2026-08-26/" +
    "added_feature_regression_runner_state_gates_2026-08-27.run.json"
$accelerationRegressionPath =
    "validation/revised_native_regression_2026-08-26/" +
    "added_feature_regression_acceleration_gates_2026-08-27.run.json"
$goalResumeResourcePath =
    "validation/revised_native_regression_2026-08-26/" +
    "openfoam_resource_gate_goal_resume_host_20260827T065641249Z.json"
$postAccelerationResourcePath =
    "validation/revised_native_regression_2026-08-26/" +
    "openfoam_resource_gate_post_acceleration_20260827T081233978Z.json"
$postCleanupResourcePath =
    "validation/revised_native_regression_2026-08-26/" +
    "openfoam_resource_gate_post_temp_cleanup_20260827T083908417Z.json"
$finalIdleResourcePath =
    "validation/revised_native_regression_2026-08-26/" +
    "openfoam_resource_gate_final_idle_20260827T095617107Z.json"
$goalCompletionResourcePath =
    "validation/revised_native_regression_2026-08-26/" +
    "openfoam_resource_gate_goal_completion_20260827T1020Z.json"
$preExportResourcePath =
    "validation/revised_native_regression_2026-08-26/" +
    "openfoam_resource_gate_pre_export_20260827T0038MDT.json"
$tempCleanupPath =
    "validation/revised_native_regression_2026-08-26/" +
    "temp_cleanup_post_runner_state_20260827T083422537Z.json"

$finalRegression = Read-JsonEvidence -Root $workspaceFull -Path $finalRegressionPath
Assert-RegressionRunEvidence -Root $workspaceFull -Run $finalRegression
$finalRegressionSummary = Get-RegressionRunSummary -Run $finalRegression
$accelerationRegression =
    Read-JsonEvidence -Root $workspaceFull -Path $accelerationRegressionPath
$accelerationRegressionSummary =
    Get-RegressionRunSummary -Run $accelerationRegression

$goalResumeResource =
    Read-JsonEvidence -Root $workspaceFull -Path $goalResumeResourcePath
$postAccelerationResource =
    Read-JsonEvidence -Root $workspaceFull -Path $postAccelerationResourcePath
$postCleanupResource =
    Read-JsonEvidence -Root $workspaceFull -Path $postCleanupResourcePath
$finalIdleResource =
    Read-JsonEvidence -Root $workspaceFull -Path $finalIdleResourcePath
$goalCompletionResource =
    Read-JsonEvidence -Root $workspaceFull -Path $goalCompletionResourcePath
$preExportResource =
    Read-JsonEvidence -Root $workspaceFull -Path $preExportResourcePath
$goalResumeResourceSummary = Get-ResourceGateSummary -Gate $goalResumeResource
$postAccelerationResourceSummary =
    Get-ResourceGateSummary -Gate $postAccelerationResource
$postCleanupResourceSummary = Get-ResourceGateSummary -Gate $postCleanupResource
$finalIdleResourceSummary = Get-ResourceGateSummary -Gate $finalIdleResource
$goalCompletionResourceSummary =
    Get-ResourceGateSummary -Gate $goalCompletionResource
$preExportResourceSummary = Get-ResourceGateSummary -Gate $preExportResource

$tempCleanup = Read-JsonEvidence -Root $workspaceFull -Path $tempCleanupPath
$cleanupTargets = @($tempCleanup.targets)
if ($cleanupTargets.Count -eq 0 -or
    @($cleanupTargets | Where-Object { -not [bool]$_.deleted }).Count -ne 0 -or
    [bool]$tempCleanup.scope_guards.workspace_validation_evidence_modified -or
    [bool]$tempCleanup.scope_guards.active_openfoam_case_modified -or
    [bool]$tempCleanup.scope_guards.retained_times_0_1p50_1p60_modified -or
    [bool]$tempCleanup.scope_guards.c_openfoam_modified) {
    throw "The post-runner-state cleanup evidence does not satisfy its scope guards."
}
$tempCleanupSummary =
    "$($cleanupTargets.Count) audited inactive installer-temp trees deleted; " +
    "$($tempCleanup.pre_delete_recheck.stable_audit_total_bytes) audited bytes; " +
    "$($tempCleanup.pre_delete_recheck.stable_audit_total_files) files; C: free-space " +
    "increase $($tempCleanup.disk.free_byte_increase) bytes; workspace validation, " +
    "active OpenFOAM state, retained times 0/1.50/1.60, and C:\OpenFOAM preserved"

$manifest = [ordered]@{
    schema_version = 1
    created_utc = [DateTime]::UtcNow.ToString("o")
    campaign =
        "updated research-lab runtime acceleration, workload/memory/output " +
        "hardening, portable case-folded probe output guards, convection/NI " +
        "hardening, native energy closure, plot recovery, OpenFOAM read-only " +
        "preflight/checked face arithmetic/ambient volume/geometry transaction " +
        "hardening, provenance gating, fail-closed host/WSL resource gating, " +
        "20-second reusable thermal-cap enforcement, exact fixed runner " +
        "endpoints, transactional airflow refresh, restart-time Courant " +
        "revalidation, marker-last fan-ramp recovery, bounded warm windows, " +
        "tracked process-group termination, bounded-memory region preparation, runtime solver " +
        "attestation/build rollback, model-input release gating, host disk " +
        "cleanup, and post-audit dated-source verification"
    integrity = [ordered]@{
        manifest_path = $manifestRelative
        self_hash_excluded = $true
        generator_path = "tools/create_runtime_acceleration_manifest.ps1"
        generator_is_hashed = $true
        exclusions = @(
            [ordered]@{
                path = $manifestRelative
                reason = "A file cannot contain a stable hash of its own final bytes."
            }
        )
    }
    source_state = [ordered]@{
        git_head = $gitHead
        git_branch = $gitBranch
        working_tree_clean = ($statusLines.Count -eq 0)
        porcelain_entry_count = $statusLines.Count
        note =
            "The working tree is intentionally dirty; authoritative source and evidence " +
            "are pinned by the sorted per-file SHA-256 rows below."
    }
    toolchain = [ordered]@{
        powershell = "$($PSVersionTable.PSEdition) $($PSVersionTable.PSVersion)"
        operating_system = [Environment]::OSVersion.VersionString
        python = $pythonVersion
        compiler = $compilerVersion
        git = $gitVersion
        bash = $bashVersion
        plot_python =
            "ANSYS Python 3.12.11; Matplotlib 3.10.0; NumPy 1.26.4; " +
            "pandas 2.2.2; PyVista 0.44.0 package present but vtkmodules and " +
            "imageio unavailable (versions captured in plot evidence)"
        openfoam =
            "OpenFOAM v2606; installed custom solver is stale and heavy rebuild/runtime " +
            "launch was withheld by the goal-completion 2026-08-27 fail-closed 5 GiB gate; " +
            "new attestation/build tests are non-WSL synthetic evidence only"
    }
    commands = @(
        [ordered]@{
            purpose = "last complete pre-inline-shelf-campaign production-source regression under the user-requested low-contention guard"
            command = [string]$finalRegression.command
            result =
                "$finalRegressionSummary; predates the current uncompiled inline-shelf " +
                "microcase and default-harness component-campaign addition, so it is not " +
                "a complete pass for the present test tree"
            evidence = @(
                [string]$finalRegression.stdout.path,
                [string]$finalRegression.stderr.path,
                $finalRegressionPath
            )
        },
        [ordered]@{
            purpose = "current inline-shelf component contract and uncompiled O2 campaign implementation"
            command = "python -m unittest -v tests.updated_lab_model_contract_test; NOT RUN under unsafe memory: g++ -std=c++17 -O2 -I src tests/native_template_microcase_test.cpp"
            result =
                "Python contract PASS 17/17 in 0.119 s under affinity mask 3 and " +
                "BelowNormal; C++ source adds the exact inline shelf as the twelfth " +
                "functional plus one expected-rejection case and is part of the default " +
                "harness, but O2 compile/filtered/full execution are UNPROVEN because " +
                "available memory remained below 1 GiB"
            evidence = @(
                "tests/native_template_microcase_test.cpp",
                "tests/run_added_feature_tests.ps1",
                "tests/updated_lab_model_contract_test.py",
                "validation/revised_native_regression_2026-08-26/component_campaign_shelf_contract_2026-08-27.stdout.log",
                "validation/revised_native_regression_2026-08-26/component_campaign_shelf_contract_2026-08-27.stderr.log"
            )
        },
        [ordered]@{
            purpose = "dependency-complete core plotting/field comparisons and current artifact integrity"
            command = "ANSYS Python 3.12.11 self-limited with psutil to affinity [0,1] and BelowNormal; run 77 core unittest cases, 9 selected PyVista cases, and 23 plot-manifest integrity checks"
            result =
                "core PASS 77/77 with zero skips in 2.499 s; artifact integrity PASS " +
                "23/23 with zero mismatches; PyVista selection ran 7 tests with four " +
                "skips and suppressed two mesh tests because vtkmodules/imageio are " +
                "unavailable; two launcher-only failures are preserved separately"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/plotting_scientific_dependency_complete_ansys_core_2026-08-27.stdout.log",
                "validation/revised_native_regression_2026-08-26/plotting_scientific_dependency_complete_ansys_core_2026-08-27.stderr.log",
                "validation/revised_native_regression_2026-08-26/plotting_scientific_dependency_complete_ansys_pyvista_2026-08-27.stdout.log",
                "validation/revised_native_regression_2026-08-26/plotting_scientific_dependency_complete_ansys_pyvista_2026-08-27.stderr.log",
                "validation/revised_native_regression_2026-08-26/plotting_current_manifest_integrity_ansys_2026-08-27.stdout.log",
                "validation/revised_native_regression_2026-08-26/plotting_current_manifest_integrity_ansys_2026-08-27.stderr.log",
                "validation/revised_native_regression_2026-08-26/plotting_scientific_ansys_core_launcher_ctypes_failure_2026-08-27.stdout.log",
                "validation/revised_native_regression_2026-08-26/plotting_scientific_ansys_core_launcher_ctypes_failure_2026-08-27.stderr.log",
                "validation/revised_native_regression_2026-08-26/plotting_scientific_ansys_core_combined_order_failure_2026-08-27.stdout.log",
                "validation/revised_native_regression_2026-08-26/plotting_scientific_ansys_core_combined_order_failure_2026-08-27.stderr.log"
            )
        },
        [ordered]@{
            purpose = "fresh current-model OpenFOAM material-fidelity reduction audit"
            command = "python tools/openfoam_material_fidelity_audit.py library/models/new_model_updated.toml"
            result =
                "exit 0 under affinity mask 3/BelowNormal; 13 instances, 9 " +
                "heterogeneous instances, 55 internal solids homogenized; " +
                "OpenFOAM-minus-defined +8.99054 kg and +14811.4 J/K"
            evidence =
                "validation/revised_native_regression_2026-08-26/goal_completion_material_fidelity_2026-08-27.stdout.log"
        },
        [ordered]@{
            purpose = "read-only retained-case post-archive integrity replay"
            command = "recorded affinity-3/BelowNormal read-only PowerShell inventory and SHA-256 replay; no WSL, solver, case write, or .foam-marker touch"
            result =
                "VERIFIED: all 82 manifest rows accounted for, including the preserved " +
                "documented rejected-1.610 yPlus contaminant; root plus four ranks retain " +
                "only 0/1.5/1.6000000000000001 and all ten endpoints have 74/74 " +
                "nonempty files; preservation only, no physical gate changed"
            evidence =
                "validation/revised_openfoam_22p5mm_2026-08-25/retained_case_post_archive_integrity_2026-08-27.json"
        },
        [ordered]@{
            purpose = "superseded historical acceleration-gates source regression"
            command = [string]$accelerationRegression.command
            result =
                "SUPERSEDED by the final runner-state regression after subsequent " +
                "generated-runner state fixes; $accelerationRegressionSummary"
            evidence = @(
                [string]$accelerationRegression.stdout.path,
                [string]$accelerationRegression.stderr.path,
                $accelerationRegressionPath
            )
        },
        [ordered]@{
            purpose = "fail-closed research-lab input release-readiness decision and focused current-state replay"
            command = "python tools/model_release_readiness.py --workspace . --ledger validation/UPDATED_MODEL_ASSUMPTIONS.toml --output <create-once path>"
            result = "fresh expected exit 1 / status FAIL; 0 verified and 9 open assumptions; paired 30-test geometry/model/readiness replay passed under affinity mask 3 and BelowNormal priority; closes input traceability only, not CFD validation"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/model_release_readiness_20260827T070322145Z.json",
                "validation/revised_native_regression_2026-08-26/model_release_readiness_goal_completion_2026-08-27.json",
                "validation/revised_native_regression_2026-08-26/model_release_readiness_test_2026-08-27.stdout.log",
                "validation/revised_native_regression_2026-08-26/model_release_readiness_test_2026-08-27.stderr.log",
                "validation/revised_native_regression_2026-08-26/goal_completion_geometry_audit_2026-08-27.stdout.log",
                "validation/revised_native_regression_2026-08-26/goal_completion_static_tests_2026-08-27.stdout.log",
                "validation/revised_native_regression_2026-08-26/goal_completion_static_tests_2026-08-27.stderr.log"
            )
        },
        [ordered]@{
            purpose = "22.5-mm retained-case selector equivalence, independent 19-mm theoretical inventory, and lifecycle verification"
            command = "python tools/openfoam_stream_region_selectors.py verify-existing --case <retained already-split 22.5-mm case> --audit <create-once path> --oom-log validation/revised_openfoam_19mm_oom_2026-08-24/prepare_manual.stdout.log; python -m unittest tests.openfoam_stream_region_selectors_test"
            result =
                "PASS on the retained 1,033,200-cell 22.5-mm case: 109 selectors " +
                "and 43,603 selected cells are logically identical. The separate " +
                "2,800,980-cell 19-mm failure log inventories 2,442,454,560 bytes " +
                "of selector internal-scalar payload; that is a theoretical field " +
                "inventory, not measured RSS savings or proof the 19-mm split fits. " +
                "12/12 lifecycle tests pass; no WSL split was run."
            evidence = @(
                "validation/revised_native_regression_2026-08-26/openfoam_low_memory_selector_equivalence_2026-08-27.json",
                "validation/revised_native_regression_2026-08-26/LOW_MEMORY_OPENFOAM_REGION_PREPARATION_2026-08-27.md",
                "validation/revised_openfoam_19mm_oom_2026-08-24/prepare_manual.stdout.log"
            )
        },
        [ordered]@{
            purpose = "non-WSL custom-solver runtime attestation, staged deployment rollback, and generated-runner provenance verification"
            command = "python -m unittest tests.openfoam_semifrozen_attestation_test tests.semifrozen_solver_policy_test; generated runner provenance tests"
            result = "PASS; 29 Python policy/attestation tests with one Windows symlink-privilege skip plus exporter/provenance/thermal-outer generated-runner tests; repository-local source SHA-256 6f5b54fddb0218558dac8798915169133c66564c199e6c95778410f9a23c9ead; no OpenFOAM build or CFD run"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/openfoam_solver_runtime_attestation_nonwsl_2026-08-27.stdout.log",
                "validation/revised_native_regression_2026-08-26/OPENFOAM_SOLVER_RUNTIME_ATTESTATION_2026-08-27.md"
            )
        },
        [ordered]@{
            purpose = "superseded historical pre-regression host-only corrected-run resource snapshot"
            command = "pwsh -NoProfile -File tools/openfoam_resource_gate.ps1 -DiskPath C:\OpenFOAM -EvidencePath <create-once path>"
            result =
                "SUPERSEDED by the post-cleanup resource decision; " +
                $goalResumeResourceSummary
            evidence = $goalResumeResourcePath
        },
        [ordered]@{
            purpose = "superseded historical post-acceleration host-only corrected-run resource snapshot"
            command = "pwsh -NoLogo -NoProfile -File tools/openfoam_resource_gate.ps1 -DiskPath C:\OpenFOAM -EvidencePath <create-once path>"
            result =
                "SUPERSEDED after exact-target installer-temp cleanup; " +
                $postAccelerationResourceSummary
            evidence = $postAccelerationResourcePath
        },
        [ordered]@{
            purpose = "audited exact-target inactive installer-temp cleanup"
            command = "RECORDED EXACT-TARGET CLEANUP; see immutable JSON for resolved paths, process/service/open-file gates, scope guards, and byte counts"
            result = $tempCleanupSummary
            evidence = $tempCleanupPath
        },
        [ordered]@{
            purpose = "superseded point-in-time post-cleanup host-only resource snapshot"
            command = "pwsh -NoLogo -NoProfile -File tools/openfoam_resource_gate.ps1 -DiskPath C:\OpenFOAM -EvidencePath <create-once path>"
            result =
                "SUPERSEDED by later idle and goal-completion decisions; " +
                $postCleanupResourceSummary
            evidence = $postCleanupResourcePath
        },
        [ordered]@{
            purpose = "superseded final-idle post-regression host/WSL corrected-run resource decision"
            command = "pwsh -NoLogo -NoProfile -File tools/openfoam_resource_gate.ps1 -QueryWsl -EvidencePath <create-once path>"
            result = $finalIdleResourceSummary
            evidence = $finalIdleResourcePath
        },
        [ordered]@{
            purpose = "current goal-completion host/WSL corrected-run resource decision"
            command = "pwsh -NoLogo -NoProfile -File tools/openfoam_resource_gate.ps1 -DiskPath C:\OpenFOAM -QueryWsl -EvidencePath <create-once path>"
            result = $goalCompletionResourceSummary
            evidence = $goalCompletionResourcePath
        },
        [ordered]@{
            purpose = "superseded historical post-audit runtime-hardening source regression"
            command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\run_added_feature_tests.ps1"
            result = "SUPERSEDED; exit 0 in 295.746 s; All added-feature tests passed; stdout 332552 bytes SHA-256 36838d5811bd909116e74e9ea92c9843b1982c0d7a86f50929d2fea64480e517; stderr 2585 bytes SHA-256 1f21f65650e34384aa2720e4f2908322c134a49796d5ecc37e996adbf19d7a9e"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stdout.log",
                "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stderr.log"
            )
        },
        [ordered]@{
            purpose = "superseded historical resource-gate and 20-second-policy regression"
            command = "pwsh -NoLogo -NoProfile -ExecutionPolicy Bypass -File .\tests\run_added_feature_tests.ps1"
            result = "SUPERSEDED; exit 0 in 303.075 s wrapper time; All added-feature tests passed; stdout 332820 bytes SHA-256 1bd2cf90e5b414b992bfb7d949bc04e71371e0a5249c1b38d4bf65132dbab01b; stderr 2585 bytes SHA-256 d76ac635fbf0db9c53331abb56f7c12e0cad3fed44d7441a4fa1516bcd7a2287"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/added_feature_regression_resource_gate_2026-08-27.stdout.log",
                "validation/revised_native_regression_2026-08-26/added_feature_regression_resource_gate_2026-08-27.stderr.log"
            )
        },
        [ordered]@{
            purpose = "preserved pre-regression launcher quoting failure"
            command = "Start-Process pwsh ... -File <unquoted workspace path containing spaces>"
            result = "launcher exit 64 in 0.437 s before the harness started; retained as failed-attempt evidence and superseded by the quoted-path exit-0 run"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/added_feature_regression_resource_gate_launch_quoting_failure_2026-08-27.stdout.log",
                "validation/revised_native_regression_2026-08-26/added_feature_regression_resource_gate_launch_quoting_failure_2026-08-27.stderr.log"
            )
        },
        [ordered]@{
            purpose = "portable probe/output and OpenFOAM export safety guards in the post-audit harness"
            command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\run_added_feature_tests.ps1"
            result = "exit 0; case-folded probe filename collisions and non-portable names reject before output mutation; OpenFOAM read-only preflight validates checked face-count arithmetic and positive ambient-connected fluid volume before cleanup; geometry staging/previous-file transactions preserve prior evidence on refusal or interrupted-swap detection"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stdout.log",
                "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stderr.log"
            )
        },
        [ordered]@{
            purpose = "preserved externally interrupted portable-I/O harness before geometry-transaction repair"
            command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\run_added_feature_tests.ps1"
            result = "INTERRUPTED EXTERNALLY and incomplete, not a pass/fail result; stdout ends while building pcg_flow_test after earlier checks passed, and stderr is empty"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/added_feature_regression_portable_io_hardening_interrupted_pretransaction_fix_2026-08-26.stdout.log",
                "validation/revised_native_regression_2026-08-26/added_feature_regression_portable_io_hardening_interrupted_pretransaction_fix_2026-08-26.stderr.log"
            )
        },
        [ordered]@{
            purpose = "preserved externally interrupted harness before OpenFOAM exporter preflight repair"
            command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\run_added_feature_tests.ps1"
            result = "INTERRUPTED EXTERNALLY and incomplete, not a pass/fail result; stdout ends while building openfoam_device_report_test after exporter/generated-runner checks passed, and stderr is empty"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/added_feature_regression_interrupted_before_exporter_preflight_fix_2026-08-26.stdout.log",
                "validation/revised_native_regression_2026-08-26/added_feature_regression_interrupted_before_exporter_preflight_fix_2026-08-26.stderr.log"
            )
        },
        [ordered]@{
            purpose = "pinned historical post-audit O2 reusable-component campaign build"
            command = "& 'g++' -std=c++17 -O2 -I .\src .\tests\native_template_microcase_test.cpp -o .\validation\revised_native_regression_2026-08-26\native_template_microcase_post_audit_runtime_hardening_2026-08-26.exe"
            result = "historical compile exit 0; executable SHA-256 133355e650d7c14ff0526997eb22707a4053c59dd07a8b4d301691ec2867ebd2; not current-source identity"
            evidence = "validation/revised_native_regression_2026-08-26/native_template_microcase_post_audit_runtime_hardening_2026-08-26.exe"
        },
        [ordered]@{
            purpose = "pinned historical post-audit O2 reusable-component campaign execution"
            command = "& .\validation\revised_native_regression_2026-08-26\native_template_microcase_post_audit_runtime_hardening_2026-08-26.exe"
            result = "historical exit 0 in 74.651 s wrapper time and 74.4218 s internal time; 11 functional passes and 1 expected NI geometry rejection across 12 selected templates; stdout SHA-256 895430bcbb8750012679daea582cf71fd9d7b42d9ca5640fd4adbf7487e60bf0; not current-source identity or physical validation"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/native_template_microcase_post_audit_runtime_hardening_2026-08-26.stdout.log",
                "validation/revised_native_regression_2026-08-26/native_template_microcase_post_audit_runtime_hardening_2026-08-26.stderr.log",
                "validation/revised_native_regression_2026-08-26/native_template_microcase_post_audit_runtime_hardening_2026-08-26.exe"
            )
        },
        [ordered]@{
            purpose = "adaptive solid/air convection conservation and stability guards"
            command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\run_added_feature_tests.ps1"
            result = "exit 0; 12/12 conservation/property cases and 8/8 safe/unsafe stability cases passed"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stdout.log",
                "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stderr.log"
            )
        },
        [ordered]@{
            purpose = "native discrete frozen-capacity energy/update ledger"
            command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\run_added_feature_tests.ps1"
            result = "exit 0; uniform and adaptive 4-step ledgers closed source, solid/fluid conduction, solid-air convection, x-directed face-flux advection, ambient exchange, and frozen-capacity storage to below 1.5e-11 J"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stdout.log",
                "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stderr.log"
            )
        },
        [ordered]@{
            purpose = "NI separator mesh ladder and vent-tunnel controls"
            command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\run_added_feature_tests.ps1"
            result = "exit 0; seven spacings and six controls per spacing isolate the invariant 0.000018 m^3 wall-1 loss to the provisional wall-3/top-right-vent topology"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stdout.log",
                "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stderr.log"
            )
        },
        [ordered]@{
            purpose = "semi-frozen OpenFOAM invocation-mode policy in the post-audit harness"
            command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\run_added_feature_tests.ps1"
            result = "exit 0; 10/10 static source-policy tests passed, including the deployed-binary marker contract"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stdout.log",
                "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stderr.log"
            )
        },
        [ordered]@{
            purpose = "generated TEMP-case runner guard regressions within the post-audit harness"
            command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\run_added_feature_tests.ps1"
            result = "exit 0; outer-corrector overrides 0 and 1 reject before launch/write and restore live count 3; missing and stale deployed binaries reject with exit 14 before case locking or writes"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stdout.log",
                "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stderr.log"
            )
        },
        [ordered]@{
            purpose = "fail-closed OpenFOAM resource-gate synthetic and production-wrapper regression"
            command = "pwsh -NoLogo -NoProfile -File .\tests\openfoam_resource_gate_test.ps1"
            result = "exit 0; host/process/memory/paging/disk/WSL/post-WSL invalidation, immutable evidence, production floors, structured failure, and no-real-WSL gates passed"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/openfoam_resource_gate_synthetic_2026-08-27.stdout.log",
                "tests/openfoam_resource_gate_test.ps1",
                "tools/openfoam_resource_gate.ps1"
            )
        },
        [ordered]@{
            purpose = "superseded historical host-only pre-export OpenFOAM resource snapshot"
            command = "pwsh -NoLogo -NoProfile -File .\tools\openfoam_resource_gate.ps1 -DiskPath C:\OpenFOAM -EvidencePath .\validation\revised_native_regression_2026-08-26\openfoam_resource_gate_pre_export_20260827T0038MDT.json"
            result =
                "SUPERSEDED by later resource snapshots; " +
                $preExportResourceSummary
            evidence = $preExportResourcePath
        },
        [ordered]@{
            purpose = "superseded historical first host disk cleanup without thermal-model evidence deletion"
            command = "RECORDED EXACT-TARGET WINDOWS TEMP CLEANUP; see the hashed audit for resolved paths, process gate, byte counts, exclusions, and post-cleanup state"
            result = "HISTORICAL cleanup before the later immutable three-target cleanup; six inactive Visual Studio installer extraction directories removed; 8215369245 logical bytes; C free space increased to 16600.8 MiB; active diagnostics and all model/OpenFOAM/validation state preserved"
            evidence = "validation/revised_native_regression_2026-08-26/HOST_RESOURCE_CLEANUP_2026-08-27.md"
        },
        [ordered]@{
            purpose = "installed OpenFOAM custom-solver provenance audit"
            command = "RECORDED ONE-OFF WSL AUDIT; the original exact shell command was not preserved, so use the hashed evidence as a historical capture rather than a replay recipe"
            result = "installed binary SHA-256 4dad80e5f4c3b4a9a37599d06291dd8c93cdf9a49053c9a2582f90cd5a5c2e6d contains historical isothermal strings and lacks the hardened marker; do not run"
            evidence = "validation/revised_native_regression_2026-08-26/installed_openfoam_solver_provenance_2026-08-26.stdout.log"
        },
        [ordered]@{
            purpose = "recorded historical provisional-NI minimum-separator geometry-only export"
            command = "RECORDED HISTORICAL COMMAND (do not replay with the current workspace binary): & .\model.exe --geometry-only .\library\models\validation_ni_minimum_separator_sensitivity.toml .\library\fan_curves\fan_curves_updated_2026_08_24.toml"
            result = "historical exit 0; geometry exported without running mesh or transient solvers; no current-source production exporter binary exists"
            evidence = "validation/ni_minimum_separator_sensitivity_2026-08-26/geometry_only.stdout.log"
        },
        [ordered]@{
            purpose = "provisional NI minimum-separator guard in the pinned historical post-audit component campaign"
            command = "& .\validation\revised_native_regression_2026-08-26\native_template_microcase_post_audit_runtime_hardening_2026-08-26.exe"
            result = "exit 0; expected fail-closed geometry rejection because Card Slot wall 1 requested 0.000192 m^3 but realized 0.000174 m^3 after overlay/opening stamping"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/native_template_microcase_post_audit_runtime_hardening_2026-08-26.stdout.log",
                "validation/revised_native_regression_2026-08-26/native_template_microcase_post_audit_runtime_hardening_2026-08-26.stderr.log",
                "validation/revised_native_regression_2026-08-26/native_template_microcase_post_audit_runtime_hardening_2026-08-26.exe"
            )
        },
        [ordered]@{
            purpose = "recorded component/rack audit plus post-audit source geometry-contract regression"
            command = "python .\tools\audit_component_geometry.py .\library\models\new_model_updated.toml"
            result = "recorded audit exit 0 with 11 reusable components, 0 errors, and 1 expected NI air/air warning; post-audit source geometry-contract tests pass in the complete harness"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/geometry_audit_canonical_hardening_2026-08-26.stdout.log",
                "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stdout.log",
                "validation/revised_native_regression_2026-08-26/added_feature_regression_post_audit_runtime_hardening_2026-08-26.stderr.log"
            )
        },
        [ordered]@{
            purpose = "geometry and unified-air-color plot regression"
            command = "& 'C:\Program Files\ANSYS Inc\ANSYS Student\v261\CEI\apex261\machines\win64\Python-3.12.11\python.exe' .\tests\plot_geometry_test.py -v"
            result = "exit 0; 10/10 tests passed, including artist-level same-color verification"
            evidence = "validation/ni_minimum_separator_sensitivity_2026-08-26/plot_geometry_ansys_python_2026-08-26.stdout.log"
        },
        [ordered]@{
            purpose = "actual provisional-NI plot artist verification"
            command = "& 'C:\Program Files\ANSYS Inc\ANSYS Student\v261\CEI\apex261\machines\win64\Python-3.12.11\python.exe' .\validation\ni_minimum_separator_sensitivity_2026-08-26\verify_actual_ni_air_artists.py"
            result = "exit 0; 17 regions drawn and both Air artists use identical tab:cyan fill and edge colors"
            evidence = "validation/ni_minimum_separator_sensitivity_2026-08-26/verify_actual_ni_air_artists_2026-08-26.stdout.log"
        },
        [ordered]@{
            purpose = "recorded historical post-audit geometry-only binary check"
            command = "RECORDED HISTORICAL COMMAND (do not replay as current-source evidence): & .\model.exe --geometry-only"
            result = "historical exit 0 in 0.289 s; executable 1838877 bytes SHA-256 335efd17d7f000e90d13b4a2594cfade4ab68bde524b2910f81c73df7b00eb12; canonical output 39808 bytes SHA-256 dbcaaa237fb8bf59c4841cf4e3a8b0a226d28208d23560c72900ce113d2ff2ea; mesh and transient solvers were not run; no current-source production exporter binary exists"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/geometry_only_post_audit_runtime_hardening_2026-08-26.stdout.log",
                "validation/revised_native_regression_2026-08-26/geometry_only_post_audit_runtime_hardening_2026-08-26.stderr.log",
                "validation/revised_native_regression_2026-08-26/model_post_audit_runtime_hardening_2026-08-26.exe",
                "model.exe",
                "output.txt"
            )
        },
        [ordered]@{
            purpose = "recorded historical post-audit canonical full-rack native fail-closed workload preflight"
            command = "RECORDED HISTORICAL COMMAND (do not replay as current-source evidence): & .\model.exe --native"
            result = "historical expected exit 1 in 0.109 s before mesh allocation; 2847663 fine cells require 1708597800 minimum visits and 216580 coarse cells require 129948000 minimum visits, for 1838545800 minimum total visits versus simulation.max_updates 30000000; no current-source production exporter binary exists"
            evidence = @(
                "validation/revised_native_regression_2026-08-26/full_rack_native_preflight_post_audit_runtime_hardening_2026-08-26.stdout.log",
                "validation/revised_native_regression_2026-08-26/full_rack_native_preflight_post_audit_runtime_hardening_2026-08-26.stderr.log",
                "validation/revised_native_regression_2026-08-26/model_post_audit_runtime_hardening_2026-08-26.exe",
                "model.exe"
            )
        },
        [ordered]@{
            purpose = "manifest generation"
            command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\create_runtime_acceleration_manifest.ps1 -OutputPath '$manifestRelative'"
            result = "generated and verified by per-file byte count and SHA-256"
            evidence = $manifestRelative
        }
    )
    results = [ordered]@{
        software_verification = [ordered]@{
            full_harness =
                "LAST COMPLETE PRODUCTION-SOURCE RUN, not a present-test-tree pass: " +
                "$finalRegressionSummary; the inline-shelf C++ microcase and default " +
                "component-campaign registration were added later and remain uncompiled"
            superseded_harness_snapshots =
                "Historical acceleration-gates, resource-gate/20-second-policy, and " +
                "post-audit regression rows remain in commands with immutable evidence; " +
                "none proves the present test tree."
            openfoam_resource_gate =
                "PASS synthetic/adversarial suite and production-wrapper checks with " +
                "realWslCalls=0; current goal-completion decision: " +
                $goalCompletionResourceSummary
            adaptive_convection_conservation = "PASS 12/12"
            convection_stability_guard = "PASS 8/8"
            native_thermal_energy_ledger =
                "PASS; uniform and adaptive 4-step discrete frozen-capacity energy/update " +
                "ledgers close to below 1.5e-11 J; all-axis convection is covered by its " +
                "separate 12-case conservation suite"
            openfoam_invocation_mode_policy =
                "PASS 10/10 static source-policy tests; installed binary is proven stale " +
                "and a rebuilt marker-bearing binary has not been runtime-proven"
            generated_runner_guards =
                "PASS in the last complete production-source harness; capped exact-endpoint tolerance, bounded warm " +
                "windows with per-window Courant postflight, nonnegative finite Courant " +
                "validation on restart, partial/final fan-ramp recovery with marker-last " +
                "publication and fail-closed unsafe states, atomic owed-to-active airflow " +
                "refresh commit, marker-last/marker-first accepted-reference guards, full " +
                "production control restoration including deltaT, and deferred tracked " +
                "process-group INT/TERM handling with bounded TERM/KILL escalation are covered"
            generated_case_bash_syntax =
                "PASS in the last complete harness (bashSyntax=pass) and in the final pre-full " +
                "current-source sweep; the generated script itself is regenerated in TEMP"
            native_component_campaign =
                "CURRENT SOURCE UNCOMPILED/UNRUN due sub-1-GiB memory; Python shelf/model " +
                "contract PASS 17/17. PINNED HISTORICAL PASS, not current-source identity: " +
                "74.651 s wrapper " +
                "time and 74.4218 s internal time; 11 " +
                "functional passes, 1 expected NI geometry rejection, 12 selected; " +
                "executable SHA-256 " +
                "133355e650d7c14ff0526997eb22707a4053c59dd07a8b4d301691ec2867ebd2; " +
                "stdout SHA-256 " +
                "895430bcbb8750012679daea582cf71fd9d7b42d9ca5640fd4adbf7487e60bf0"
            native_full_rack_preflight =
                "EXPECTED FAIL-CLOSED in 0.109 s; 2847663 fine and 216580 coarse " +
                "cells require 1838545800 minimum visits, exceeding 30000000 before " +
                "mesh allocation; no full-rack native flow or temperature field"
            portable_probe_output_paths =
                "PASS; portable probe names are validated and case-folded filename " +
                "collisions reject before legacy or structured CSV output mutation"
            openfoam_export_preflight =
                "PASS; read-only preflight validates checked face-count arithmetic and " +
                "requires positive ambient-connected fluid volume before cleanup or writes"
            openfoam_geometry_transaction =
                "PASS; a previous-file recovery guard preserves the existing checkpoint; " +
                "overwrite=false read-only refusal preserves geometry/checkpoint bytes and " +
                "creates neither a staging nor a previous transaction file"
            lazy_output_preservation =
                "PASS; duplicate, non-portable, and case-folded-colliding probes, exact " +
                "workload refusals, and OpenFOAM geometry refusals preserve existing " +
                "outputs before advancement"
            geometry_audit = "PASS with one expected unfinished-NI air/air warning"
            plot_geometry =
                "PASS 77/77 dependency-complete core tests plus 23/23 current artifact " +
                "integrity checks under ANSYS Python; rendered component/rack artifacts " +
                "and actual two-Air artist equality independently verified"
            ni_separator_sensitivity =
                "PROVISIONAL and inactive; seven-spacing ladder proves the 0.000018 m^3 " +
                "wall-1 loss is caused by the added wall 3 sealing the canonical corridor " +
                "and the top-right vent tunnel, not by missing source cuts alone"
            ni_separator_plot =
                "RENDERED and hashed; both actual Air-region artists use tab:cyan. The plot " +
                "is nominal geometry evidence, not stamped-topology or flow validation."
            explicit_harness_skips = @(
                "PyVista GIF/convergence/hotspot and mesh runtime requiring missing vtkmodules/imageio",
                "real WSL flock lifetime integration in the non-escalated harness environment"
            )
        }
        runtime_decisions = [ordered]@{
            initial_airflow_timestep_s = 0.0005
            initial_airflow_timestep_decision = "retain; 0.00075 s was 9.79% slower and failed field/fan equivalence"
            screening_refresh_timestep_s = 0.001
            screening_refresh_timestep_decision = "retain as an independent later-refresh cap; it does not enlarge the unfinished initial-airflow continuation"
            exact_legacy_thermal_cap_s = 24
            exact_legacy_thermal_cap_decision = "exploratory screening only; 22.190252% cumulative and 13.725195% sustained speed gains versus 20 s, but strict temperature gates failed"
            reusable_screening_thermal_cap_s = 20
            reusable_export_fixture_thermal_cap_s = 20
            reusable_export_fixture_thermal_cap_decision = "reset from the rejected 24 s exact-case experiment; current and corrected exports cannot promote 24 s"
            thermal_only_outer_candidate = [ordered]@{
                status = "SCREENING CANDIDATE ONLY"
                outer_correctors = 2
                qualification =
                    "Corrected-full-rack 3-versus-2 A/B remains unmeasured; use 3 for " +
                    "quantitative runs until that acceptance test passes."
            }
            live_outer_correctors = 3
            resource_gate = $goalCompletionResourceSummary
            heavy_openfoam_run_launched = $false
        }
        current_case_limits = @(
            "The retained 1.600 s state has about 0.875 nominal air replacements and 8.8677% whole-fluid velocity RMS drift, so it is not eligible for frozen-flow thermal acceleration.",
            "The retained 22.5 mm case predates corrected geometry and has 39,878 below-threshold determinant cells.",
            "Selector equivalence was measured only on the retained 1,033,200-cell 22.5 mm case; the 2,442,454,560-byte value for the 2,800,980-cell 19 mm failure is a theoretical internal-scalar inventory, not measured RSS savings or proof the split fits memory.",
            "The canonical NI chassis remains active and unchanged; its minimum-separator sensitivity is provisional, inactive, and intentionally rejected by native geometry stamping pending measurements and vent topology.",
            "The exact native ledger verifies the solver's discrete beginning-of-step frozen rho*cp*V storage convention; it is not endpoint-density thermodynamics, a long-transient accuracy result, radiation validation, or OpenFOAM validation.",
            "The current component campaign source adds the exact inline storage shelf and is registered in the default harness, but its O2 compile and execution are unproven under the sub-1-GiB memory decision; the last full harness therefore predates the present test tree.",
            "The configured shelf is a full 0.024503013493875 m3 aluminum envelope, analytically 66.1581364 kg and 59542.3228 J/K; this is a provisional conservative obstruction, not measured construction.",
            "The installed OpenFOAM custom solver is definitely stale: it lacks the required policy marker and contains historical isothermal-mode strings. The source snapshot hashed by this manifest has not been rebuilt and runtime-proven; the current goal-completion host gate failed on 379,506,688 available bytes versus the mandatory 5,368,709,120-byte floor before WSL launch.",
            "No runtime control in this manifest is an industry-readiness or validation sign-off."
        )
    }
    file_summary = [ordered]@{
        file_count = $evidenceRows.Count
        total_bytes = $totalBytes
        ordering = "workspace-relative paths, forward slashes, ordinal ascending"
        digest = "SHA-256 lowercase hexadecimal"
    }
    authoritative_files = $evidenceRows
}

$manifestDirectory = Split-Path -Parent $manifestFull
if (-not (Test-Path -LiteralPath $manifestDirectory -PathType Container)) {
    [void](New-Item -ItemType Directory -Path $manifestDirectory)
}
$json = $manifest | ConvertTo-Json -Depth 12
[IO.File]::WriteAllText(
    $manifestFull,
    $json + "`n",
    [Text.UTF8Encoding]::new($false))

$verification = Assert-ManifestValid `
    -Root $workspaceFull `
    -ManifestFullPath $manifestFull
$verification | ConvertTo-Json -Depth 4
