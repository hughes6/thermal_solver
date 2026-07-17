# count_lines.ps1

$files = Get-ChildItem -Recurse -Include *.hpp, *.cpp, *.py, *.ps1

if ($files.Count -eq 0) {
    Write-Host "No .hpp, .cpp, or .py files found."
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
