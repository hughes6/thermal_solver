[CmdletBinding()]
param(
    [double]$MinimumAvailableMemoryGiB = 5.0,
    [double]$SampleDurationSeconds = 60.0,
    [double]$SampleIntervalSeconds = 1.0,
    [double]$MaximumPagesInputPerSecond = -1.0,
    [double]$MinimumFreeDiskGiB = 10.0,
    [string]$DiskPath = (Get-Location).Path,
    [switch]$QueryWsl,
    [string]$WslDistribution = "Ubuntu",
    [double]$MinimumWslAvailableMemoryGiB = 5.0,
    [double]$MinimumWslFreeDiskGiB = 10.0,
    [string]$WslDiskPath = "/home",
    [string]$EvidencePath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:OpenFoamResourceGateGiB = [UInt64]1073741824
$script:OpenFoamResourceGateProcessPatterns = @(
    '(?i)^fluent.*$',
    '(?i)^fl_mpi.*$',
    '(?i)^cortex(?:\..*)?$',
    '(?i)^mpiexec(?:\.hydra)?$',
    '(?i)^mpirun$',
    '(?i)^orted$',
    '(?i)^hydra_(?:pmi_)?proxy$',
    '(?i)^smpd$',
    '(?i)^foamRun$',
    '(?i)^snappyHexMesh$',
    '(?i)^splitMeshRegions$',
    '(?i)^decomposePar$',
    '(?i)^redistributePar$',
    '(?i)^reconstructPar$',
    '(?i)^.*Foam$'
)

function New-OpenFoamGateException {
    param(
        [Parameter(Mandatory = $true)][string]$Code,
        [Parameter(Mandatory = $true)][int]$ExitCode,
        [Parameter(Mandatory = $true)][string]$Message
    )

    $exception = [InvalidOperationException]::new($Message)
    $exception.Data['OpenFoamGateFailureCode'] = $Code
    $exception.Data['OpenFoamGateExitCode'] = $ExitCode
    return $exception
}

function Assert-OpenFoamGateProductionPolicy {
    param(
        [Parameter(Mandatory = $true)][double]$MinimumAvailableMemoryGiB,
        [Parameter(Mandatory = $true)][double]$SampleDurationSeconds,
        [Parameter(Mandatory = $true)][double]$SampleIntervalSeconds,
        [Parameter(Mandatory = $true)][double]$MinimumFreeDiskGiB,
        [Parameter(Mandatory = $true)][bool]$QueryWsl,
        [Parameter(Mandatory = $true)][double]$MinimumWslAvailableMemoryGiB,
        [Parameter(Mandatory = $true)][double]$MinimumWslFreeDiskGiB
    )

    $violations = [Collections.Generic.List[string]]::new()
    if ($MinimumAvailableMemoryGiB -lt 5.0) {
        [void]$violations.Add('MinimumAvailableMemoryGiB cannot be below 5 GiB.')
    }
    if ($SampleDurationSeconds -lt 60.0) {
        [void]$violations.Add('SampleDurationSeconds cannot be below 60 seconds.')
    }
    if ($SampleIntervalSeconds -gt 1.0) {
        [void]$violations.Add('SampleIntervalSeconds cannot exceed 1 second.')
    }
    if ($MinimumFreeDiskGiB -lt 10.0) {
        [void]$violations.Add('MinimumFreeDiskGiB cannot be below 10 GiB.')
    }
    if ($QueryWsl -and $MinimumWslAvailableMemoryGiB -lt 5.0) {
        [void]$violations.Add('MinimumWslAvailableMemoryGiB cannot be below 5 GiB.')
    }
    if ($QueryWsl -and $MinimumWslFreeDiskGiB -lt 10.0) {
        [void]$violations.Add('MinimumWslFreeDiskGiB cannot be below 10 GiB.')
    }
    if ($violations.Count -gt 0) {
        throw (New-OpenFoamGateException -Code 'direct_policy_floor_not_met' `
            -ExitCode 10 -Message ($violations -join ' '))
    }
}

function Resolve-OpenFoamGateEvidenceTarget {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [IO.Path]::GetFullPath($Path)
    if ([IO.File]::Exists($fullPath) -or [IO.Directory]::Exists($fullPath)) {
        throw (New-OpenFoamGateException -Code 'evidence_target_exists' `
            -ExitCode 24 `
            -Message "Evidence target already exists; use a unique path: '$fullPath'.")
    }
    return $fullPath
}

function Convert-GiBToBytes {
    param(
        [Parameter(Mandatory = $true)][double]$Value,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if ([double]::IsNaN($Value) -or [double]::IsInfinity($Value) -or
        $Value -lt 0.0) {
        throw "$Name must be a finite, nonnegative number."
    }
    $bytes = $Value * [double]$script:OpenFoamResourceGateGiB
    if ($bytes -gt [double][UInt64]::MaxValue) {
        throw "$Name is too large to represent as bytes."
    }
    return [UInt64][Math]::Ceiling($bytes)
}

function Test-OpenFoamGateScalar {
    param(
        [Parameter(Mandatory = $true)][double]$Value,
        [Parameter(Mandatory = $true)][string]$Name,
        [double]$Minimum = 0.0,
        [double]$Maximum = 86400.0,
        [switch]$StrictlyPositive
    )

    if ([double]::IsNaN($Value) -or [double]::IsInfinity($Value) -or
        $Value -lt $Minimum -or $Value -gt $Maximum -or
        ($StrictlyPositive -and $Value -le 0.0)) {
        $qualifier = if ($StrictlyPositive) { "positive" } else { "nonnegative" }
        throw "$Name must be a finite $qualifier number no greater than $Maximum."
    }
}

function Get-OpenFoamGateSampleOffsets {
    param(
        [Parameter(Mandatory = $true)][double]$DurationSeconds,
        [Parameter(Mandatory = $true)][double]$IntervalSeconds
    )

    $offsets = [Collections.Generic.List[double]]::new()
    [void]$offsets.Add(0.0)
    $offset = 0.0
    while ($offset -lt $DurationSeconds) {
        $offset = [Math]::Min($DurationSeconds, $offset + $IntervalSeconds)
        [void]$offsets.Add($offset)
        if ($offsets.Count -gt 100000) {
            throw "The requested sampling schedule exceeds 100000 samples."
        }
    }
    return @($offsets)
}

function Get-OpenFoamCompetingProcesses {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$ProcessProvider,
        [Parameter(Mandatory = $true)][string[]]$Patterns
    )

    $matchedProcesses = [Collections.Generic.List[object]]::new()
    foreach ($process in @(& $ProcessProvider)) {
        if ($null -eq $process) {
            continue
        }
        $nameProperty = $process.PSObject.Properties['ProcessName']
        if ($null -eq $nameProperty) {
            $nameProperty = $process.PSObject.Properties['Name']
        }
        if ($null -eq $nameProperty -or
            [string]::IsNullOrWhiteSpace([string]$nameProperty.Value)) {
            throw "A process provider record did not contain Name or ProcessName."
        }
        $name = [string]$nameProperty.Value
        $identifiers = [Collections.Generic.List[string]]::new()
        [void]$identifiers.Add($name)
        $commandProperty = $process.PSObject.Properties['CommandLine']
        if ($null -ne $commandProperty -and
            -not [string]::IsNullOrWhiteSpace([string]$commandProperty.Value)) {
            foreach ($token in @(([string]$commandProperty.Value) -split '\s+')) {
                $candidate = $token.Trim('"', "'")
                if ([string]::IsNullOrWhiteSpace($candidate)) {
                    continue
                }
                [void]$identifiers.Add($candidate)
                $baseName = [IO.Path]::GetFileName($candidate)
                if (-not [string]::IsNullOrWhiteSpace($baseName)) {
                    [void]$identifiers.Add($baseName)
                }
            }
        }
        $isMatch = $false
        $matchedIdentifier = $null
        foreach ($pattern in $Patterns) {
            foreach ($identifier in $identifiers) {
                if ($identifier -match $pattern) {
                    $isMatch = $true
                    $matchedIdentifier = $identifier
                    break
                }
            }
            if ($isMatch) {
                break
            }
        }
        if (-not $isMatch) {
            continue
        }

        $idProperty = $process.PSObject.Properties['Id']
        $processId = if ($null -ne $idProperty) { [Int64]$idProperty.Value } else { -1 }
        [void]$matchedProcesses.Add([pscustomobject][ordered]@{
            name = $name
            id = $processId
            matched_identifier = $matchedIdentifier
        })
    }
    return @($matchedProcesses)
}

function Add-OpenFoamGateCompetitors {
    param(
        [Parameter(Mandatory = $true)]
        [Collections.Generic.Dictionary[string, object]]$Destination,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$Processes
    )

    foreach ($process in $Processes) {
        $key = "{0}|{1}" -f $process.name, $process.id
        if (-not $Destination.ContainsKey($key)) {
            $Destination.Add($key, $process)
        }
    }
}

function New-OpenFoamGateFailure {
    param(
        [Parameter(Mandatory = $true)][string]$Code,
        [Parameter(Mandatory = $true)][string]$Message
    )
    return [pscustomobject][ordered]@{
        code = $Code
        message = $Message
    }
}

function Invoke-OpenFoamResourceGateCore {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][double]$MinimumAvailableMemoryGiB,
        [Parameter(Mandatory = $true)][double]$SampleDurationSeconds,
        [Parameter(Mandatory = $true)][double]$SampleIntervalSeconds,
        [Parameter(Mandatory = $true)][double]$MaximumPagesInputPerSecond,
        [Parameter(Mandatory = $true)][double]$MinimumFreeDiskGiB,
        [Parameter(Mandatory = $true)][string]$DiskPath,
        [Parameter(Mandatory = $true)][bool]$QueryWsl,
        [Parameter(Mandatory = $true)][string]$WslDistribution,
        [Parameter(Mandatory = $true)][double]$MinimumWslAvailableMemoryGiB,
        [Parameter(Mandatory = $true)][double]$MinimumWslFreeDiskGiB,
        [Parameter(Mandatory = $true)][string]$WslDiskPath,
        [Parameter(Mandatory = $true)][scriptblock]$ProcessProvider,
        [Parameter(Mandatory = $true)][scriptblock]$MemoryProvider,
        [Parameter(Mandatory = $true)][scriptblock]$PagingProvider,
        [Parameter(Mandatory = $true)][scriptblock]$DiskProvider,
        [Parameter(Mandatory = $true)][scriptblock]$SleepProvider,
        [Parameter(Mandatory = $true)][scriptblock]$WslProvider,
        [string]$ProviderLabel = "injected"
    )

    $failures = [Collections.Generic.List[object]]::new()
    $samples = [Collections.Generic.List[object]]::new()
    $competitors = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    $exitCode = 0

    $evidence = [ordered]@{
        schema_version = 1
        tool = "openfoam_resource_gate"
        evidence_id = [guid]::NewGuid().ToString("D")
        generated_utc = [DateTimeOffset]::UtcNow.ToString("o")
        provider = $ProviderLabel
        status = "FAIL"
        exit_code = 10
        parameters = [ordered]@{
            minimum_available_memory_gib = $MinimumAvailableMemoryGiB
            sample_duration_seconds = $SampleDurationSeconds
            sample_interval_seconds = $SampleIntervalSeconds
            maximum_pages_input_per_second = $MaximumPagesInputPerSecond
            minimum_free_disk_gib = $MinimumFreeDiskGiB
            disk_path = $DiskPath
            query_wsl = $QueryWsl
            wsl_distribution = $WslDistribution
            minimum_wsl_available_memory_gib = $MinimumWslAvailableMemoryGiB
            minimum_wsl_free_disk_gib = $MinimumWslFreeDiskGiB
            wsl_disk_path = $WslDiskPath
            competing_process_patterns = @($script:OpenFoamResourceGateProcessPatterns)
        }
        host = [ordered]@{
            passed = $false
            process_check = [ordered]@{
                passed = $null
                scan_count = 0
                competitors = @()
                error = $null
            }
            disk_check = [ordered]@{
                passed = $null
                requested_path = $DiskPath
                resolved_root = $null
                free_bytes = $null
                total_bytes = $null
                minimum_bytes = $null
                error = $null
                skipped_reason = $null
            }
            memory_check = [ordered]@{
                passed = $null
                minimum_bytes = $null
                minimum_observed_bytes = $null
                requested_duration_seconds = $SampleDurationSeconds
                requested_interval_seconds = $SampleIntervalSeconds
                paging_check_enabled = $MaximumPagesInputPerSecond -ge 0.0
                maximum_pages_input_per_second = if (
                    $MaximumPagesInputPerSecond -ge 0.0
                ) { $MaximumPagesInputPerSecond } else { $null }
                completed_duration_seconds = 0.0
                samples = @()
                error = $null
                skipped_reason = $null
            }
            post_wsl_recheck = [ordered]@{
                performed = $false
                passed = $null
                available_memory_bytes = $null
                pages_input_per_second = $null
                disk_free_bytes = $null
                competitors = @()
                error = $null
            }
        }
        wsl = [ordered]@{
            requested = $QueryWsl
            queried = $false
            passed = $null
            distribution = $WslDistribution
            available_bytes = $null
            total_bytes = $null
            minimum_available_bytes = $null
            disk_path = $WslDiskPath
            disk_free_bytes = $null
            disk_total_bytes = $null
            minimum_disk_free_bytes = $null
            competitors = @()
            error = $null
            skipped_reason = if ($QueryWsl) { $null } else { "not_requested" }
        }
        evidence_output = [ordered]@{
            requested = $false
            path = $null
            written = $false
        }
        failures = @()
    }

    try {
        Test-OpenFoamGateScalar -Value $MinimumAvailableMemoryGiB `
            -Name "MinimumAvailableMemoryGiB" -Maximum 1048576.0
        Test-OpenFoamGateScalar -Value $SampleDurationSeconds `
            -Name "SampleDurationSeconds" -Maximum 86400.0
        Test-OpenFoamGateScalar -Value $SampleIntervalSeconds `
            -Name "SampleIntervalSeconds" -Maximum 3600.0 -StrictlyPositive
        if ($MaximumPagesInputPerSecond -ne -1.0) {
            Test-OpenFoamGateScalar -Value $MaximumPagesInputPerSecond `
                -Name "MaximumPagesInputPerSecond" -Maximum 1000000000.0
        }
        Test-OpenFoamGateScalar -Value $MinimumFreeDiskGiB `
            -Name "MinimumFreeDiskGiB" -Maximum 1048576.0
        Test-OpenFoamGateScalar -Value $MinimumWslAvailableMemoryGiB `
            -Name "MinimumWslAvailableMemoryGiB" -Maximum 1048576.0
        Test-OpenFoamGateScalar -Value $MinimumWslFreeDiskGiB `
            -Name "MinimumWslFreeDiskGiB" -Maximum 1048576.0
        if ([string]::IsNullOrWhiteSpace($DiskPath)) {
            throw "DiskPath must not be empty."
        }
        if ($QueryWsl -and [string]::IsNullOrWhiteSpace($WslDistribution)) {
            throw "WslDistribution must not be empty when QueryWsl is enabled."
        }
        if ($QueryWsl -and [string]::IsNullOrWhiteSpace($WslDiskPath)) {
            throw "WslDiskPath must not be empty when QueryWsl is enabled."
        }

        $minimumMemoryBytes = Convert-GiBToBytes `
            -Value $MinimumAvailableMemoryGiB -Name "MinimumAvailableMemoryGiB"
        $minimumDiskBytes = Convert-GiBToBytes `
            -Value $MinimumFreeDiskGiB -Name "MinimumFreeDiskGiB"
        $minimumWslMemoryBytes = Convert-GiBToBytes `
            -Value $MinimumWslAvailableMemoryGiB `
            -Name "MinimumWslAvailableMemoryGiB"
        $minimumWslDiskBytes = Convert-GiBToBytes `
            -Value $MinimumWslFreeDiskGiB -Name "MinimumWslFreeDiskGiB"
        $evidence.host.memory_check.minimum_bytes = $minimumMemoryBytes
        $evidence.host.disk_check.minimum_bytes = $minimumDiskBytes
        $evidence.wsl.minimum_available_bytes = $minimumWslMemoryBytes
        $evidence.wsl.minimum_disk_free_bytes = $minimumWslDiskBytes
        $sampleOffsets = @(Get-OpenFoamGateSampleOffsets `
            -DurationSeconds $SampleDurationSeconds `
            -IntervalSeconds $SampleIntervalSeconds)
    }
    catch {
        [void]$failures.Add((New-OpenFoamGateFailure `
            -Code "invalid_configuration" -Message $_.Exception.Message))
        $evidence.host.process_check.passed = $false
        $evidence.host.disk_check.skipped_reason = "invalid_configuration"
        $evidence.host.memory_check.skipped_reason = "invalid_configuration"
        if ($QueryWsl) {
            $evidence.wsl.skipped_reason = "host_gate_failed"
        }
        $evidence.failures = @($failures)
        return [pscustomobject]@{
            ExitCode = 10
            Evidence = [pscustomobject]$evidence
        }
    }

    try {
        $initialCompetitors = @(Get-OpenFoamCompetingProcesses `
            -ProcessProvider $ProcessProvider `
            -Patterns $script:OpenFoamResourceGateProcessPatterns)
        $evidence.host.process_check.scan_count++
        Add-OpenFoamGateCompetitors -Destination $competitors `
            -Processes $initialCompetitors
    }
    catch {
        $evidence.host.process_check.passed = $false
        $evidence.host.process_check.error = $_.Exception.Message
        [void]$failures.Add((New-OpenFoamGateFailure `
            -Code "host_process_query_failed" -Message $_.Exception.Message))
        $exitCode = 20
    }

    if ($exitCode -eq 0 -and $competitors.Count -gt 0) {
        $evidence.host.process_check.passed = $false
        [void]$failures.Add((New-OpenFoamGateFailure `
            -Code "competing_process_detected" `
            -Message "A competing CFD or MPI process is already running."))
        $exitCode = 20
    }
    elseif ($exitCode -eq 0) {
        $evidence.host.process_check.passed = $true
    }

    if ($exitCode -eq 0) {
        try {
            $disk = & $DiskProvider $DiskPath
            if ($null -eq $disk) {
                throw "The disk provider returned no result."
            }
            $diskFreeBytes = [UInt64]$disk.FreeBytes
            $diskTotalBytes = [UInt64]$disk.TotalBytes
            $evidence.host.disk_check.resolved_root = [string]$disk.Root
            $evidence.host.disk_check.free_bytes = $diskFreeBytes
            $evidence.host.disk_check.total_bytes = $diskTotalBytes
            $evidence.host.disk_check.passed = $diskFreeBytes -ge $minimumDiskBytes
            if (-not $evidence.host.disk_check.passed) {
                [void]$failures.Add((New-OpenFoamGateFailure `
                    -Code "insufficient_host_disk" `
                    -Message "Host disk free space is below the configured minimum."))
                $exitCode = 22
            }
        }
        catch {
            $evidence.host.disk_check.passed = $false
            $evidence.host.disk_check.error = $_.Exception.Message
            [void]$failures.Add((New-OpenFoamGateFailure `
                -Code "host_disk_query_failed" -Message $_.Exception.Message))
            $exitCode = 22
        }
    }
    else {
        $evidence.host.disk_check.skipped_reason = "competing_process_or_query_failure"
    }

    if ($exitCode -eq 0) {
        $previousOffset = 0.0
        foreach ($offset in $sampleOffsets) {
            if ($offset -gt $previousOffset) {
                try {
                    & $SleepProvider ([double]($offset - $previousOffset))
                }
                catch {
                    $evidence.host.memory_check.error = $_.Exception.Message
                    [void]$failures.Add((New-OpenFoamGateFailure `
                        -Code "host_memory_sampling_failed" `
                        -Message "Memory sampling delay failed: $($_.Exception.Message)"))
                    $exitCode = 21
                    break
                }
            }
            $previousOffset = $offset

            try {
                $sampleCompetitors = @(Get-OpenFoamCompetingProcesses `
                    -ProcessProvider $ProcessProvider `
                    -Patterns $script:OpenFoamResourceGateProcessPatterns)
                $evidence.host.process_check.scan_count++
                Add-OpenFoamGateCompetitors -Destination $competitors `
                    -Processes $sampleCompetitors
                if ($sampleCompetitors.Count -gt 0) {
                    [void]$failures.Add((New-OpenFoamGateFailure `
                        -Code "competing_process_detected_during_sampling" `
                        -Message "A competing CFD or MPI process appeared during host sampling."))
                    $evidence.host.process_check.passed = $false
                    $exitCode = 20
                    break
                }

                $availableBytes = [UInt64](& $MemoryProvider)
                $pagesInputPerSecond = $null
                if ($MaximumPagesInputPerSecond -ge 0.0) {
                    $pagesInputPerSecond = [double](& $PagingProvider)
                    if ([double]::IsNaN($pagesInputPerSecond) -or
                        [double]::IsInfinity($pagesInputPerSecond) -or
                        $pagesInputPerSecond -lt 0.0) {
                        throw "Paging provider returned a non-finite or negative value."
                    }
                }
                $memoryPassed = $availableBytes -ge $minimumMemoryBytes
                $pagingPassed = (
                    $MaximumPagesInputPerSecond -lt 0.0 -or
                    $pagesInputPerSecond -le $MaximumPagesInputPerSecond)
                [void]$samples.Add([pscustomobject][ordered]@{
                    offset_seconds = [double]$offset
                    timestamp_utc = [DateTimeOffset]::UtcNow.ToString("o")
                    available_bytes = $availableBytes
                    pages_input_per_second = $pagesInputPerSecond
                    memory_passed = $memoryPassed
                    paging_passed = $pagingPassed
                    passed = $memoryPassed -and $pagingPassed
                })
                $evidence.host.memory_check.completed_duration_seconds = [double]$offset
                if (-not $memoryPassed) {
                    [void]$failures.Add((New-OpenFoamGateFailure `
                        -Code "insufficient_host_memory" `
                        -Message "Host available physical memory fell below the configured minimum."))
                    $exitCode = 21
                    break
                }
                if (-not $pagingPassed) {
                    [void]$failures.Add((New-OpenFoamGateFailure `
                        -Code "host_paging_ceiling_exceeded" `
                        -Message "Host Pages Input/sec exceeded the configured ceiling."))
                    $exitCode = 21
                    break
                }
            }
            catch {
                $evidence.host.memory_check.error = $_.Exception.Message
                [void]$failures.Add((New-OpenFoamGateFailure `
                    -Code "host_memory_or_process_query_failed" `
                    -Message $_.Exception.Message))
                $exitCode = 21
                break
            }
        }

        if ($samples.Count -gt 0) {
            $evidence.host.memory_check.minimum_observed_bytes = [UInt64](
                $samples | Measure-Object -Property available_bytes -Minimum
            ).Minimum
        }
        $evidence.host.memory_check.passed = (
            $exitCode -eq 0 -and
            $samples.Count -eq $sampleOffsets.Count -and
            $evidence.host.memory_check.completed_duration_seconds -ge
                $SampleDurationSeconds)
    }
    else {
        $evidence.host.memory_check.skipped_reason = if ($exitCode -eq 22) {
            "host_disk_gate_failed"
        }
        else {
            "host_process_gate_failed"
        }
    }

    $evidence.host.process_check.competitors = @($competitors.Values | Sort-Object name, id)
    $evidence.host.memory_check.samples = @($samples)
    $evidence.host.passed = (
        $evidence.host.process_check.passed -eq $true -and
        $evidence.host.disk_check.passed -eq $true -and
        $evidence.host.memory_check.passed -eq $true)

    # This is the only point at which the injected WSL provider may be invoked.
    # Normal script execution binds that provider to wsl.exe; a failed host gate
    # therefore cannot start or wake WSL.
    if ($QueryWsl -and $evidence.host.passed) {
        $evidence.wsl.queried = $true
        try {
            $wslSnapshot = & $WslProvider $WslDistribution $WslDiskPath
            if ($null -eq $wslSnapshot) {
                throw "The WSL provider returned no result."
            }
            $wslAvailableBytes = [UInt64]$wslSnapshot.AvailableBytes
            $wslTotalBytes = [UInt64]$wslSnapshot.TotalBytes
            $wslDiskFreeBytes = [UInt64]$wslSnapshot.DiskFreeBytes
            $wslDiskTotalBytes = [UInt64]$wslSnapshot.DiskTotalBytes
            $wslProcesses = @($wslSnapshot.Processes)
            $wslCompetitors = @(Get-OpenFoamCompetingProcesses `
                -ProcessProvider { $wslProcesses } `
                -Patterns $script:OpenFoamResourceGateProcessPatterns)

            $evidence.wsl.available_bytes = $wslAvailableBytes
            $evidence.wsl.total_bytes = $wslTotalBytes
            $evidence.wsl.disk_free_bytes = $wslDiskFreeBytes
            $evidence.wsl.disk_total_bytes = $wslDiskTotalBytes
            $evidence.wsl.competitors = @($wslCompetitors)
            $evidence.wsl.passed = (
                $wslAvailableBytes -ge $minimumWslMemoryBytes -and
                $wslDiskFreeBytes -ge $minimumWslDiskBytes -and
                $wslCompetitors.Count -eq 0)
            if (-not $evidence.wsl.passed) {
                [void]$failures.Add((New-OpenFoamGateFailure `
                    -Code "wsl_resource_gate_failed" `
                    -Message "WSL memory, disk, or competing-process checks failed."))
                $exitCode = 23
            }
        }
        catch {
            $evidence.wsl.passed = $false
            $evidence.wsl.error = $_.Exception.Message
            [void]$failures.Add((New-OpenFoamGateFailure `
                -Code "wsl_query_failed" -Message $_.Exception.Message))
            $exitCode = 23
        }

        # Starting WSL can reclaim host memory and expose new host-side helper
        # processes. Recheck all immediate host gates after the WSL query so a
        # formerly green host snapshot cannot authorize a now-invalid launch.
        $evidence.host.post_wsl_recheck.performed = $true
        try {
            $postWslCompetitors = @(Get-OpenFoamCompetingProcesses `
                -ProcessProvider $ProcessProvider `
                -Patterns $script:OpenFoamResourceGateProcessPatterns)
            $evidence.host.process_check.scan_count++
            Add-OpenFoamGateCompetitors -Destination $competitors `
                -Processes $postWslCompetitors
            $postWslAvailableBytes = [UInt64](& $MemoryProvider)
            $postWslPagesInputPerSecond = $null
            if ($MaximumPagesInputPerSecond -ge 0.0) {
                $postWslPagesInputPerSecond = [double](& $PagingProvider)
                if ([double]::IsNaN($postWslPagesInputPerSecond) -or
                    [double]::IsInfinity($postWslPagesInputPerSecond) -or
                    $postWslPagesInputPerSecond -lt 0.0) {
                    throw "Paging provider returned a non-finite or negative post-WSL value."
                }
            }
            $postWslDisk = & $DiskProvider $DiskPath
            if ($null -eq $postWslDisk) {
                throw "The disk provider returned no post-WSL result."
            }
            $postWslDiskFreeBytes = [UInt64]$postWslDisk.FreeBytes
            $postWslPassed = (
                $postWslCompetitors.Count -eq 0 -and
                $postWslAvailableBytes -ge $minimumMemoryBytes -and
                $postWslDiskFreeBytes -ge $minimumDiskBytes -and
                ($MaximumPagesInputPerSecond -lt 0.0 -or
                    $postWslPagesInputPerSecond -le $MaximumPagesInputPerSecond))
            $evidence.host.post_wsl_recheck.available_memory_bytes =
                $postWslAvailableBytes
            $evidence.host.post_wsl_recheck.pages_input_per_second =
                $postWslPagesInputPerSecond
            $evidence.host.post_wsl_recheck.disk_free_bytes = $postWslDiskFreeBytes
            $evidence.host.post_wsl_recheck.competitors = @($postWslCompetitors)
            $evidence.host.post_wsl_recheck.passed = $postWslPassed
            if (-not $postWslPassed) {
                [void]$failures.Add((New-OpenFoamGateFailure `
                    -Code "host_invalidated_after_wsl_start" `
                    -Message "Host memory, disk, paging, or process checks failed after WSL started."))
                $evidence.host.passed = $false
                $exitCode = 25
            }
        }
        catch {
            $evidence.host.post_wsl_recheck.passed = $false
            $evidence.host.post_wsl_recheck.error = $_.Exception.Message
            [void]$failures.Add((New-OpenFoamGateFailure `
                -Code "post_wsl_host_recheck_failed" -Message $_.Exception.Message))
            $evidence.host.passed = $false
            $exitCode = 25
        }
    }
    elseif ($QueryWsl) {
        $evidence.wsl.skipped_reason = "host_gate_failed"
    }

    $evidence.host.process_check.competitors = @(
        $competitors.Values | Sort-Object name, id)
    $evidence.status = if ($exitCode -eq 0) { "PASS" } else { "FAIL" }
    $evidence.exit_code = $exitCode
    $evidence.failures = @($failures)
    return [pscustomobject]@{
        ExitCode = $exitCode
        Evidence = [pscustomobject]$evidence
    }
}

function Get-OpenFoamHostAvailableMemoryBytes {
    $nativeMemoryType = [System.Management.Automation.PSTypeName]::new(
        'ThermalSimResourceGate.NativeMemory').Type
    if ($null -eq $nativeMemoryType) {
        $source = @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace ThermalSimResourceGate
{
    public static class NativeMemory
    {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
        private sealed class MemoryStatusEx
        {
            public uint Length = (uint)Marshal.SizeOf(typeof(MemoryStatusEx));
            public uint MemoryLoad;
            public ulong TotalPhysical;
            public ulong AvailablePhysical;
            public ulong TotalPageFile;
            public ulong AvailablePageFile;
            public ulong TotalVirtual;
            public ulong AvailableVirtual;
            public ulong AvailableExtendedVirtual;
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GlobalMemoryStatusEx(
            [In, Out] MemoryStatusEx status);

        public static ulong AvailablePhysicalBytes()
        {
            var status = new MemoryStatusEx();
            if (!GlobalMemoryStatusEx(status))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return status.AvailablePhysical;
        }
    }
}
'@
        [void](Add-Type -TypeDefinition $source -Language CSharp)
    }
    return [UInt64][ThermalSimResourceGate.NativeMemory]::AvailablePhysicalBytes()
}

function Get-OpenFoamHostPagesInputPerSecond {
    $counter = Get-Counter -Counter '\Memory\Pages Input/sec' `
        -MaxSamples 1 -ErrorAction Stop
    $sample = @($counter.CounterSamples) | Select-Object -First 1
    if ($null -eq $sample) {
        throw "The Pages Input/sec counter returned no sample."
    }
    $value = [double]$sample.CookedValue
    if ([double]::IsNaN($value) -or [double]::IsInfinity($value) -or
        $value -lt 0.0) {
        throw "The Pages Input/sec counter returned an invalid value."
    }
    return $value
}

function Get-OpenFoamHostProcesses {
    return @(Get-Process -ErrorAction Stop | ForEach-Object {
        [pscustomobject]@{
            Name = $_.ProcessName
            Id = $_.Id
        }
    })
}

function Get-OpenFoamHostDiskSnapshot {
    param([Parameter(Mandatory = $true)][string]$RequestedPath)

    $fullPath = [IO.Path]::GetFullPath($RequestedPath)
    $root = [IO.Path]::GetPathRoot($fullPath)
    if ([string]::IsNullOrWhiteSpace($root)) {
        throw "Unable to resolve a drive root for '$RequestedPath'."
    }
    $drive = [IO.DriveInfo]::new($root)
    if (-not $drive.IsReady) {
        throw "Drive '$root' is not ready."
    }
    return [pscustomobject]@{
        Root = $drive.RootDirectory.FullName
        FreeBytes = [UInt64]$drive.AvailableFreeSpace
        TotalBytes = [UInt64]$drive.TotalSize
    }
}

function Invoke-OpenFoamWslCommand {
    param(
        [Parameter(Mandatory = $true)][string]$Distribution,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $command = Get-Command wsl.exe -ErrorAction Stop
    $output = @(& $command.Source -d $Distribution -- @Arguments 2>&1 |
        ForEach-Object { [string]$_ })
    if ($LASTEXITCODE -ne 0) {
        throw "wsl.exe failed with exit ${LASTEXITCODE}: $($output -join ' ')"
    }
    return $output
}

function Get-OpenFoamWslSnapshot {
    param(
        [Parameter(Mandatory = $true)][string]$Distribution,
        [Parameter(Mandatory = $true)][string]$RequestedDiskPath
    )

    $memInfo = @(Invoke-OpenFoamWslCommand -Distribution $Distribution `
        -Arguments @('cat', '/proc/meminfo'))
    $memTotalMatch = $memInfo | Where-Object { $_ -match '^MemTotal:\s+(\d+)\s+kB$' } |
        Select-Object -First 1
    $memAvailableMatch = $memInfo |
        Where-Object { $_ -match '^MemAvailable:\s+(\d+)\s+kB$' } |
        Select-Object -First 1
    if ($null -eq $memTotalMatch -or $null -eq $memAvailableMatch) {
        throw "WSL /proc/meminfo did not contain MemTotal and MemAvailable."
    }
    [void]($memTotalMatch -match '^MemTotal:\s+(\d+)\s+kB$')
    $totalBytes = [UInt64]$Matches[1] * [UInt64]1024
    [void]($memAvailableMatch -match '^MemAvailable:\s+(\d+)\s+kB$')
    $availableBytes = [UInt64]$Matches[1] * [UInt64]1024

    $diskOutput = @(Invoke-OpenFoamWslCommand -Distribution $Distribution `
        -Arguments @('df', '-Pk', '--', $RequestedDiskPath))
    $diskLine = $diskOutput | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and $_ -notmatch '^Filesystem\s'
    } | Select-Object -Last 1
    if ($null -eq $diskLine) {
        throw "WSL df did not return a data row for '$RequestedDiskPath'."
    }
    $diskFields = @($diskLine.Trim() -split '\s+')
    if ($diskFields.Count -lt 6) {
        throw "Unable to parse WSL df output: '$diskLine'."
    }
    $diskTotalBytes = [UInt64]$diskFields[1] * [UInt64]1024
    $diskFreeBytes = [UInt64]$diskFields[3] * [UInt64]1024

    $processOutput = @(Invoke-OpenFoamWslCommand -Distribution $Distribution `
        -Arguments @('ps', '-ww', '-eo', 'pid=,args='))
    $processes = [Collections.Generic.List[object]]::new()
    foreach ($line in $processOutput) {
        if ($line -match '^\s*(\d+)\s+(.+?)\s*$') {
            $commandLine = [string]$Matches[2]
            $firstToken = @($commandLine -split '\s+')[0].Trim('"', "'")
            [void]$processes.Add([pscustomobject]@{
                Id = [Int64]$Matches[1]
                Name = [IO.Path]::GetFileName($firstToken)
                CommandLine = $commandLine
            })
        }
    }

    return [pscustomobject]@{
        AvailableBytes = $availableBytes
        TotalBytes = $totalBytes
        DiskFreeBytes = $diskFreeBytes
        DiskTotalBytes = $diskTotalBytes
        Processes = @($processes)
    }
}

function Write-OpenFoamGateEvidenceFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Json
    )

    $fullPath = Resolve-OpenFoamGateEvidenceTarget -Path $Path
    $parent = [IO.Path]::GetDirectoryName($fullPath)
    if ([string]::IsNullOrWhiteSpace($parent)) {
        throw "Unable to resolve the evidence parent directory for '$Path'."
    }
    [void][IO.Directory]::CreateDirectory($parent)
    $temporaryPath = Join-Path $parent (
        ".{0}.{1}.{2}.tmp" -f [IO.Path]::GetFileName($fullPath),
        $PID, [guid]::NewGuid().ToString("N"))
    try {
        [IO.File]::WriteAllText(
            $temporaryPath, $Json, [Text.UTF8Encoding]::new($false))
        [IO.File]::Move($temporaryPath, $fullPath)
    }
    finally {
        if (Test-Path -LiteralPath $temporaryPath) {
            Remove-Item -LiteralPath $temporaryPath -Force
        }
    }
    return $fullPath
}

# Dot-sourcing exposes only the pure/injected core to synthetic tests. Direct
# execution below always binds real host providers; there is no command-line
# fixture or bypass switch that can forge a production PASS.
if ($MyInvocation.InvocationName -eq '.') {
    return
}

$result = $null
$evidenceTargetAvailable = [string]::IsNullOrWhiteSpace($EvidencePath)
try {
    if (-not [string]::IsNullOrWhiteSpace($EvidencePath)) {
        [void](Resolve-OpenFoamGateEvidenceTarget -Path $EvidencePath)
        $evidenceTargetAvailable = $true
    }
    Assert-OpenFoamGateProductionPolicy `
        -MinimumAvailableMemoryGiB $MinimumAvailableMemoryGiB `
        -SampleDurationSeconds $SampleDurationSeconds `
        -SampleIntervalSeconds $SampleIntervalSeconds `
        -MinimumFreeDiskGiB $MinimumFreeDiskGiB `
        -QueryWsl ([bool]$QueryWsl) `
        -MinimumWslAvailableMemoryGiB $MinimumWslAvailableMemoryGiB `
        -MinimumWslFreeDiskGiB $MinimumWslFreeDiskGiB
    $result = Invoke-OpenFoamResourceGateCore `
        -MinimumAvailableMemoryGiB $MinimumAvailableMemoryGiB `
        -SampleDurationSeconds $SampleDurationSeconds `
        -SampleIntervalSeconds $SampleIntervalSeconds `
        -MaximumPagesInputPerSecond $MaximumPagesInputPerSecond `
        -MinimumFreeDiskGiB $MinimumFreeDiskGiB `
        -DiskPath $DiskPath `
        -QueryWsl ([bool]$QueryWsl) `
        -WslDistribution $WslDistribution `
        -MinimumWslAvailableMemoryGiB $MinimumWslAvailableMemoryGiB `
        -MinimumWslFreeDiskGiB $MinimumWslFreeDiskGiB `
        -WslDiskPath $WslDiskPath `
        -ProcessProvider { Get-OpenFoamHostProcesses } `
        -MemoryProvider { Get-OpenFoamHostAvailableMemoryBytes } `
        -PagingProvider { Get-OpenFoamHostPagesInputPerSecond } `
        -DiskProvider { param($path) Get-OpenFoamHostDiskSnapshot $path } `
        -SleepProvider {
            param($seconds)
            Start-Sleep -Milliseconds ([int][Math]::Ceiling($seconds * 1000.0))
        } `
        -WslProvider {
            param($distribution, $path)
            Get-OpenFoamWslSnapshot $distribution $path
        } `
        -ProviderLabel "windows_native"
}
catch {
    $failureCode = 'unhandled_gate_error'
    $failureExitCode = 10
    if ($_.Exception.Data.Contains('OpenFoamGateFailureCode')) {
        $failureCode = [string]$_.Exception.Data['OpenFoamGateFailureCode']
    }
    if ($_.Exception.Data.Contains('OpenFoamGateExitCode')) {
        $failureExitCode = [int]$_.Exception.Data['OpenFoamGateExitCode']
    }
    $result = [pscustomobject]@{
        ExitCode = $failureExitCode
        Evidence = [pscustomobject][ordered]@{
            schema_version = 1
            tool = "openfoam_resource_gate"
            evidence_id = [guid]::NewGuid().ToString("D")
            generated_utc = [DateTimeOffset]::UtcNow.ToString("o")
            provider = "windows_native"
            status = "FAIL"
            exit_code = $failureExitCode
            wsl = [ordered]@{
                requested = [bool]$QueryWsl
                queried = $false
                skipped_reason = "startup_preflight_failed"
            }
            evidence_output = [ordered]@{
                requested = -not [string]::IsNullOrWhiteSpace($EvidencePath)
                path = $EvidencePath
                written = $false
            }
            failures = @((New-OpenFoamGateFailure `
                -Code $failureCode -Message $_.Exception.Message))
        }
    }
}

$result.Evidence.evidence_output.requested =
    -not [string]::IsNullOrWhiteSpace($EvidencePath)
if ($result.Evidence.evidence_output.requested -and $evidenceTargetAvailable) {
    try {
        $result.Evidence.evidence_output.path = [IO.Path]::GetFullPath($EvidencePath)
        $result.Evidence.evidence_output.written = $true
        $json = $result.Evidence | ConvertTo-Json -Depth 12
        [void](Write-OpenFoamGateEvidenceFile -Path $EvidencePath -Json $json)
    }
    catch {
        $result.Evidence.evidence_output.written = $false
        $result.Evidence.status = "FAIL"
        $result.Evidence.exit_code = 24
        $result.ExitCode = 24
        $currentFailures = @($result.Evidence.failures)
        $result.Evidence.failures = @($currentFailures + (New-OpenFoamGateFailure `
            -Code "evidence_write_failed" -Message $_.Exception.Message))
    }
}

$json = $result.Evidence | ConvertTo-Json -Depth 12
[Console]::Out.WriteLine($json)
exit [int]$result.ExitCode
