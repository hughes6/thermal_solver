$ErrorActionPreference = "Stop"

$tests = @(
    "air_heat_source_parser_test",
    "openfoam_export_test",
    "openfoam_device_report_test",
    "openfoam_connected_volume_test",
    "openfoam_profile_options_test",
    "pcg_flow_test",
    "boundary_fan_curve_discretization_test",
    "internal_fan_face_topology_test",
    "internal_fan_thermal_transfer_test",
    "flow_network_grounding_test",
    "fan_active_set_test",
    "flow_nonlinear_convergence_test",
    "porous_region_test",
    "advection_subcycling_test",
    "mesh_preflight_guard_test",
    "adaptive_convection_conservation_test",
    "convection_stability_guard_test",
    "ni_separator_mesh_ladder_test",
    "native_thermal_energy_ledger_test",
    "native_multiaxis_persistent_ledger_test",
    "native_template_microcase_test",
    "solver_logger_timestep_test",
    "face_wall_test",
    "model_config_test",
    "model_runner_tracer_commands_test",
    "run_metadata_test"
)

# tests/native_template_microcase_test.cpp is intentionally part of this
# current-source O2 harness even though it is a longer model campaign rather
# than a fast unit test. It exercises all 11 canonical reusable templates, the
# exact inline storage-shelf object from new_model_updated.toml, and the
# deliberately inactive provisional NI separator as an expected rejection.
# These are bounded numerical topology/solver screens, not physical calibration,
# thermal-soak, mesh-independence, or full-rack interaction evidence.

$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testBinDir = [IO.Path]::GetFullPath(
    (Join-Path $tempBase ("thermal_solver_added_feature_tests_" + [guid]::NewGuid().ToString("N"))))
if (-not $testBinDir.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe test executable directory: $testBinDir"
}
[void](New-Item -ItemType Directory -Path $testBinDir)

