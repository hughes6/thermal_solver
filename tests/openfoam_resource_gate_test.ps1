$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-Condition {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function New-MemoryProvider {
    param([Parameter(Mandatory = $true)][UInt64[]]$Values)
    $state = [pscustomobject]@{
        Index = 0
        Values = @($Values)
    }
    return {
        if ($state.Index -ge $state.Values.Count) {
            throw "Synthetic memory provider was called too many times."
        }
        $value = $state.Values[$state.Index]
        $state.Index++
        return [UInt64]$value
    }.GetNewClosure()
}

function New-DoubleProvider {
    param([Parameter(Mandatory = $true)][double[]]$Values)
    $state = [pscustomobject]@{
        Index = 0
        Values = @($Values)
    }
    return {
        if ($state.Index -ge $state.Values.Count) {
            throw "Synthetic double provider was called too many times."
        }
        $value = $state.Values[$state.Index]
        $state.Index++
        return [double]$value
    }.GetNewClosure()
}

function New-SyntheticDiskProvider {
    param([Parameter(Mandatory = $true)][UInt64]$FreeBytes)
    return {
        param($requestedPath)
        return [pscustomobject]@{
            Root = "synthetic:/"
            FreeBytes = $FreeBytes
            TotalBytes = [UInt64](100 * 1GB)
        }
    }.GetNewClosure()
}

function Invoke-SyntheticGate {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$ProcessProvider,
        [Parameter(Mandatory = $true)][scriptblock]$MemoryProvider,
        [Parameter(Mandatory = $true)][scriptblock]$DiskProvider,
        [Parameter(Mandatory = $true)][scriptblock]$WslProvider,
        [bool]$QueryWsl = $false,
        [double]$Duration = 2.0,
        [double]$MaximumPaging = -1.0,
        [scriptblock]$PagingProvider = { 0.0 }
    )

    return Invoke-OpenFoamResourceGateCore `
        -MinimumAvailableMemoryGiB 5.0 `
        -SampleDurationSeconds $Duration `
        -SampleIntervalSeconds 1.0 `
        -MaximumPagesInputPerSecond $MaximumPaging `
        -MinimumFreeDiskGiB 10.0 `
        -DiskPath "synthetic:/case" `
        -QueryWsl $QueryWsl `
        -WslDistribution "SyntheticUbuntu" `
        -MinimumWslAvailableMemoryGiB 5.0 `
        -MinimumWslFreeDiskGiB 10.0 `
        -WslDiskPath "/synthetic/case" `
        -ProcessProvider $ProcessProvider `
        -MemoryProvider $MemoryProvider `
        -PagingProvider $PagingProvider `
        -DiskProvider $DiskProvider `
        -SleepProvider { param($seconds) } `
        -WslProvider $WslProvider `
        -ProviderLabel "synthetic_test"
}

$gateScript = Join-Path $PSScriptRoot "../tools/openfoam_resource_gate.ps1"
. $gateScript

$script:wslCalls = 0
$forbiddenWsl = {
    param($distribution, $path)
    $script:wslCalls++
    throw "Synthetic test unexpectedly reached WSL."
}

# A passing host-only gate samples the complete requested schedule and never
# reaches WSL.
$pass = Invoke-SyntheticGate `
    -ProcessProvider { @() } `
    -MemoryProvider (New-MemoryProvider -Values @(
        [UInt64](6 * 1GB), [UInt64](5.5 * 1GB), [UInt64](5.25 * 1GB))) `
    -DiskProvider (New-SyntheticDiskProvider -FreeBytes ([UInt64](20 * 1GB))) `
    -WslProvider $forbiddenWsl
Assert-Condition ($pass.ExitCode -eq 0) (
    "Passing synthetic gate returned nonzero: " +
    ($pass.Evidence | ConvertTo-Json -Depth 12 -Compress))
Assert-Condition ($pass.Evidence.status -eq "PASS") "Passing gate did not report PASS."
Assert-Condition ($pass.Evidence.host.memory_check.samples.Count -eq 3) `
    "Passing gate did not retain all three memory samples."
Assert-Condition ($pass.Evidence.host.process_check.scan_count -eq 4) `
    "Passing gate did not scan processes initially and at every memory sample."
