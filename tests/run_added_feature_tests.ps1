$ErrorActionPreference = "Stop"

$tests = @(
    "pcg_flow_test",
    "advection_subcycling_test",
<<<<<<< Updated upstream
=======
    "multithreading_test",
    "openfoam_export_test",
>>>>>>> Stashed changes
    "face_wall_test",
    "model_config_test"
)

foreach ($test in $tests) {
    Write-Host "Building $test"
    & g++ -std=c++17 -O2 -I . "tests/$test.cpp" -o "tests/$test.exe"
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
    "tests/coarse_heat_io_test.py"
if ($LASTEXITCODE -ne 0) {
    throw "Python plotting syntax check failed"
}

& python "tests/coarse_heat_io_test.py"
if ($LASTEXITCODE -ne 0) {
    throw "Coarse heat input tests failed"
}

Write-Host "All added-feature tests passed."