try {
    foreach ($test in $tests) {
        $testExe = Join-Path $testBinDir "$test.exe"
        Write-Host "Building $test in $testBinDir"
        & g++ -std=c++17 -O2 -I src "tests/$test.cpp" -o $testExe
        if ($LASTEXITCODE -ne 0) {
            throw "Compilation failed: $test"
        }

        Write-Host "Running $test"
        if ($test -eq "openfoam_export_test") {
            $generatedRunnerCase = Join-Path $testBinDir "generated_runner_case"
            & $testExe $generatedRunnerCase
        }
        else {
            & $testExe
        }
        if ($LASTEXITCODE -ne 0) {
            throw "Test failed: $test"
        }
        if ($test -eq "openfoam_export_test") {
            Write-Host "Running generated thermal-only outer-corrector guards"
            & (Join-Path $PSScriptRoot "generated_runner_thermal_outer_test.ps1") `
                -CasePath $generatedRunnerCase
            if ($LASTEXITCODE -ne 0) {
                throw "Generated runner thermal-only outer-corrector test failed"
            }
            Write-Host "Running generated custom-solver provenance guards"
            & (Join-Path $PSScriptRoot "generated_runner_solver_provenance_test.ps1") `
                -CasePath $generatedRunnerCase
            if ($LASTEXITCODE -ne 0) {
                throw "Generated runner custom-solver provenance test failed"
            }
            Write-Host "Running generated timestep-policy guards"
            & (Join-Path $PSScriptRoot "generated_runner_timestep_policy_test.ps1") `
                -CasePath $generatedRunnerCase
            if ($LASTEXITCODE -ne 0) {
                throw "Generated runner timestep-policy test failed"
            }
        }
    }
}
finally {
    if (Test-Path -LiteralPath $testBinDir) {
        $cleanupTarget = [IO.Path]::GetFullPath($testBinDir)
        if (-not $cleanupTarget.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -or
            -not ([IO.Path]::GetFileName($cleanupTarget)).StartsWith(
                "thermal_solver_added_feature_tests_", [StringComparison]::Ordinal)) {
            throw "Refusing unsafe test executable cleanup: $cleanupTarget"
        }
        Remove-Item -LiteralPath $cleanupTarget -Recurse -Force
    }
}

$pythonCacheDir = [IO.Path]::GetFullPath(
    (Join-Path $tempBase ("thermal_solver_python_cache_" + [guid]::NewGuid().ToString("N"))))
if (-not $pythonCacheDir.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe Python cache directory: $pythonCacheDir"
}
$previousPythonCachePrefix = $env:PYTHONPYCACHEPREFIX
[void](New-Item -ItemType Directory -Path $pythonCacheDir)
$env:PYTHONPYCACHEPREFIX = $pythonCacheDir

try {
Write-Host "Checking Python plotting scripts"
& python -m py_compile `
    "plot/heat_animation.py" `
    "plot/fluid_results.py" `
    "plot/run_metadata.py" `
    "plot/coarse_heat_animation.py" `
    "plot/coarse_heat_io.py" `
    "plot/plot.py" `
    "plot/plot_component.py" `
    "tools/render_component_plots.py" `
    "tools/fan_curve_fitter.py" `
    "tools/rack_system_curve.py" `
    "tools/heat_load_estimator.py" `
    "tools/porous_obstruction_calculator.py" `
    "tools/openfoam_field_convergence.py" `
    "tools/openfoam_field_delta.py" `
    "tools/openfoam_cross_case_comparison.py" `
    "tools/openfoam_temperature_comparison.py" `
    "tools/openfoam_mesh_comparison.py" `
    "tools/openfoam_progress.py" `
    "tools/openfoam_partition_audit.py" `
    "tools/openfoam_boundary_mass_balance_audit.py" `
    "tools/openfoam_fan_flow_audit.py" `
    "tools/openfoam_fan_operating_domain_audit.py" `
    "tools/openfoam_component_report.py" `
    "tools/openfoam_heat_source_audit.py" `
    "tools/openfoam_transient_energy_audit.py" `
    "tools/model_release_readiness.py" `
    "tools/openfoam_semifrozen_attestation.py" `
    "tools/openfoam_stream_region_selectors.py" `
    "tools/openfoam_yplus_report.py" `
    "tools/map_openfoam_case.py" `
    "tools/exhaust_recirculation_tracer.py" `
    "plot/exhaust_recirculation_matrix.py" `
    "tests/coarse_heat_io_test.py" `
    "tests/engineering_tools_test.py" `
    "tests/porous_obstruction_calculator_test.py" `
    "tests/rack_system_curve_test.py" `
    "tests/plot_geometry_test.py" `
    "tests/openfoam_partition_audit_test.py" `
    "tests/test_openfoam_boundary_mass_balance_audit.py" `
    "tests/openfoam_fan_flow_audit_test.py" `
    "tests/openfoam_fan_operating_domain_audit_test.py" `
    "tests/component_geometry_audit_test.py" `
    "tests/updated_lab_model_contract_test.py" `
    "tests/openfoam_animation_test.py" `
    "tests/fluid_results_test.py" `
    "tests/run_metadata_test.py" `
    "tests/openfoam_field_convergence_test.py" `
    "tests/openfoam_field_delta_test.py" `
    "tests/openfoam_cross_case_comparison_test.py" `
    "tests/openfoam_temperature_comparison_test.py" `
    "tests/openfoam_mesh_comparison_test.py" `
    "tests/map_openfoam_case_test.py" `
    "tests/openfoam_profile_policy_test.py" `
    "tests/openfoam_component_report_test.py" `
    "tests/openfoam_heat_source_audit_test.py" `
    "tests/openfoam_transient_energy_audit_test.py" `
    "tests/model_release_readiness_test.py" `
    "tests/openfoam_semifrozen_attestation_test.py" `
    "tests/openfoam_stream_region_selectors_test.py" `
    "tests/openfoam_yplus_report_test.py" `
    "tests/exhaust_recirculation_tracer_test.py" `
    "tests/exhaust_recirculation_matrix_plot_test.py" `
    "tools/validate_openfoam_case.py" `
    "plot_outlet_flow.py" `
    "plot/recirculation_report.py" `
    "tests/recirculation_report_test.py" `
    "tests/openfoam_validation_test.py" `
    "tests/semifrozen_solver_policy_test.py"
if ($LASTEXITCODE -ne 0) {
    throw "Python plotting syntax check failed"
}

& python "tests/coarse_heat_io_test.py"
if ($LASTEXITCODE -ne 0) {
    throw "Coarse heat input tests failed"
}

& python "tests/engineering_tools_test.py"
if ($LASTEXITCODE -ne 0) {
    throw "Engineering utility tests failed"
}

& python "tests/plot_geometry_test.py"
if ($LASTEXITCODE -ne 0) {
    throw "Plot geometry tests failed"
}

& python -m unittest "tests.openfoam_partition_audit_test"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM partition audit tests failed"
}

& python -m unittest "tests.test_openfoam_boundary_mass_balance_audit"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM boundary mass-balance audit tests failed"
}

& python -m unittest `
    "tests.openfoam_fan_flow_audit_test" `
    "tests.openfoam_fan_operating_domain_audit_test"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM fan operating-point audit tests failed"
}

& python -m unittest "tests.component_geometry_audit_test"
if ($LASTEXITCODE -ne 0) {
    throw "Installed component geometry-audit tests failed"
}

& python -m unittest "tests.updated_lab_model_contract_test"
if ($LASTEXITCODE -ne 0) {
    throw "Updated lab model contract tests failed"
}

& python "tests/openfoam_animation_test.py"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM animation tests failed"
}

& python -m unittest "tests.porous_obstruction_calculator_test"
if ($LASTEXITCODE -ne 0) {
    throw "Porous obstruction calculator tests failed"
}

& python -m unittest "tests.rack_system_curve_test"
if ($LASTEXITCODE -ne 0) {
    throw "Rack system pressure-curve tests failed"
}

& python "tests/fluid_results_test.py"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM fluid-results viewer tests failed"
}

& python "tests/run_metadata_test.py"
if ($LASTEXITCODE -ne 0) {
    throw "Last-run metadata tests failed"
}

& python -m unittest "tests.recirculation_report_test"
if ($LASTEXITCODE -ne 0) {
    throw "Recirculation reporting tests failed"
}

& python -m unittest "tests.map_openfoam_case_test"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM mapping workflow tests failed"
}

$numpyDeps = & python -c `
    "import importlib.util; print('available' if importlib.util.find_spec('numpy') else 'missing')"
if ($LASTEXITCODE -ne 0) {
    throw "NumPy dependency probe failed"
}
if ($numpyDeps -eq "available") {
    & python -m unittest "tests.openfoam_field_convergence_test"
    if ($LASTEXITCODE -ne 0) {
        throw "OpenFOAM field convergence tests failed"
    }
}
else {
    Write-Host "Skipping OpenFOAM field convergence tests: optional NumPy dependency is unavailable."
}

& python -m unittest "tests.openfoam_field_delta_test"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM binary field delta tests failed"
}

if ($numpyDeps -eq "available") {
    & python -m unittest "tests.openfoam_cross_case_comparison_test"
    if ($LASTEXITCODE -ne 0) {
        throw "OpenFOAM cross-case comparison tests failed"
    }
    & python -m unittest "tests.openfoam_temperature_comparison_test"
    if ($LASTEXITCODE -ne 0) {
        throw "OpenFOAM temperature comparison tests failed"
    }
}
else {
    Write-Host "Skipping OpenFOAM cross-case comparison tests: optional NumPy dependency is unavailable."
}

$meshComparisonDeps = & python -c `
    "import importlib.util; print('available' if all(importlib.util.find_spec(name) for name in ('numpy', 'pyvista')) else 'missing')"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM mesh comparison dependency probe failed"
}
if ($meshComparisonDeps -eq "available") {
    & python -m unittest "tests.openfoam_mesh_comparison_test"
    if ($LASTEXITCODE -ne 0) {
        throw "OpenFOAM mesh comparison tests failed"
    }
}
else {
    Write-Host "Skipping OpenFOAM mesh comparison tests: optional NumPy/PyVista dependencies are unavailable."
}

& python -m unittest "tests.openfoam_profile_policy_test"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM profile policy tests failed"
}

& python -m unittest "tests.openfoam_progress_test"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM progress reporting tests failed"
}

& python -m unittest "tests.exhaust_recirculation_tracer_test"
if ($LASTEXITCODE -ne 0) {
    throw "Exhaust recirculation tracer tests failed"
}

& python -m unittest "tests.openfoam_validation_test"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM numerical validation tests failed"
}

& python -m unittest "tests.openfoam_transient_energy_audit_test"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM transient first-law audit tests failed"
}

& python -m unittest "tests.model_release_readiness_test"
if ($LASTEXITCODE -ne 0) {
    throw "Research-lab model release-readiness tests failed"
}

& python -m unittest "tests.openfoam_stream_region_selectors_test"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM bounded-memory region-selector tests failed"
}

& python -m unittest "tests.openfoam_semifrozen_attestation_test"
if ($LASTEXITCODE -ne 0) {
    throw "Semi-frozen solver runtime-attestation tests failed"
}

& python -m unittest "tests.semifrozen_solver_policy_test"
if ($LASTEXITCODE -ne 0) {
    throw "Semi-frozen solver policy tests failed"
}

$gitBash = "C:\Program Files\Git\bin\bash.exe"
if (-not (Test-Path -LiteralPath $gitBash -PathType Leaf)) {
    throw "Git Bash is required for OpenFOAM helper syntax checks: $gitBash"
}
foreach ($helperScript in @(
    "tools/build_openfoam_semifrozen_solver.sh",
    "tools/prepare_openfoam_regions_low_memory.sh")) {
    & $gitBash -n $helperScript
    if ($LASTEXITCODE -ne 0) {
        throw "OpenFOAM helper shell syntax check failed: $helperScript"
    }
}

& pwsh -NoLogo -NoProfile -File (Join-Path $PSScriptRoot 'archive_openfoam_checkpoint_test.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM checkpoint archive helper tests failed"
}

& pwsh -NoLogo -NoProfile -File (Join-Path $PSScriptRoot 'openfoam_resource_gate_test.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM resource gate synthetic tests failed"
}
}
finally {
    if ($null -eq $previousPythonCachePrefix) {
        Remove-Item Env:PYTHONPYCACHEPREFIX -ErrorAction SilentlyContinue
    }
    else {
        $env:PYTHONPYCACHEPREFIX = $previousPythonCachePrefix
    }
    if (Test-Path -LiteralPath $pythonCacheDir) {
        $cleanupTarget = [IO.Path]::GetFullPath($pythonCacheDir)
        if (-not $cleanupTarget.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -or
            -not ([IO.Path]::GetFileName($cleanupTarget)).StartsWith(
                "thermal_solver_python_cache_", [StringComparison]::Ordinal)) {
            throw "Refusing unsafe Python cache cleanup: $cleanupTarget"
        }
        Remove-Item -LiteralPath $cleanupTarget -Recurse -Force
    }
}

Write-Host "All added-feature tests passed."