Assert-Condition (-not $pass.Evidence.wsl.queried) `
    "Host-only gate unexpectedly queried WSL."
Assert-Condition ($script:wslCalls -eq 0) "Host-only gate invoked the WSL provider."

# A competing solver fails before disk, memory, sleep, or WSL providers run.
$script:memoryCalls = 0
$script:diskCalls = 0
$script:wslCalls = 0
$competitor = Invoke-SyntheticGate `
    -ProcessProvider { @([pscustomobject]@{ Name = "fluent"; Id = 4242 }) } `
    -MemoryProvider { $script:memoryCalls++; throw "memory must be skipped" } `
    -DiskProvider { param($path) $script:diskCalls++; throw "disk must be skipped" } `
    -WslProvider $forbiddenWsl
Assert-Condition ($competitor.ExitCode -ne 0) `
    "Competing Fluent process did not fail the gate."
Assert-Condition ($competitor.Evidence.host.process_check.competitors.Count -eq 1) `
    ("Competing process was not recorded in evidence: " +
    ($competitor.Evidence | ConvertTo-Json -Depth 12 -Compress))
Assert-Condition ($script:memoryCalls -eq 0 -and $script:diskCalls -eq 0 -and
    $script:wslCalls -eq 0) `
    "A provider ran after the initial competing-process failure."

# One below-threshold sample breaks the continuous-memory requirement and keeps
# WSL unreachable.
$script:wslCalls = 0
$memoryDip = Invoke-SyntheticGate `
    -ProcessProvider { @() } `
    -MemoryProvider (New-MemoryProvider -Values @(
        [UInt64](6 * 1GB), [UInt64](4.9 * 1GB))) `
    -DiskProvider (New-SyntheticDiskProvider -FreeBytes ([UInt64](20 * 1GB))) `
    -WslProvider $forbiddenWsl
Assert-Condition ($memoryDip.ExitCode -ne 0) `
    "Below-threshold host memory did not fail the gate."
Assert-Condition ($memoryDip.Evidence.host.memory_check.samples.Count -eq 2) `
    "Memory-dip evidence did not stop at the failing sample."
Assert-Condition (-not $memoryDip.Evidence.wsl.queried -and $script:wslCalls -eq 0) `
    "Memory failure still reached WSL."

# An explicitly configured paging ceiling is sampled alongside memory and is
# fail-closed without being imposed as an undocumented production default.
$script:wslCalls = 0
$pagingSpike = Invoke-SyntheticGate `
    -ProcessProvider { @() } `
    -MemoryProvider (New-MemoryProvider -Values @(
        [UInt64](6 * 1GB), [UInt64](6 * 1GB))) `
    -PagingProvider (New-DoubleProvider -Values @(10.0, 150.0)) `
    -MaximumPaging 100.0 `
    -DiskProvider (New-SyntheticDiskProvider -FreeBytes ([UInt64](20 * 1GB))) `
    -WslProvider $forbiddenWsl
Assert-Condition ($pagingSpike.ExitCode -ne 0 -and
    $pagingSpike.Evidence.failures[-1].code -eq "host_paging_ceiling_exceeded") `
    "Configured Pages Input/sec ceiling did not fail closed."
Assert-Condition ($script:wslCalls -eq 0) `
    "Paging failure still reached WSL."

# Insufficient disk fails before the long memory interval.
$script:memoryCalls = 0
$diskFailure = Invoke-SyntheticGate `
    -ProcessProvider { @() } `
    -MemoryProvider { $script:memoryCalls++; return [UInt64](6 * 1GB) } `
    -DiskProvider (New-SyntheticDiskProvider -FreeBytes ([UInt64](9 * 1GB))) `
    -WslProvider $forbiddenWsl
Assert-Condition ($diskFailure.ExitCode -ne 0) `
    "Insufficient host disk did not fail the gate."
