$ErrorActionPreference = "Stop"

$tests = @(
    "air_heat_source_parser_test",
    "openfoam_export_test",
    "openfoam_device_report_test",
    "openfoam_connected_volume_test",
    "openfoam_profile_options_test",
    "pcg_flow_test",
    "advection_subcycling_test",
    "face_wall_test",
    "model_config_test",
    "model_runner_tracer_commands_test"
)

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
        & $testExe
        if ($LASTEXITCODE -ne 0) {
            throw "Test failed: $test"
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
    "plot/coarse_heat_animation.py" `
    "plot/coarse_heat_io.py" `
    "plot/plot.py" `
    "tools/fan_curve_fitter.py" `
    "tools/heat_load_estimator.py" `
    "tools/openfoam_field_convergence.py" `
    "tools/openfoam_field_delta.py" `
    "tools/openfoam_cross_case_comparison.py" `
    "tools/openfoam_mesh_comparison.py" `
    "tools/openfoam_progress.py" `
    "tools/map_openfoam_case.py" `
    "tools/exhaust_recirculation_tracer.py" `
    "plot/exhaust_recirculation_matrix.py" `
    "tests/coarse_heat_io_test.py" `
    "tests/engineering_tools_test.py" `
    "tests/plot_geometry_test.py" `
    "tests/openfoam_animation_test.py" `
    "tests/openfoam_field_convergence_test.py" `
    "tests/openfoam_field_delta_test.py" `
    "tests/openfoam_cross_case_comparison_test.py" `
    "tests/openfoam_mesh_comparison_test.py" `
    "tests/map_openfoam_case_test.py" `
    "tests/openfoam_profile_policy_test.py" `
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

& python "tests/openfoam_animation_test.py"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM animation tests failed"
}

& python -m unittest "tests.recirculation_report_test"
if ($LASTEXITCODE -ne 0) {
    throw "Recirculation reporting tests failed"
}

& python -m unittest "tests.map_openfoam_case_test"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM mapping workflow tests failed"
}

& python -m unittest "tests.openfoam_field_convergence_test"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM field convergence tests failed"
}

& python -m unittest "tests.openfoam_field_delta_test"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM binary field delta tests failed"
}

& python -m unittest "tests.openfoam_cross_case_comparison_test"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM cross-case comparison tests failed"
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

& python -m unittest "tests.semifrozen_solver_policy_test"
if ($LASTEXITCODE -ne 0) {
    throw "Semi-frozen solver policy tests failed"
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
