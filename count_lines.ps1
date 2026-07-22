# count_lines.ps1

# Skip any file named toml.hpp
$files = Get-ChildItem -Recurse -Include *.hpp, *.cpp, *.py, *.ps1 -Exclude "toml.hpp"

if ($files.Count -eq 0) {
    Write-Host "No .hpp, .cpp, .py, or .ps1 files found (excluding toml.hpp)."
    exit
}

$total = 0
Write-Host "Line counts by file:"
Write-Host "---------------------"

foreach ($f in $files) {
    $count = (Get-Content $f.FullName | Measure-Object -Line).Lines
    Write-Host "$count  $($f.FullName)"
    $total += $count
}

Write-Host "---------------------"
Write-Host "Total lines: $total"