Assert-Condition ($script:memoryCalls -eq 0) `
    "Memory sampling ran after the host disk gate failed."

# A solver appearing during the observation window invalidates the host gate.
$script:wslCalls = 0
$script:lateProcessCalls = 0
$lateProcessProvider = {
    $script:lateProcessCalls++
    if ($script:lateProcessCalls -ge 3) {
        return @([pscustomobject]@{ Name = "mpiexec"; Id = 81 })
    }
    return @()
}
$lateCompetitor = Invoke-SyntheticGate `
    -ProcessProvider $lateProcessProvider `
    -MemoryProvider (New-MemoryProvider -Values @([UInt64](6 * 1GB))) `
    -DiskProvider (New-SyntheticDiskProvider -FreeBytes ([UInt64](20 * 1GB))) `
    -WslProvider $forbiddenWsl
Assert-Condition ($lateCompetitor.ExitCode -ne 0) `
    "MPI process appearing during sampling did not fail the gate."
Assert-Condition ($lateCompetitor.Evidence.host.process_check.competitors.Count -eq 1) `
    "Late competing process was not retained in JSON evidence."
Assert-Condition ($script:wslCalls -eq 0) `
    "Late host-process failure still reached WSL."

# OpenFOAM preparation utilities that do not end in "Foam" are competitors too.
$preparationCompetitors = @(Get-OpenFoamCompetingProcesses `
    -ProcessProvider { @([pscustomobject]@{
        Name = "bash"
        Id = 82
        CommandLine = "/opt/openfoam/bin/snappyHexMesh -overwrite"
    }) } `
    -Patterns $script:OpenFoamResourceGateProcessPatterns)
Assert-Condition ($preparationCompetitors.Count -eq 1) `
    "OpenFOAM preparation process was not detected from its full command line."

# WSL is reached only after a green host gate, and its synthetic snapshot is
# incorporated into the same evidence object.
$script:wslCalls = 0
$passingWslProvider = {
    param($distribution, $path)
    $script:wslCalls++
    return [pscustomobject]@{
        AvailableBytes = [UInt64](6 * 1GB)
        TotalBytes = [UInt64](8 * 1GB)
        DiskFreeBytes = [UInt64](30 * 1GB)
        DiskTotalBytes = [UInt64](100 * 1GB)
        Processes = @()
    }
}
$wslPass = Invoke-SyntheticGate `
    -ProcessProvider { @() } `
    -MemoryProvider (New-MemoryProvider -Values @(
        [UInt64](6 * 1GB), [UInt64](6 * 1GB))) `
    -DiskProvider (New-SyntheticDiskProvider -FreeBytes ([UInt64](20 * 1GB))) `
    -WslProvider $passingWslProvider -QueryWsl $true -Duration 0.0
Assert-Condition ($wslPass.ExitCode -eq 0 -and $wslPass.Evidence.wsl.queried -and
    $wslPass.Evidence.wsl.passed) `
    "Passing optional WSL snapshot did not pass the combined gate."
Assert-Condition ($script:wslCalls -eq 1) `
    "Optional WSL provider was not called exactly once after host PASS."

# A requested WSL check remains skipped after a host failure.
$script:wslCalls = 0
$hostFailWithWslRequested = Invoke-SyntheticGate `
    -ProcessProvider { @() } `
    -MemoryProvider (New-MemoryProvider -Values @([UInt64](4 * 1GB))) `
    -DiskProvider (New-SyntheticDiskProvider -FreeBytes ([UInt64](20 * 1GB))) `
    -WslProvider $forbiddenWsl -QueryWsl $true -Duration 0.0
Assert-Condition (-not $hostFailWithWslRequested.Evidence.wsl.queried -and
    $hostFailWithWslRequested.Evidence.wsl.skipped_reason -eq "host_gate_failed" -and
    $script:wslCalls -eq 0) `
    "Requested WSL check was not suppressed by host failure."

