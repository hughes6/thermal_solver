$ErrorActionPreference = "Stop"

$tests = @(
    "pcg_flow_test",
    "advection_subcycling_test",
    "face_wall_test",
    "model_config_test"
)

foreach ($test in $tests) {
    Write-Host "Building $test"
    & g++ -std=c++17 -O2 -I src "tests/$test.cpp" -o "tests/$test.exe"
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed: $test"
    }

    Write-Host "Running $test"
    & "tests/$test.exe"
    if ($LASTEXITCODE -ne 0) {
        throw "Test failed: $test"
    }
}

Write-Host "Checking Python plotting scripts"
& python -m py_compile `
    "plot/heat_animation.py" `
    "plot/coarse_heat_animation.py" `
    "plot/coarse_heat_io.py" `
    "plot/plot.py" `
    "tools/fan_curve_fitter.py" `
    "tools/heat_load_estimator.py" `
    "tests/coarse_heat_io_test.py" `
    "tests/engineering_tools_test.py" `
    "tests/plot_geometry_test.py" `
    "tests/openfoam_animation_test.py" `
    "tools/validate_openfoam_case.py" `
    "plot_outlet_flow.py" `
    "plot/recirculation_report.py" `
    "tests/openfoam_validation_test.py"
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

& python -m unittest "tests.openfoam_validation_test"
if ($LASTEXITCODE -ne 0) {
    throw "OpenFOAM numerical validation tests failed"
}

Write-Host "All added-feature tests passed."