# WSL evidence is itself fail-closed.
$script:wslCalls = 0
$failingWslProvider = {
    param($distribution, $path)
    $script:wslCalls++
    return [pscustomobject]@{
        AvailableBytes = [UInt64](4 * 1GB)
        TotalBytes = [UInt64](8 * 1GB)
        DiskFreeBytes = [UInt64](30 * 1GB)
        DiskTotalBytes = [UInt64](100 * 1GB)
        Processes = @([pscustomobject]@{
            Name = "semiFrozenChtM"
            Id = 9
            CommandLine = "/opt/OpenFOAM/bin/semiFrozenChtMultiRegionFoam -parallel"
        })
    }
}
$wslFailure = Invoke-SyntheticGate `
    -ProcessProvider { @() } `
    -MemoryProvider (New-MemoryProvider -Values @(
        [UInt64](6 * 1GB), [UInt64](6 * 1GB))) `
    -DiskProvider (New-SyntheticDiskProvider -FreeBytes ([UInt64](20 * 1GB))) `
    -WslProvider $failingWslProvider -QueryWsl $true -Duration 0.0
Assert-Condition ($wslFailure.ExitCode -ne 0 -and
    $wslFailure.Evidence.wsl.competitors.Count -eq 1) `
    "Failing WSL resources or full-command solver name did not fail closed."

# WSL startup can consume enough Windows memory to invalidate a previously
# passing host observation. The immediate post-start recheck must catch that.
$script:wslCalls = 0
$postWslInvalidation = Invoke-SyntheticGate `
    -ProcessProvider { @() } `
    -MemoryProvider (New-MemoryProvider -Values @(
        [UInt64](6 * 1GB), [UInt64](4 * 1GB))) `
    -DiskProvider (New-SyntheticDiskProvider -FreeBytes ([UInt64](20 * 1GB))) `
    -WslProvider $passingWslProvider -QueryWsl $true -Duration 0.0
Assert-Condition ($postWslInvalidation.ExitCode -ne 0 -and
    $postWslInvalidation.Evidence.host.post_wsl_recheck.performed -and
    -not $postWslInvalidation.Evidence.host.post_wsl_recheck.passed -and
    -not $postWslInvalidation.Evidence.host.passed) `
    "Post-WSL host-memory invalidation did not fail closed."

# Invalid scalar input also produces structured failure evidence.
$invalid = Invoke-OpenFoamResourceGateCore `
    -MinimumAvailableMemoryGiB 5.0 `
    -SampleDurationSeconds -1.0 `
    -SampleIntervalSeconds 1.0 `
    -MaximumPagesInputPerSecond -1.0 `
    -MinimumFreeDiskGiB 10.0 `
    -DiskPath "synthetic:/case" `
    -QueryWsl $false `
    -WslDistribution "SyntheticUbuntu" `
    -MinimumWslAvailableMemoryGiB 5.0 `
    -MinimumWslFreeDiskGiB 10.0 `
    -WslDiskPath "/synthetic/case" `
    -ProcessProvider { @() } `
    -MemoryProvider { [UInt64](6 * 1GB) } `
    -PagingProvider { 0.0 } `
    -DiskProvider (New-SyntheticDiskProvider -FreeBytes ([UInt64](20 * 1GB))) `
    -SleepProvider { param($seconds) } `
    -WslProvider $forbiddenWsl `
    -ProviderLabel "synthetic_test"
Assert-Condition ($invalid.ExitCode -ne 0 -and
    $invalid.Evidence.failures[0].code -eq "invalid_configuration") `
    "Invalid configuration did not return machine-readable failure evidence."

$roundTrip = $wslFailure.Evidence | ConvertTo-Json -Depth 12 | ConvertFrom-Json
Assert-Condition ($roundTrip.schema_version -eq 1 -and
    $roundTrip.tool -eq "openfoam_resource_gate" -and
    -not [string]::IsNullOrWhiteSpace($roundTrip.evidence_id) -and
    $roundTrip.status -eq "FAIL") `
    "Evidence did not survive a JSON round trip."

# The production wrapper must reject attempts to weaken the standing campaign
# floors before querying real providers (and therefore before WSL can be touched).
$pwshPath = (Get-Command pwsh -ErrorAction Stop).Source
$directOutput = @(& $pwshPath -NoLogo -NoProfile -File $gateScript `
    -MinimumAvailableMemoryGiB 0 `
    -SampleDurationSeconds 0 `
    -MinimumFreeDiskGiB 0 `
    -QueryWsl `
    -MinimumWslAvailableMemoryGiB 0 `
    -MinimumWslFreeDiskGiB 0)
$directExitCode = $LASTEXITCODE
$directEvidence = ($directOutput -join [Environment]::NewLine) | ConvertFrom-Json
Assert-Condition ($directExitCode -ne 0 -and
    $directEvidence.status -eq "FAIL" -and
    $directEvidence.failures[0].code -eq "direct_policy_floor_not_met" -and
    $directEvidence.wsl.requested -and
    -not $directEvidence.wsl.queried) `
    "Production wrapper allowed campaign floors to be weakened or emitted invalid JSON."

# Evidence targets are create-once. A second writer and a direct gate invocation
# both refuse the existing target without changing its retained bytes.
$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$evidenceTestRoot = [IO.Path]::GetFullPath((Join-Path $tempBase (
    "thermal_sim_resource_gate_test_" + [guid]::NewGuid().ToString("N"))))
if (-not $evidenceTestRoot.StartsWith(
    $tempBase, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe synthetic evidence directory: $evidenceTestRoot"
}
[void][IO.Directory]::CreateDirectory($evidenceTestRoot)
try {
    $evidencePath = Join-Path $evidenceTestRoot "pre_export_unique.json"
    $sentinelJson = '{"status":"PASS","evidence_id":"retained-sentinel"}'
    [void](Write-OpenFoamGateEvidenceFile -Path $evidencePath -Json $sentinelJson)
    $sentinelBytes = [IO.File]::ReadAllBytes($evidencePath)
    $secondWriteRefused = $false
    try {
        [void](Write-OpenFoamGateEvidenceFile -Path $evidencePath `
            -Json '{"status":"FAIL"}')
    }
    catch {
        $secondWriteRefused = $true
    }
    Assert-Condition $secondWriteRefused `
        "Evidence writer overwrote an existing target."
    Assert-Condition ([Convert]::ToBase64String($sentinelBytes) -eq
        [Convert]::ToBase64String([IO.File]::ReadAllBytes($evidencePath))) `
        "Refused evidence write changed the retained artifact."

    $existingOutput = @(& $pwshPath -NoLogo -NoProfile -File $gateScript `
        -EvidencePath $evidencePath)
    $existingExitCode = $LASTEXITCODE
    $existingEvidence = ($existingOutput -join [Environment]::NewLine) |
        ConvertFrom-Json
    Assert-Condition ($existingExitCode -eq 24 -and
        $existingEvidence.failures.Count -eq 1 -and
        $existingEvidence.failures[0].code -eq "evidence_target_exists" -and
        -not $existingEvidence.evidence_output.written -and
        -not $existingEvidence.wsl.queried) `
        "Production wrapper did not reject an existing evidence target before providers."
    Assert-Condition ([Convert]::ToBase64String($sentinelBytes) -eq
        [Convert]::ToBase64String([IO.File]::ReadAllBytes($evidencePath))) `
        "Direct evidence-target refusal changed the retained artifact."
}
finally {
    if ([IO.Directory]::Exists($evidenceTestRoot)) {
        $cleanupTarget = [IO.Path]::GetFullPath($evidenceTestRoot)
        if (-not $cleanupTarget.StartsWith(
                $tempBase, [StringComparison]::OrdinalIgnoreCase) -or
            -not [IO.Path]::GetFileName($cleanupTarget).StartsWith(
                "thermal_sim_resource_gate_test_", [StringComparison]::Ordinal)) {
            throw "Refusing unsafe synthetic evidence cleanup: $cleanupTarget"
        }
        Remove-Item -LiteralPath $cleanupTarget -Recurse -Force
    }
}

Write-Host (
    "openfoam_resource_gate_test PASSED: hostPass=1; competitorFailures=3; " +
    "memoryFailures=2; pagingFailures=1; diskFailures=1; wslPass=1; " +
    "wslFailures=1; postWslInvalidations=1; invalidConfiguration=1; " +
    "productionFloorFailures=1; immutableEvidenceFailures=2; realWslCalls=0")
