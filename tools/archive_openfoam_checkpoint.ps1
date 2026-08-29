#requires -Version 7.0

<#
.SYNOPSIS
Archives one exact reconstructed/decomposed OpenFOAM checkpoint.

.DESCRIPTION
Plan and Validate are read-only. Archive holds the same WSL flock used by the
generated solver runners, stages one tar, verifies the GitHub Release asset,
records and pushes checkpoints.csv, re-reads remote main, and only then may
delete the five exact source directories.

Archive without -DeleteSources performs and verifies the remote archive but
retains local fields. A retry accepts a same-name remote asset or manifest row
only when every recorded field is identical. It never replaces a conflicting
remote asset or force-pushes.

.EXAMPLE
pwsh tools/archive_openfoam_checkpoint.ps1 -Mode Plan -CasePath C:\OpenFOAM\case -ArchiveRepoPath C:\archive\manifest -TimeDirectory 0.4 -AssetName case_t0p4.tar -ReleaseTag openfoam-checkpoints-2026-08-25

.EXAMPLE
pwsh tools/archive_openfoam_checkpoint.ps1 -Mode Archive -CasePath C:\OpenFOAM\case -ArchiveRepoPath C:\archive\manifest -TimeDirectory 0.4 -AssetName case_t0p4.tar -ReleaseTag openfoam-checkpoints-2026-08-25 -DeleteSources
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('Plan', 'Validate', 'Archive')]
    [string] $Mode,

    [Parameter(Mandatory)]
    [string] $CasePath,

    [Parameter(Mandatory)]
    [string] $ArchiveRepoPath,

    [Parameter(Mandatory)]
    [string] $TimeDirectory,

    [Parameter(Mandatory)]
    [string] $AssetName,

    [Parameter(Mandatory)]
    [string] $ReleaseTag,

    [string] $Branch = 'main',
    [switch] $DeleteSources,
    [switch] $AllowDeleteLatestTwo,

    [Parameter(DontShow)]
    [switch] $LoadFunctionsOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ExpectedFilesPerCopy = 74
$ManifestName = 'checkpoints.csv'
$ArchiveAssetDirectoryName = 'assets'
$SolverLockName = '.thermal_solver_run.lock'
$InvariantCulture = [Globalization.CultureInfo]::InvariantCulture
$NumberStyle = [Globalization.NumberStyles]::Float
$script:ArchiveRoot = $null
$script:GitHubToken = $null
$script:CaseLockProcess = $null
$script:ArchiveRepoLock = $null
$script:StagedAssetGuard = $null

function Resolve-ExistingDirectory {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Label
    )

    $resolved = @(Resolve-Path -LiteralPath $LiteralPath -ErrorAction Stop)
    if ($resolved.Count -ne 1) {
        throw "$Label must resolve to exactly one directory: $LiteralPath"
    }
    $fullPath = [IO.Path]::GetFullPath($resolved[0].Path)
    $fileSystemRoot = [IO.Path]::GetPathRoot($fullPath)
    $trimCharacters = @(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
    if (
        $fullPath.TrimEnd($trimCharacters) -ceq
        $fileSystemRoot.TrimEnd($trimCharacters)
    ) {
        throw "$Label may not be a filesystem root: $fullPath"
    }
    $item = Get-Item -LiteralPath $fullPath -Force
    if (-not $item.PSIsContainer) {
        throw "$Label is not a directory: $($resolved[0].Path)"
    }
    Assert-NoReparsePointInExistingPath -LiteralPath $fullPath -Label $Label
    return $fullPath.TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
}

function Assert-NoReparsePointInExistingPath {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Label
    )

    $current = [IO.Path]::GetFullPath($LiteralPath)
    while ($current) {
        $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Label may not traverse a reparse point: $current"
        }
        $parent = [IO.Path]::GetDirectoryName($current)
        if ([string]::IsNullOrEmpty($parent) -or $parent -ceq $current) {
            break
        }
        $current = $parent
    }
}

function Test-PathIsUnderRoot {
    param(
        [Parameter(Mandatory)] [string] $Candidate,
        [Parameter(Mandatory)] [string] $Root
    )

    $rootPrefix = $Root.TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    $comparison = if ($IsWindows) {
        [StringComparison]::OrdinalIgnoreCase
    } else {
        [StringComparison]::Ordinal
    }
    return $Candidate.StartsWith($rootPrefix, $comparison)
}

function Assert-PathUnderRoot {
    param(
        [Parameter(Mandatory)] [string] $Candidate,
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $Label
    )

    if (-not (Test-PathIsUnderRoot -Candidate $Candidate -Root $Root)) {
        throw "$Label escapes the approved root '$Root': $Candidate"
    }
}

function Assert-SafeLeafName {
    param(
        [Parameter(Mandatory)] [string] $Value,
        [Parameter(Mandatory)] [string] $Label,
        [Parameter(Mandatory)] [string] $Pattern
    )

    if (
        [IO.Path]::IsPathRooted($Value) -or
        $Value.IndexOf([IO.Path]::DirectorySeparatorChar) -ge 0 -or
        $Value.IndexOf([IO.Path]::AltDirectorySeparatorChar) -ge 0 -or
        $Value -eq '.' -or
        $Value -eq '..' -or
        $Value -notmatch $Pattern
    ) {
        throw "$Label must be one safe leaf name; received '$Value'"
    }
}

function ConvertTo-TimeValue {
    param([Parameter(Mandatory)] [string] $Value)

    $parsed = 0.0
    if (-not [double]::TryParse(
        $Value,
        $NumberStyle,
        $InvariantCulture,
        [ref] $parsed
    )) {
        throw "OpenFOAM time directory is not an invariant number: '$Value'"
    }
    if ([double]::IsNaN($parsed) -or [double]::IsInfinity($parsed) -or $parsed -le 0.0) {
        throw "OpenFOAM time directory must be finite and greater than zero: '$Value'"
    }
    return $parsed
}

function Start-CaseArchiveLock {
    param(
        [Parameter(Mandatory)] [string] $CaseRoot,
        [Parameter(Mandatory)] [string] $LockFile
    )

    if (-not $IsWindows) {
        throw 'Archive mode currently requires Windows/WSL so it can share the solver flock'
    }
    $wsl = @(Get-Command wsl.exe -CommandType Application -ErrorAction Stop)[0]
    $wslPathOutput = @(& $wsl.Source --exec wslpath -a -u $LockFile 2>&1)
    if ($LASTEXITCODE -ne 0 -or $wslPathOutput.Count -ne 1) {
        throw "Could not map solver lock into WSL: $($wslPathOutput -join [Environment]::NewLine)"
    }
    $wslLockPath = $wslPathOutput[0].Trim()
    if (-not $wslLockPath.StartsWith('/')) {
        throw "WSL returned an unsafe lock path: $wslLockPath"
    }

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $wsl.Source
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $true
    $startInfo.ArgumentList.Add('--exec')
    $startInfo.ArgumentList.Add('bash')
    $startInfo.ArgumentList.Add('-c')
    $startInfo.ArgumentList.Add(
        'exec 9>>"$1"; if ! flock -n 9; then exit 73; fi; printf "LOCKED\n"; IFS= read -r _ || true')
    $startInfo.ArgumentList.Add('--')
    $startInfo.ArgumentList.Add($wslLockPath)

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw 'Failed to start the WSL lock holder'
    }
    $signalTask = $process.StandardOutput.ReadLineAsync()
    if (-not $signalTask.Wait(5000)) {
        try { $process.StandardInput.Close() } catch {}
        if (-not $process.WaitForExit(1000)) {
            $process.Kill($true)
            $process.WaitForExit()
        }
        $detail = $process.StandardError.ReadToEnd().Trim()
        $process.Dispose()
        throw "Timed out acquiring the solver/archive flock: $detail"
    }
    $signal = $signalTask.Result
    if ($signal -cne 'LOCKED') {
        try { $process.StandardInput.Close() } catch {}
        if (-not $process.WaitForExit(5000)) {
            $process.Kill($true)
            $process.WaitForExit()
        }
        $detail = $process.StandardError.ReadToEnd().Trim()
        $exitCode = $process.ExitCode
        $process.Dispose()
        if ($exitCode -eq 73) {
            throw "The solver/archive flock is already held for case '$CaseRoot'"
        }
        throw "Could not acquire the solver/archive flock (exit $exitCode): $detail"
    }
    return $process
}

function Stop-CaseArchiveLock {
    param($Process)

    if ($null -eq $Process) {
        return
    }
    try {
        if (-not $Process.HasExited) {
            $Process.StandardInput.WriteLine('release')
            $Process.StandardInput.Close()
            if (-not $Process.WaitForExit(5000)) {
                $Process.Kill($true)
                $Process.WaitForExit()
            }
        }
    } finally {
        $Process.Dispose()
    }
}

function Assert-CaseArchiveLockHeld {
    param([Parameter(Mandatory)] $Process)

    if ($null -eq $Process -or $Process.HasExited) {
        $exitDetail = if ($null -ne $Process -and $Process.HasExited) {
            " (lock holder exit code $($Process.ExitCode))"
        } else {
            ''
        }
        throw "The solver/archive flock is no longer held$exitDetail"
    }
}

function Get-Sha256FromOpenStream {
    param([Parameter(Mandatory)] [IO.FileStream] $Stream)

    if (-not $Stream.CanRead -or -not $Stream.CanSeek) {
        throw 'Staged tar guard is not a readable, seekable file stream'
    }
    $Stream.Position = 0
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $digest = $algorithm.ComputeHash($Stream)
    } finally {
        $algorithm.Dispose()
        $Stream.Position = 0
    }
    return [Convert]::ToHexString($digest).ToLowerInvariant()
}

function Start-ArchiveRepoLock {
    param([Parameter(Mandatory)] [string] $GitMetadataPath)

    $lockPath = [IO.Path]::GetFullPath(
        (Join-Path $GitMetadataPath 'thermal_checkpoint_archive.lock'))
    Assert-PathUnderRoot -Candidate $lockPath -Root $GitMetadataPath -Label 'Archive repository lock'
    try {
        return [IO.File]::Open(
            $lockPath,
            [IO.FileMode]::OpenOrCreate,
            [IO.FileAccess]::ReadWrite,
            [IO.FileShare]::None)
    } catch [IO.IOException] {
        throw 'Another checkpoint helper already holds the archive repository lock'
    }
}

function Assert-ArchiveRepoLockHeld {
    param([Parameter(Mandatory)] $LockStream)

    if ($null -eq $LockStream -or -not $LockStream.CanWrite) {
        throw 'The archive repository lock is no longer held'
    }
}

function Set-ArchiveRootForTesting {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    if (-not $LoadFunctionsOnly) {
        throw 'Set-ArchiveRootForTesting is available only in function-load test mode'
    }
    $script:ArchiveRoot = Resolve-ExistingDirectory -LiteralPath $LiteralPath -Label 'Test archive root'
}

function Get-CheckpointTargetPaths {
    param(
        [Parameter(Mandatory)] [string] $CaseRoot,
        [Parameter(Mandatory)] [string] $ExactTime
    )

    $relativePaths = @(
        $ExactTime,
        "processor0/$ExactTime",
        "processor1/$ExactTime",
        "processor2/$ExactTime",
        "processor3/$ExactTime"
    )
    $targets = @()
    foreach ($relativePath in $relativePaths) {
        $candidate = [IO.Path]::GetFullPath((Join-Path $CaseRoot $relativePath))
        Assert-PathUnderRoot -Candidate $candidate -Root $CaseRoot -Label 'Checkpoint target'
        if (-not (Test-Path -LiteralPath $candidate -PathType Container)) {
            throw "Required checkpoint directory does not exist: $candidate"
        }
        $resolved = Resolve-ExistingDirectory -LiteralPath $candidate -Label 'Checkpoint target'
        Assert-PathUnderRoot -Candidate $resolved -Root $CaseRoot -Label 'Resolved checkpoint target'

        $firstPart = ($relativePath -split '[/\\]')[0]
        if ($firstPart.StartsWith('processor', [StringComparison]::Ordinal)) {
            $processorPath = [IO.Path]::GetFullPath((Join-Path $CaseRoot $firstPart))
            $processorItem = Get-Item -LiteralPath $processorPath -Force
            if (($processorItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Processor directory may not be a reparse point: $processorPath"
            }
        }
        $reparseChildren = @(
            Get-ChildItem -LiteralPath $resolved -Recurse -Force -Attributes ReparsePoint
        )
        if ($reparseChildren.Count -ne 0) {
            throw "Checkpoint contains a reparse point: $($reparseChildren[0].FullName)"
        }
        $targets += [pscustomobject]@{
            Relative = $relativePath.Replace('\', '/')
            FullPath = $resolved
        }
    }
    return $targets
}

function Test-CheckpointCopy {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [switch] $Quiet
    )

    if (-not (Test-Path -LiteralPath $LiteralPath -PathType Container)) {
        if ($Quiet) { return $null }
        throw "Checkpoint directory is missing: $LiteralPath"
    }
    $files = @(Get-ChildItem -LiteralPath $LiteralPath -Recurse -Force -File)
    if ($files.Count -ne $ExpectedFilesPerCopy) {
        if ($Quiet) { return $null }
        throw "Checkpoint '$LiteralPath' has $($files.Count) files; expected $ExpectedFilesPerCopy"
    }
    $empty = @($files | Where-Object { $_.Length -le 0 })
    if ($empty.Count -ne 0) {
        if ($Quiet) { return $null }
        throw "Checkpoint '$LiteralPath' contains an empty file: $($empty[0].FullName)"
    }
    $relativeFiles = @(
        $files |
            ForEach-Object {
                [IO.Path]::GetRelativePath($LiteralPath, $_.FullName).Replace('\', '/')
            } |
            Sort-Object
    )
    foreach ($restartField in @('fluid/T', 'fluid/U')) {
        if ($relativeFiles -cnotcontains $restartField) {
            if ($Quiet) { return $null }
            throw "Checkpoint '$LiteralPath' is not restartable; missing $restartField"
        }
    }
    return [pscustomobject]@{
        Files = $files.Count
        Bytes = [int64](($files | Measure-Object -Property Length -Sum).Sum)
        RelativeFiles = $relativeFiles
        ManifestKey = $relativeFiles -join [char]10
    }
}

function Get-ValidatedCheckpoint {
    param(
        [Parameter(Mandatory)] [string] $CaseRoot,
        [Parameter(Mandatory)] [string] $ExactTime
    )

    $targets = @(Get-CheckpointTargetPaths -CaseRoot $CaseRoot -ExactTime $ExactTime)
    $totalFiles = 0
    $totalBytes = [int64]0
    $referenceManifest = $null
    foreach ($target in $targets) {
        $copy = Test-CheckpointCopy -LiteralPath $target.FullPath
        if ($null -eq $referenceManifest) {
            $referenceManifest = $copy.ManifestKey
        } elseif ($copy.ManifestKey -cne $referenceManifest) {
            throw "Checkpoint copies do not have identical relative-file manifests: $($target.FullPath)"
        }
        $totalFiles += $copy.Files
        $totalBytes += $copy.Bytes
    }
    return [pscustomobject]@{
        Time = $ExactTime
        TimeValue = ConvertTo-TimeValue -Value $ExactTime
        Targets = $targets
        SourceFiles = $totalFiles
        SourceBytes = $totalBytes
    }
}

function Get-LatestCompleteCheckpointTimes {
    param([Parameter(Mandatory)] [string] $CaseRoot)

    $complete = @()
    foreach ($directory in (Get-ChildItem -LiteralPath $CaseRoot -Directory -Force)) {
        $value = 0.0
        if (-not [double]::TryParse(
            $directory.Name,
            $NumberStyle,
            $InvariantCulture,
            [ref] $value
        )) {
            continue
        }
        if ([double]::IsNaN($value) -or [double]::IsInfinity($value) -or $value -le 0.0) {
            continue
        }
        try {
            $candidateTargets = @(
                Get-CheckpointTargetPaths -CaseRoot $CaseRoot -ExactTime $directory.Name)
        } catch {
            continue
        }
        $isComplete = $true
        $referenceManifest = $null
        foreach ($target in $candidateTargets) {
            $copy = Test-CheckpointCopy -LiteralPath $target.FullPath -Quiet
            if ($null -eq $copy) {
                $isComplete = $false
                break
            }
            if ($null -eq $referenceManifest) {
                $referenceManifest = $copy.ManifestKey
            } elseif ($copy.ManifestKey -cne $referenceManifest) {
                $isComplete = $false
                break
            }
        }
        if ($isComplete) {
            $complete += [pscustomobject]@{ Name = $directory.Name; Value = $value }
        }
    }
    return @($complete | Sort-Object -Property Value -Descending | Select-Object -First 2)
}

function Invoke-ArchiveGit {
    param(
        [Parameter(Mandatory)] [string[]] $Arguments,
        [switch] $AllowFailure
    )

    $output = @(& git -c "safe.directory=$script:ArchiveRoot" -C $script:ArchiveRoot @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0 -and -not $AllowFailure) {
        throw "git $($Arguments -join ' ') failed with exit code $($exitCode): $($output -join [Environment]::NewLine)"
    }
    return [pscustomobject]@{
        ExitCode = $exitCode
        Text = ($output -join [Environment]::NewLine).Trim()
    }
}

function Invoke-ArchiveGitRaw {
    param([Parameter(Mandatory)] [string[]] $Arguments)

    $git = @(Get-Command git -CommandType Application -ErrorAction Stop)[0]
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $git.Source
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $true
    foreach ($argument in @(
        '-c',
        "safe.directory=$script:ArchiveRoot",
        '-C',
        $script:ArchiveRoot
    ) + $Arguments) {
        $startInfo.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw 'Failed to start git for exact blob validation'
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result.Trim()
    $exitCode = $process.ExitCode
    $process.Dispose()
    if ($exitCode -ne 0) {
        throw "git exact blob validation failed with exit code ${exitCode}: $stderr"
    }
    return $stdout
}

function Get-ArchiveGitStatusRecords {
    $raw = Invoke-ArchiveGitRaw -Arguments @(
        'status',
        '--porcelain=v1',
        '-z',
        '--untracked-files=all'
    )
    $records = @($raw -split [char]0 | Where-Object { $_ })
    $result = @()
    foreach ($record in $records) {
        if ($record.Length -lt 4 -or $record[2] -cne ' ') {
            throw 'Archive repository returned an unsupported porcelain status record'
        }
        $status = $record.Substring(0, 2)
        if ($status -match '[RC]') {
            throw 'Archive repository contains a rename/copy; refusing archive mutation'
        }
        $result += [pscustomobject]@{
            Status = $status
            Path = $record.Substring(3)
        }
    }
    return $result
}

function ConvertTo-GitHubRepositorySlug {
    param([Parameter(Mandatory)] [string] $RemoteUrl)

    $value = $RemoteUrl.Trim()
    $slug = $null
    if ($value -match '^https://github\.com/(?<slug>[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+?)(?:\.git)?/?$') {
        $slug = $Matches.slug
    } elseif ($value -match '^git@github\.com:(?<slug>[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+?)(?:\.git)?$') {
        $slug = $Matches.slug
    } elseif ($value -match '^ssh://git@github\.com/(?<slug>[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+?)(?:\.git)?/?$') {
        $slug = $Matches.slug
    } else {
        throw 'Archive remote is not an exact supported credential-free github.com URL'
    }
    return $slug
}

function Get-GitHubRepositorySlug {
    $fetchUrl = (Invoke-ArchiveGit -Arguments @('remote', 'get-url', 'origin')).Text.Trim()
    $pushUrls = @(
        (Invoke-ArchiveGit -Arguments @(
            'remote',
            'get-url',
            '--push',
            '--all',
            'origin'
        )).Text -split '\r?\n' | Where-Object { $_ }
    )
    if ($pushUrls.Count -ne 1) {
        throw "Archive origin must have exactly one push URL; found $($pushUrls.Count)"
    }
    $pushUrl = $pushUrls[0].Trim()
    $fetchSlug = ConvertTo-GitHubRepositorySlug -RemoteUrl $fetchUrl
    $pushSlug = ConvertTo-GitHubRepositorySlug -RemoteUrl $pushUrl
    if (-not $fetchSlug.Equals($pushSlug, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Archive origin fetch and push repositories differ'
    }
    return [pscustomobject]@{
        Slug = $fetchSlug
        FetchUrl = $fetchUrl
        PushUrl = $pushUrl
    }
}

function Get-GitHubHeaders {
    $newline = [Environment]::NewLine
    $credentialInput = 'protocol=https' + $newline + 'host=github.com' + $newline + $newline
    $credentialLines = @($credentialInput | git credential fill)
    if ($LASTEXITCODE -ne 0) {
        throw 'git credential fill failed for github.com'
    }
    $passwordLine = $credentialLines |
        Where-Object { $_ -like 'password=*' } |
        Select-Object -First 1
    if (-not $passwordLine) {
        throw 'No stored GitHub credential was returned by git credential fill'
    }

    $script:GitHubToken = $passwordLine.Substring(9)
    $credentialLines = $null
    return @{
        'User-Agent' = 'thermal-sim-openfoam-checkpoint-archive'
        'Accept' = 'application/vnd.github+json'
        'Authorization' = "Bearer $script:GitHubToken"
        'X-GitHub-Api-Version' = '2022-11-28'
    }
}

function Invoke-GitHubGet {
    param(
        [Parameter(Mandatory)] [string] $Uri,
        [Parameter(Mandatory)] [hashtable] $Headers
    )
    return Invoke-RestMethod -Method Get -Uri $Uri -Headers $Headers -Verbose:$false -Debug:$false
}

function Get-Release {
    param(
        [Parameter(Mandatory)] [string] $RepositorySlug,
        [Parameter(Mandatory)] [string] $Tag,
        [Parameter(Mandatory)] [hashtable] $Headers
    )

    $encodedTag = [Uri]::EscapeDataString($Tag)
    return Invoke-GitHubGet -Uri "https://api.github.com/repos/$RepositorySlug/releases/tags/$encodedTag" -Headers $Headers
}

function Get-SingleReleaseAsset {
    param(
        [Parameter(Mandatory)] $Release,
        [Parameter(Mandatory)] [string] $Name
    )

    $matches = @($Release.assets | Where-Object { $_.name -ceq $Name })
    if ($matches.Count -gt 1) {
        throw "Release contains duplicate assets named '$Name'"
    }
    if ($matches.Count -eq 0) {
        return $null
    }
    return $matches[0]
}

function Assert-RemoteAssetMatches {
    param(
        [Parameter(Mandatory)] $Asset,
        [Parameter(Mandatory)] [int64] $ExpectedBytes,
        [Parameter(Mandatory)] [string] $ExpectedSha256
    )

    $expectedDigest = "sha256:$ExpectedSha256"
    if ([int64]$Asset.size -ne $ExpectedBytes) {
        throw "GitHub asset byte mismatch: remote=$($Asset.size), local=$ExpectedBytes"
    }
    if (
        [string]::IsNullOrWhiteSpace([string]$Asset.digest) -or
        -not ([string]$Asset.digest).Equals($expectedDigest, [StringComparison]::OrdinalIgnoreCase)
    ) {
        throw "GitHub asset digest mismatch: remote='$($Asset.digest)', local='$expectedDigest'"
    }
    if ([string]$Asset.state -ne 'uploaded') {
        throw "GitHub asset is not in uploaded state ($($Asset.state)). Remove the incomplete same-name asset manually after confirming it is not a valid archive, then resume."
    }
}

function Test-ManifestRowMatches {
    param(
        [Parameter(Mandatory)] $Actual,
        [Parameter(Mandatory)] $Expected
    )

    foreach ($property in $Expected.PSObject.Properties.Name) {
        if ([string]$Actual.$property -cne [string]$Expected.$property) {
            return $false
        }
    }
    return $true
}

function Get-MatchingManifestRows {
    param(
        [Parameter(Mandatory)] [string] $CsvText,
        [Parameter(Mandatory)] $Expected
    )

    $rows = @($CsvText | ConvertFrom-Csv)
    return @($rows | Where-Object {
        (
            $_.case -ceq $Expected.case -and
            $_.time_directory -ceq $Expected.time_directory
        ) -or
        $_.asset -ceq $Expected.asset
    })
}

function Assert-ManifestRowState {
    param(
        [Parameter(Mandatory)] [string] $CsvText,
        [Parameter(Mandatory)] $Expected,
        [Parameter(Mandatory)] [string] $Label,
        [switch] $AllowMissing
    )

    $matches = @(Get-MatchingManifestRows -CsvText $CsvText -Expected $Expected)
    if ($matches.Count -eq 0) {
        if ($AllowMissing) {
            return $false
        }
        throw "$Label does not contain the verified manifest row"
    }
    if (
        $matches.Count -ne 1 -or
        -not (Test-ManifestRowMatches -Actual $matches[0] -Expected $Expected)
    ) {
        throw "$Label contains a conflicting row for time '$($Expected.time_directory)' or asset '$($Expected.asset)'"
    }
    return $true
}

function Assert-ManifestIntegrity {
    param([Parameter(Mandatory)] [string] $CsvText)

    $rows = @($CsvText | ConvertFrom-Csv)
    $timeKeys = @{}
    $assetKeys = @{}
    foreach ($row in $rows) {
        $timeKey = "$($row.case)|$($row.time_directory)"
        if ($timeKeys.ContainsKey($timeKey)) {
            throw "Archive manifest duplicates case/time '$timeKey'"
        }
        if ($assetKeys.ContainsKey([string]$row.asset)) {
            throw "Archive manifest duplicates asset '$($row.asset)'"
        }
        $timeKeys[$timeKey] = $true
        $assetKeys[[string]$row.asset] = $true
    }
}

function Assert-CaseLayout {
    param([Parameter(Mandatory)] [string] $CaseRoot)

    foreach ($required in @('system', 'constant', 'processor0', 'processor1', 'processor2', 'processor3')) {
        $path = [IO.Path]::GetFullPath((Join-Path $CaseRoot $required))
        Assert-PathUnderRoot -Candidate $path -Root $CaseRoot -Label 'Required case path'
        if (-not (Test-Path -LiteralPath $path -PathType Container)) {
            throw "Case is missing required directory: $path"
        }
        $item = Get-Item -LiteralPath $path -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Required case directory may not be a reparse point: $path"
        }
    }

    $processorNames = @(
        Get-ChildItem -LiteralPath $CaseRoot -Directory -Force |
            Where-Object { $_.Name -match '^processor[0-9]+$' } |
            Sort-Object -Property Name |
            ForEach-Object { $_.Name }
    )
    $expected = @('processor0', 'processor1', 'processor2', 'processor3')
    if (($processorNames -join ',') -cne ($expected -join ',')) {
        throw "Case must contain exactly processor0 through processor3; found: $($processorNames -join ', ')"
    }
}

function Assert-NoNumericTimeAliases {
    param([Parameter(Mandatory)] [string] $CaseRoot)

    $spellings = @{}
    $locations = @{}
    $parents = @($CaseRoot)
    0..3 | ForEach-Object {
        $parents += Join-Path $CaseRoot "processor$_"
    }
    foreach ($parent in $parents) {
        foreach ($directory in (Get-ChildItem -LiteralPath $parent -Directory -Force)) {
            $value = 0.0
            if (-not [double]::TryParse(
                $directory.Name,
                $NumberStyle,
                $InvariantCulture,
                [ref] $value
            )) {
                continue
            }
            if ([double]::IsNaN($value) -or [double]::IsInfinity($value) -or $value -le 0.0) {
                continue
            }
            $key = $value.ToString('R', $InvariantCulture)
            if (
                $spellings.ContainsKey($key) -and
                [string]$spellings[$key] -cne $directory.Name
            ) {
                throw "Ambiguous numeric time spellings exist: '$($locations[$key])' and '$($directory.FullName)'"
            }
            $spellings[$key] = $directory.Name
            $locations[$key] = $directory.FullName
        }
    }
}

function Get-CheckpointFingerprint {
    param([Parameter(Mandatory)] $Checkpoint)

    $records = [Collections.Generic.List[string]]::new()
    foreach ($target in $Checkpoint.Targets) {
        foreach ($file in (Get-ChildItem -LiteralPath $target.FullPath -Recurse -Force -File | Sort-Object FullName)) {
            $inside = [IO.Path]::GetRelativePath($target.FullPath, $file.FullName).Replace('\', '/')
            $member = "$($target.Relative)/$inside"
            $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            $records.Add("$member|$($file.Length)|$hash")
        }
    }
    $payload = [Text.Encoding]::UTF8.GetBytes(($records -join [char]10))
    $digest = [Security.Cryptography.SHA256]::HashData($payload)
    return [Convert]::ToHexString($digest).ToLowerInvariant()
}

function Get-StructuredTarEntries {
    param([Parameter(Mandatory)] [string] $TarPath)

    $stream = [IO.File]::Open(
        $TarPath,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    $reader = $null
    try {
        $reader = [System.Formats.Tar.TarReader]::new($stream, $false)
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        $result = [Collections.Generic.List[object]]::new()
        while ($null -ne ($entry = $reader.GetNextEntry($false))) {
            $name = ([string]$entry.Name).Replace('\', '/')
            while ($name.StartsWith('./', [StringComparison]::Ordinal)) {
                $name = $name.Substring(2)
            }
            $name = $name.TrimEnd('/')
            if (
                [string]::IsNullOrEmpty($name) -or
                $name.StartsWith('/') -or
                $name -match '(^|/)\.\.($|/)' -or
                $name -match '^[A-Za-z]:'
            ) {
                throw "Tar contains an unsafe member name: $name"
            }
            if (-not $seen.Add($name)) {
                throw "Tar contains a duplicate normalized member: $name"
            }
            $entryType = $entry.EntryType.ToString()
            $isFile = $entryType -in @('V7RegularFile', 'RegularFile')
            $isDirectory = $entryType -ceq 'Directory'
            if (-not $isFile -and -not $isDirectory) {
                throw "Tar member '$name' has forbidden type '$entryType'"
            }
            if ($isDirectory -and [int64]$entry.Length -ne 0) {
                throw "Tar directory '$name' declares nonzero payload bytes"
            }
            $result.Add([pscustomobject]@{
                Name = $name
                IsFile = $isFile
                IsDirectory = $isDirectory
                Size = [int64]$entry.Length
            })
        }
        return $result
    } finally {
        if ($null -ne $reader) {
            $reader.Dispose()
        } else {
            $stream.Dispose()
        }
    }
}

function Assert-TarListingMatches {
    param(
        [Parameter(Mandatory)] [string] $TarPath,
        [Parameter(Mandatory)] $Checkpoint
    )

    $entries = @(Get-StructuredTarEntries -TarPath $TarPath)
    $expected = @(
        foreach ($target in $Checkpoint.Targets) {
            foreach ($file in (Get-ChildItem -LiteralPath $target.FullPath -Recurse -Force -File)) {
                $inside = [IO.Path]::GetRelativePath($target.FullPath, $file.FullName).Replace('\', '/')
                [pscustomobject]@{
                    Name = "$($target.Relative)/$inside"
                    Size = [int64]$file.Length
                }
            }
        }
    )
    $expected = @($expected | Sort-Object -Property Name)
    $actual = @($entries | Where-Object { $_.IsFile } | Sort-Object -Property Name)
    if ($actual.Count -ne $expected.Count) {
        throw "Staged tar has $($actual.Count) regular files; expected $($expected.Count)"
    }
    for ($index = 0; $index -lt $expected.Count; ++$index) {
        if (
            $actual[$index].Name -cne $expected[$index].Name -or
            [int64]$actual[$index].Size -ne [int64]$expected[$index].Size
        ) {
            throw "Staged tar file/size mismatch at '$($actual[$index].Name)'"
        }
    }
    foreach ($directoryEntry in ($entries | Where-Object { $_.IsDirectory })) {
        $directoryPrefix = $directoryEntry.Name + '/'
        if (-not ($expected | Where-Object {
            $_.Name.StartsWith($directoryPrefix, [StringComparison]::Ordinal)
        } | Select-Object -First 1)) {
            throw "Staged tar contains an unexpected directory member: $($directoryEntry.Name)"
        }
    }
    $actualBytes = [int64](($actual | Measure-Object -Property Size -Sum).Sum)
    if ($actualBytes -ne [int64]$Checkpoint.SourceBytes) {
        throw "Staged tar declares $actualBytes payload bytes; expected $($Checkpoint.SourceBytes)"
    }
}

function Assert-StagedTarContentMatchesCheckpoint {
    param(
        [Parameter(Mandatory)] [string] $TarPath,
        [Parameter(Mandatory)] $Checkpoint,
        [Parameter(Mandatory)] [string] $ExpectedSourceFingerprint,
        [Parameter(Mandatory)] $LockProcess
    )

    Assert-CaseArchiveLockHeld -Process $LockProcess
    $validationGuard = [IO.File]::Open(
        $TarPath,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    try {
    Assert-SafeCheckpointTarForRestore -TarPath $TarPath -ExactTime $Checkpoint.Time
    $parent = Resolve-ExistingDirectory -LiteralPath (
        [IO.Path]::GetDirectoryName($TarPath)) -Label 'Tar verification parent'
    $verificationRoot = [IO.Path]::GetFullPath((Join-Path $parent (
        '.tar_content_verify_' + [guid]::NewGuid().ToString('N'))))
    Assert-PathUnderRoot -Candidate $verificationRoot -Root $parent -Label 'Tar content verification root'
    $requiredBytes = [int64]$Checkpoint.SourceBytes
    $reserveBytes = [int64](64MB)
    $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($verificationRoot))
    if ($drive.AvailableFreeSpace -lt ($requiredBytes + $reserveBytes)) {
        throw "Tar content verification needs $requiredBytes bytes plus a $reserveBytes-byte reserve, but only $($drive.AvailableFreeSpace) bytes are free"
    }
    try {
        New-Item -ItemType Directory -Path $verificationRoot | Out-Null
        $tar = @(Get-Command tar -CommandType Application -ErrorAction Stop)[0]
        & $tar.Source -xf $TarPath -C $verificationRoot
        if ($LASTEXITCODE -ne 0) {
            throw "Could not extract staged tar for content verification: $TarPath"
        }
        $verifiedCheckpoint = Get-ValidatedCheckpoint -CaseRoot $verificationRoot -ExactTime $Checkpoint.Time
        if ((Get-CheckpointFingerprint -Checkpoint $verifiedCheckpoint) -cne $ExpectedSourceFingerprint) {
            throw 'Staged tar payload does not match the current checkpoint source fingerprint'
        }
        Assert-CaseArchiveLockHeld -Process $LockProcess
    } finally {
        if (Test-Path -LiteralPath $verificationRoot) {
            Remove-Item -LiteralPath $verificationRoot -Recurse -Force
        }
    }
    } finally {
        $validationGuard.Dispose()
    }
}

function New-CheckpointTar {
    param(
        [Parameter(Mandatory)] [string] $CaseRoot,
        [Parameter(Mandatory)] $Checkpoint,
        [Parameter(Mandatory)] [string] $FinalPath,
        [Parameter(Mandatory)] [string] $PartialPath,
        [Parameter(Mandatory)] [string] $MetadataPath,
        [Parameter(Mandatory)] [string] $SourceFingerprint,
        [Parameter(Mandatory)] $LockProcess
    )

    if (Test-Path -LiteralPath $PartialPath) {
        Remove-Item -LiteralPath $PartialPath -Force
    }
    if (Test-Path -LiteralPath $FinalPath) {
        Assert-NoReparsePointInExistingPath -LiteralPath $FinalPath -Label 'Existing staged tar'
        if (-not (Test-Path -LiteralPath $MetadataPath -PathType Leaf)) {
            throw "Existing staged tar has no resumable metadata; preserving it for inspection: $FinalPath"
        }
        Assert-NoReparsePointInExistingPath -LiteralPath $MetadataPath -Label 'Staged tar metadata'
        $metadata = Get-Content -LiteralPath $MetadataPath -Raw | ConvertFrom-Json
        $comparison = if ($IsWindows) {
            [StringComparison]::OrdinalIgnoreCase
        } else {
            [StringComparison]::Ordinal
        }
        if (
            [string]$metadata.status -cne 'staged' -or
            [string]$metadata.time_directory -cne $Checkpoint.Time -or
            [string]$metadata.source_fingerprint -cne $SourceFingerprint -or
            -not ([string]$metadata.asset_path).Equals($FinalPath, $comparison) -or
            [string]$metadata.sha256 -notmatch '^[0-9a-f]{64}$'
        ) {
            throw "Existing staged tar metadata conflicts with current sources; preserving it: $FinalPath"
        }
        $existingItem = Get-Item -LiteralPath $FinalPath -Force
        $existingSha = (Get-FileHash -LiteralPath $FinalPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if (
            $existingSha -cne [string]$metadata.sha256 -or
            [int64]$existingItem.Length -ne [int64]$metadata.bytes
        ) {
            throw "Existing staged tar bytes conflict with durable metadata; preserving it: $FinalPath"
        }
        Assert-TarListingMatches -TarPath $FinalPath -Checkpoint $Checkpoint
        Assert-StagedTarContentMatchesCheckpoint -TarPath $FinalPath -Checkpoint $Checkpoint -ExpectedSourceFingerprint $SourceFingerprint -LockProcess $LockProcess
        return $existingItem
    }
    if (Test-Path -LiteralPath $MetadataPath) {
        throw "Staged tar metadata exists without its tar; preserving the resumption evidence: $MetadataPath"
    }
    Assert-CaseArchiveLockHeld -Process $LockProcess
    $tar = @(Get-Command tar -CommandType Application -ErrorAction Stop)[0]
    $tarArguments = @('-cf', $PartialPath, '-C', $CaseRoot)
    $tarArguments += @($Checkpoint.Targets | ForEach-Object { $_.Relative })
    & $tar.Source @tarArguments
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $PartialPath -PathType Leaf)) {
        throw "tar failed while staging '$PartialPath'"
    }
    Assert-TarListingMatches -TarPath $PartialPath -Checkpoint $Checkpoint
    Move-Item -LiteralPath $PartialPath -Destination $FinalPath
    Assert-TarListingMatches -TarPath $FinalPath -Checkpoint $Checkpoint
    Assert-StagedTarContentMatchesCheckpoint -TarPath $FinalPath -Checkpoint $Checkpoint -ExpectedSourceFingerprint $SourceFingerprint -LockProcess $LockProcess
    $finalItem = Get-Item -LiteralPath $FinalPath -Force
    $finalSha = (Get-FileHash -LiteralPath $FinalPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $metadata = [pscustomobject][ordered]@{
        status = 'staged'
        time_directory = $Checkpoint.Time
        asset_path = $FinalPath
        source_fingerprint = $SourceFingerprint
        bytes = [int64]$finalItem.Length
        sha256 = $finalSha
    }
    try {
        Write-DurableJsonFile -ReceiptPath $MetadataPath -Receipt $metadata
    } catch {
        Remove-Item -LiteralPath $FinalPath -Force
        throw
    }
    return $finalItem
}

function Get-QuarantinePaths {
    param(
        [Parameter(Mandatory)] [string] $CaseRoot,
        [Parameter(Mandatory)] [string] $Asset
    )

    $root = [IO.Path]::GetFullPath(
        (Join-Path $CaseRoot '.checkpoint_archive_quarantine'))
    Assert-PathUnderRoot -Candidate $root -Root $CaseRoot -Label 'Quarantine root'
    $groupName = [IO.Path]::GetFileNameWithoutExtension($Asset)
    $group = [IO.Path]::GetFullPath((Join-Path $root $groupName))
    Assert-PathUnderRoot -Candidate $group -Root $root -Label 'Quarantine group'
    $receipt = [IO.Path]::GetFullPath(
        (Join-Path $root "$groupName.archive_receipt.json"))
    Assert-PathUnderRoot -Candidate $receipt -Root $root -Label 'Quarantine receipt'
    return [pscustomobject]@{
        Root = $root
        Group = $group
        Receipt = $receipt
    }
}

function Assert-SafeCheckpointTarForRestore {
    param(
        [Parameter(Mandatory)] [string] $TarPath,
        [Parameter(Mandatory)] [string] $ExactTime,
        [object[]] $TargetMetadata
    )

    $entries = @(Get-StructuredTarEntries -TarPath $TarPath)
    $allowedRoots = @(
        $ExactTime,
        "processor0/$ExactTime",
        "processor1/$ExactTime",
        "processor2/$ExactTime",
        "processor3/$ExactTime"
    )
    foreach ($entry in $entries) {
        $allowed = $false
        foreach ($root in $allowedRoots) {
            if (
                $entry.Name -ceq $root -or
                $entry.Name.StartsWith("$root/", [StringComparison]::Ordinal)
            ) {
                $allowed = $true
                break
            }
        }
        if (-not $allowed) {
            throw "Recovery tar contains a member outside the five exact targets: $($entry.Name)"
        }
    }
    $expectedBytesByRoot = @{}
    if ($null -ne $TargetMetadata) {
        if ($TargetMetadata.Count -ne 5) {
            throw "Recovery metadata must describe exactly five targets; found $($TargetMetadata.Count)"
        }
        foreach ($metadata in $TargetMetadata) {
            $relative = [string]$metadata.relative
            if (
                $allowedRoots -cnotcontains $relative -or
                $expectedBytesByRoot.ContainsKey($relative) -or
                [int64]$metadata.bytes -le 0
            ) {
                throw "Recovery metadata contains an invalid target: '$relative'"
            }
            $expectedBytesByRoot[$relative] = [int64]$metadata.bytes
        }
    }
    $referenceManifest = $null
    foreach ($root in $allowedRoots) {
        $files = @($entries | Where-Object {
            $_.IsFile -and $_.Name.StartsWith("$root/", [StringComparison]::Ordinal)
        })
        if ($files.Count -ne $ExpectedFilesPerCopy) {
            throw "Recovery tar target '$root' has $($files.Count) regular files; expected $ExpectedFilesPerCopy"
        }
        if (@($files | Where-Object { [int64]$_.Size -le 0 }).Count -ne 0) {
            throw "Recovery tar target '$root' contains an empty regular file"
        }
        $manifest = @(
            $files | ForEach-Object { $_.Name.Substring($root.Length + 1) } | Sort-Object
        )
        foreach ($restartField in @('fluid/T', 'fluid/U')) {
            if ($manifest -cnotcontains $restartField) {
                throw "Recovery tar target '$root' is missing restart field $restartField"
            }
        }
        $manifestKey = $manifest -join [char]10
        if ($null -eq $referenceManifest) {
            $referenceManifest = $manifestKey
        } elseif ($manifestKey -cne $referenceManifest) {
            throw 'Recovery tar targets do not have identical relative-file manifests'
        }
        if ($expectedBytesByRoot.Count -ne 0) {
            $declaredBytes = [int64](($files | Measure-Object -Property Size -Sum).Sum)
            if ($declaredBytes -ne [int64]$expectedBytesByRoot[$root]) {
                throw "Recovery tar target '$root' declares $declaredBytes bytes; expected $($expectedBytesByRoot[$root])"
            }
        }
    }
    $allFiles = @($entries | Where-Object { $_.IsFile })
    foreach ($directory in ($entries | Where-Object { $_.IsDirectory })) {
        $prefix = $directory.Name + '/'
        if (-not ($allFiles | Where-Object {
            $_.Name.StartsWith($prefix, [StringComparison]::Ordinal)
        } | Select-Object -First 1)) {
            throw "Recovery tar contains an unexpected empty directory: $($directory.Name)"
        }
    }
}

function Write-DurableJsonFile {
    param(
        [Parameter(Mandatory)] [string] $ReceiptPath,
        [Parameter(Mandatory)] $Receipt
    )

    $parent = [IO.Path]::GetDirectoryName($ReceiptPath)
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        throw "Durable JSON parent is missing: $parent"
    }
    $temporary = Join-Path $parent (
        ([IO.Path]::GetFileName($ReceiptPath)) + '.tmp.' + [guid]::NewGuid().ToString('N'))
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(
        ($Receipt | ConvertTo-Json -Depth 6))
    $stream = $null
    try {
        $stream = [IO.File]::Open(
            $temporary,
            [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write,
            [IO.FileShare]::None)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush($true)
        $stream.Dispose()
        $stream = $null
        [IO.File]::Move($temporary, $ReceiptPath, $true)
    } finally {
        if ($null -ne $stream) {
            $stream.Dispose()
        }
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}

function Write-QuarantineReceipt {
    param(
        [Parameter(Mandatory)] [string] $ReceiptPath,
        [Parameter(Mandatory)] $Receipt
    )

    Write-DurableJsonFile -ReceiptPath $ReceiptPath -Receipt $Receipt
}

function Set-TextFileIfUnchangedDurably {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $ExpectedCurrentText,
        [Parameter(Mandatory)] [string] $NewText
    )

    $stream = [IO.File]::Open(
        $LiteralPath,
        [IO.FileMode]::Open,
        [IO.FileAccess]::ReadWrite,
        [IO.FileShare]::None)
    try {
        $currentBytes = [byte[]]::new($stream.Length)
        $read = 0
        while ($read -lt $currentBytes.Length) {
            $count = $stream.Read($currentBytes, $read, $currentBytes.Length - $read)
            if ($count -le 0) {
                throw "Unexpected end of file while locking '$LiteralPath'"
            }
            $read += $count
        }
        $currentText = [Text.UTF8Encoding]::new($false, $true).GetString($currentBytes)
        if ($currentText -cne $ExpectedCurrentText) {
            throw "File changed concurrently before durable replacement: $LiteralPath"
        }
        $newBytes = [Text.UTF8Encoding]::new($false).GetBytes($NewText)
        $stream.Position = 0
        $stream.SetLength(0)
        $stream.Write($newBytes, 0, $newBytes.Length)
        $stream.Flush($true)
    } finally {
        $stream.Dispose()
    }
}

function Restore-CheckpointSourcesFromTar {
    param(
        [Parameter(Mandatory)] [string] $CaseRoot,
        [Parameter(Mandatory)] [string] $ExactTime,
        [Parameter(Mandatory)] [string] $StagedAsset,
        [Parameter(Mandatory)] [string] $ExpectedAssetSha256,
        [Parameter(Mandatory)] [string] $ExpectedSourceFingerprint,
        [Parameter(Mandatory)] [string] $QuarantineRoot,
        [Parameter(Mandatory)] [string] $QuarantineGroup,
        [Parameter(Mandatory)] [object[]] $TargetMetadata,
        [Parameter(Mandatory)] $LockProcess
    )

    Assert-CaseArchiveLockHeld -Process $LockProcess
    if (-not (Test-Path -LiteralPath $StagedAsset -PathType Leaf)) {
        throw "Staged tar required for source recovery is missing: $StagedAsset"
    }
    Assert-NoReparsePointInExistingPath -LiteralPath $StagedAsset -Label 'Recovery staged tar'
    $recoveryGuard = [IO.File]::Open(
        $StagedAsset,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    try {
    $actualAssetSha = Get-Sha256FromOpenStream -Stream $recoveryGuard
    if ($actualAssetSha -cne $ExpectedAssetSha256) {
        throw "Staged recovery tar SHA-256 mismatch: actual=$actualAssetSha expected=$ExpectedAssetSha256"
    }
    Assert-SafeCheckpointTarForRestore -TarPath $StagedAsset -ExactTime $ExactTime -TargetMetadata $TargetMetadata

    if (-not (Test-Path -LiteralPath $QuarantineGroup -PathType Container)) {
        New-Item -ItemType Directory -Path $QuarantineGroup | Out-Null
    }
    $QuarantineGroup = Resolve-ExistingDirectory -LiteralPath $QuarantineGroup -Label 'Quarantine recovery group'

    $expectedRelatives = @(
        $ExactTime,
        "processor0/$ExactTime",
        "processor1/$ExactTime",
        "processor2/$ExactTime",
        "processor3/$ExactTime"
    )
    if ($TargetMetadata.Count -ne 5) {
        throw "Recovery receipt must describe exactly five targets; found $($TargetMetadata.Count)"
    }
    $metadataByRelative = @{}
    foreach ($entry in $TargetMetadata) {
        $relative = [string]$entry.relative
        if (
            $expectedRelatives -cnotcontains $relative -or
            $metadataByRelative.ContainsKey($relative) -or
            [int64]$entry.bytes -le 0
        ) {
            throw "Recovery receipt contains invalid target metadata: '$relative'"
        }
        $metadataByRelative[$relative] = $entry
    }

    $missing = [Collections.Generic.List[object]]::new()
    foreach ($relative in $expectedRelatives) {
        Assert-CaseArchiveLockHeld -Process $LockProcess
        $source = [IO.Path]::GetFullPath((Join-Path $CaseRoot $relative))
        Assert-PathUnderRoot -Candidate $source -Root $CaseRoot -Label 'Recovered source target'
        if (Test-Path -LiteralPath $source -PathType Container) {
            continue
        }
        $quarantined = [IO.Path]::GetFullPath((Join-Path $QuarantineGroup $relative))
        Assert-PathUnderRoot -Candidate $quarantined -Root $QuarantineGroup -Label 'Quarantined recovery target'
        if (Test-Path -LiteralPath $quarantined -PathType Container) {
            Move-Item -LiteralPath $quarantined -Destination $source
        } else {
            $missing.Add($metadataByRelative[$relative])
        }
    }

    if ($missing.Count -ne 0) {
        $restoreRoot = [IO.Path]::GetFullPath((Join-Path $QuarantineGroup '.restore'))
        Assert-PathUnderRoot -Candidate $restoreRoot -Root $QuarantineGroup -Label 'Quarantine restore root'
        if (Test-Path -LiteralPath $restoreRoot) {
            Remove-Item -LiteralPath $restoreRoot -Recurse -Force
        }
        $requiredBytes = [int64](($missing | Measure-Object -Property bytes -Sum).Sum)
        $reserveBytes = [int64](64MB)
        $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($QuarantineGroup))
        if ($drive.AvailableFreeSpace -lt ($requiredBytes + $reserveBytes)) {
            throw "Selective recovery needs $requiredBytes bytes plus a $reserveBytes-byte reserve, but only $($drive.AvailableFreeSpace) bytes are free"
        }
        New-Item -ItemType Directory -Path $restoreRoot | Out-Null
        $restoreRoot = Resolve-ExistingDirectory -LiteralPath $restoreRoot -Label 'Quarantine restore root'

        $tar = @(Get-Command tar -CommandType Application -ErrorAction Stop)[0]
        $tarArguments = @('-xf', $StagedAsset, '-C', $restoreRoot)
        $tarArguments += @($missing | ForEach-Object { [string]$_.relative })
        & $tar.Source @tarArguments
        if ($LASTEXITCODE -ne 0) {
            throw "Could not selectively extract missing targets from verified staged tar: $StagedAsset"
        }
        foreach ($entry in $missing) {
            Assert-CaseArchiveLockHeld -Process $LockProcess
            $relative = [string]$entry.relative
            $extracted = [IO.Path]::GetFullPath((Join-Path $restoreRoot $relative))
            Assert-PathUnderRoot -Candidate $extracted -Root $restoreRoot -Label 'Extracted recovery target'
            $extracted = Resolve-ExistingDirectory -LiteralPath $extracted -Label 'Extracted recovery target'
            $null = Test-CheckpointCopy -LiteralPath $extracted
            $source = [IO.Path]::GetFullPath((Join-Path $CaseRoot $relative))
            if (Test-Path -LiteralPath $source) {
                throw "Recovered source appeared concurrently: $source"
            }
            Move-Item -LiteralPath $extracted -Destination $source
        }
    }

    Assert-CaseArchiveLockHeld -Process $LockProcess
    $finalCheckpoint = Get-ValidatedCheckpoint -CaseRoot $CaseRoot -ExactTime $ExactTime
    if ((Get-CheckpointFingerprint -Checkpoint $finalCheckpoint) -cne $ExpectedSourceFingerprint) {
        throw 'Recovered source directories do not match the pre-delete source fingerprint'
    }
    Remove-Item -LiteralPath $QuarantineGroup -Recurse -Force
    if (
        (Test-Path -LiteralPath $QuarantineRoot -PathType Container) -and
        -not (Get-ChildItem -LiteralPath $QuarantineRoot -Force)
    ) {
        Remove-Item -LiteralPath $QuarantineRoot -Force
    }
    } finally {
        $recoveryGuard.Dispose()
    }
}

function Restore-PriorCheckpointQuarantine {
    param(
        [Parameter(Mandatory)] [string] $CaseRoot,
        [Parameter(Mandatory)] [string] $ExactTime,
        [Parameter(Mandatory)] [string] $Asset,
        [Parameter(Mandatory)] [string] $ExpectedStagedAsset,
        [Parameter(Mandatory)] $LockProcess
    )

    $paths = Get-QuarantinePaths -CaseRoot $CaseRoot -Asset $Asset
    $groupExists = Test-Path -LiteralPath $paths.Group -PathType Container
    $receiptExists = Test-Path -LiteralPath $paths.Receipt -PathType Leaf
    if (-not $groupExists -and -not $receiptExists) {
        return $null
    }
    if ($groupExists) {
        Assert-NoReparsePointInExistingPath -LiteralPath $paths.Group -Label 'Prior quarantine group'
    }
    $receiptPath = $paths.Receipt
    if (-not $receiptExists) {
        $entries = @(Get-ChildItem -LiteralPath $paths.Group -Force)
        if ($entries.Count -eq 0) {
            Remove-Item -LiteralPath $paths.Group -Force
            if (-not (Get-ChildItem -LiteralPath $paths.Root -Force)) {
                Remove-Item -LiteralPath $paths.Root -Force
            }
            return $null
        }
        throw "Prior quarantine has no recovery receipt: $($paths.Group)"
    }
    Assert-NoReparsePointInExistingPath -LiteralPath $receiptPath -Label 'Quarantine recovery receipt'
    $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    $comparison = if ($IsWindows) {
        [StringComparison]::OrdinalIgnoreCase
    } else {
        [StringComparison]::Ordinal
    }
    if (
        -not ([string]$receipt.case_path).Equals($CaseRoot, $comparison) -or
        [string]$receipt.time_directory -cne $ExactTime -or
        [string]$receipt.asset -cne $Asset -or
        -not ([string]$receipt.staged_asset).Equals($ExpectedStagedAsset, $comparison) -or
        [string]$receipt.sha256 -notmatch '^[0-9a-f]{64}$' -or
        [string]$receipt.source_fingerprint -notmatch '^[0-9a-f]{64}$' -or
        [string]$receipt.remote_commit -notmatch '^[0-9a-f]{40,64}$'
    ) {
        throw "Prior quarantine receipt does not match the explicit archive request: $receiptPath"
    }
    if (
        -not $groupExists -and
        [string]$receipt.phase -in @('sources_deleted', 'completed')
    ) {
        foreach ($relative in @(
            $ExactTime,
            "processor0/$ExactTime",
            "processor1/$ExactTime",
            "processor2/$ExactTime",
            "processor3/$ExactTime"
        )) {
            $source = [IO.Path]::GetFullPath((Join-Path $CaseRoot $relative))
            Assert-PathUnderRoot -Candidate $source -Root $CaseRoot -Label 'Completed deletion target'
            if (Test-Path -LiteralPath $source) {
                throw "Deletion tombstone conflicts with an existing source target: $source"
            }
        }
        return [pscustomobject]@{
            State = 'DeletionCompleted'
            ReceiptPath = $receiptPath
            Receipt = $receipt
        }
    }
    if (-not $groupExists) {
        New-Item -ItemType Directory -Path $paths.Group | Out-Null
        $groupExists = $true
    }
    Restore-CheckpointSourcesFromTar -CaseRoot $CaseRoot -ExactTime $ExactTime -StagedAsset $ExpectedStagedAsset -ExpectedAssetSha256 ([string]$receipt.sha256) -ExpectedSourceFingerprint ([string]$receipt.source_fingerprint) -QuarantineRoot $paths.Root -QuarantineGroup $paths.Group -TargetMetadata @($receipt.targets) -LockProcess $LockProcess
    Remove-Item -LiteralPath $receiptPath -Force
    if (-not (Get-ChildItem -LiteralPath $paths.Root -Force)) {
        Remove-Item -LiteralPath $paths.Root -Force
    }
    return [pscustomobject]@{
        State = 'SourcesRestored'
        ReceiptPath = $receiptPath
        Receipt = $receipt
    }
}

function Remove-CheckpointGroupSafely {
    param(
        [Parameter(Mandatory)] [string] $CaseRoot,
        [Parameter(Mandatory)] $Checkpoint,
        [Parameter(Mandatory)] [string] $Asset,
        [Parameter(Mandatory)] [string] $AssetSha256,
        [Parameter(Mandatory)] [string] $ExpectedSourceFingerprint,
        [Parameter(Mandatory)] [string] $StagedAsset,
        [Parameter(Mandatory)] [string] $RemoteCommit,
        [Parameter(Mandatory)] $LockProcess,
        [IO.FileStream] $AssetGuard,
        [Parameter(DontShow)] [int] $InjectMoveFailureAfter = -1,
        [Parameter(DontShow)] [int] $InjectPurgeFailureAfter = -1,
        [Parameter(DontShow)] [scriptblock] $AssetGuardProbe,
        [Parameter(DontShow)] [scriptblock] $PreMoveSourceProbe
    )

    Assert-CaseArchiveLockHeld -Process $LockProcess
    Assert-NoReparsePointInExistingPath -LiteralPath $StagedAsset -Label 'Deletion-gate staged tar'
    $assetGuard = $AssetGuard
    $ownsAssetGuard = $null -eq $assetGuard
    $transferAssetGuard = $false
    try {
    if ($ownsAssetGuard) {
    $assetGuard = [IO.File]::Open(
        $StagedAsset,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    }
    $guardComparison = if ($IsWindows) {
        [StringComparison]::OrdinalIgnoreCase
    } else {
        [StringComparison]::Ordinal
    }
    if (-not ([IO.Path]::GetFullPath($assetGuard.Name)).Equals(
        [IO.Path]::GetFullPath($StagedAsset),
        $guardComparison)) {
        throw 'Deletion-gate staged tar guard is bound to a different file'
    }
    $deleteGateAssetSha = Get-Sha256FromOpenStream -Stream $assetGuard
    if ($deleteGateAssetSha -cne $AssetSha256) {
        throw "Staged tar changed after remote verification; refusing source deletion: actual=$deleteGateAssetSha expected=$AssetSha256"
    }
    Assert-TarListingMatches -TarPath $StagedAsset -Checkpoint $Checkpoint
    if ($ExpectedSourceFingerprint -notmatch '^[0-9a-f]{64}$') {
        throw 'Expected source fingerprint must be a lowercase SHA-256 digest'
    }
    $paths = Get-QuarantinePaths -CaseRoot $CaseRoot -Asset $Asset
    $quarantineRoot = $paths.Root
    if (-not (Test-Path -LiteralPath $quarantineRoot)) {
        New-Item -ItemType Directory -Path $quarantineRoot | Out-Null
    }
    $quarantineRoot = Resolve-ExistingDirectory -LiteralPath $quarantineRoot -Label 'Quarantine root'

    $quarantineGroup = $paths.Group
    if (Test-Path -LiteralPath $quarantineGroup) {
        throw "A prior quarantine requires recovery before deletion can continue: $quarantineGroup"
    }
    New-Item -ItemType Directory -Path $quarantineGroup | Out-Null
    $quarantineGroup = Resolve-ExistingDirectory -LiteralPath $quarantineGroup -Label 'Quarantine group'

    $mappings = @(
        foreach ($target in $Checkpoint.Targets) {
            $destination = [IO.Path]::GetFullPath(
                (Join-Path $quarantineGroup $target.Relative))
            Assert-PathUnderRoot -Candidate $destination -Root $quarantineGroup -Label 'Quarantine target'
            $targetCopy = Test-CheckpointCopy -LiteralPath $target.FullPath
            [pscustomobject]@{
                source = $target.FullPath
                destination = $destination
                relative = $target.Relative
                bytes = [int64]$targetCopy.Bytes
            }
        }
    )
    $receiptPath = $paths.Receipt
    $receipt = [pscustomobject][ordered]@{
        case_path = $CaseRoot
        time_directory = $Checkpoint.Time
        asset = $Asset
        sha256 = $AssetSha256
        source_fingerprint = $ExpectedSourceFingerprint
        staged_asset = $StagedAsset
        remote_commit = $RemoteCommit
        phase = 'prepared'
        staged_asset_removed = $false
        moved_count = 0
        purged_count = 0
        targets = $mappings
    }

    $moved = [Collections.Generic.List[object]]::new()
    try {
        Write-QuarantineReceipt -ReceiptPath $receiptPath -Receipt $receipt
        if ($null -ne $AssetGuardProbe) {
            & $AssetGuardProbe $StagedAsset
        }
        if ($null -ne $PreMoveSourceProbe) {
            & $PreMoveSourceProbe $Checkpoint
        }
        foreach ($mapping in $mappings) {
            Assert-CaseArchiveLockHeld -Process $LockProcess
            $destinationParent = [IO.Path]::GetDirectoryName([string]$mapping.destination)
            if (-not (Test-Path -LiteralPath $destinationParent)) {
                New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
            }
            if ($moved.Count -eq 0) {
                Assert-CaseArchiveLockHeld -Process $LockProcess
                $deleteGateCheckpoint = Get-ValidatedCheckpoint -CaseRoot $CaseRoot -ExactTime $Checkpoint.Time
                $deleteGateSourceFingerprint = Get-CheckpointFingerprint -Checkpoint $deleteGateCheckpoint
                if ($deleteGateSourceFingerprint -cne $ExpectedSourceFingerprint) {
                    throw "Checkpoint source fingerprint changed immediately before quarantine; refusing deletion: actual=$deleteGateSourceFingerprint expected=$ExpectedSourceFingerprint"
                }
            }
            Move-Item -LiteralPath $mapping.source -Destination $mapping.destination
            $moved.Add($mapping)
            $receipt.phase = 'moving'
            $receipt.moved_count = $moved.Count
            Write-QuarantineReceipt -ReceiptPath $receiptPath -Receipt $receipt
            if ($moved.Count -eq $InjectMoveFailureAfter) {
                throw "Injected move failure after $InjectMoveFailureAfter targets"
            }
        }
        $receipt.phase = 'quarantined'
        Write-QuarantineReceipt -ReceiptPath $receiptPath -Receipt $receipt
    } catch {
        $moveFailure = $_
        if ($moved.Count -eq 0) {
            Remove-Item -LiteralPath $quarantineGroup -Recurse -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath $receiptPath -Force -ErrorAction SilentlyContinue
            if (
                (Test-Path -LiteralPath $quarantineRoot -PathType Container) -and
                -not (Get-ChildItem -LiteralPath $quarantineRoot -Force)
            ) {
                Remove-Item -LiteralPath $quarantineRoot -Force
            }
            throw "No source directory was moved: $($moveFailure.Exception.Message)"
        }
        try {
            Restore-CheckpointSourcesFromTar -CaseRoot $CaseRoot -ExactTime $Checkpoint.Time -StagedAsset $StagedAsset -ExpectedAssetSha256 $AssetSha256 -ExpectedSourceFingerprint $ExpectedSourceFingerprint -QuarantineRoot $quarantineRoot -QuarantineGroup $quarantineGroup -TargetMetadata $mappings -LockProcess $LockProcess
            Remove-Item -LiteralPath $receiptPath -Force
            if (-not (Get-ChildItem -LiteralPath $quarantineRoot -Force)) {
                Remove-Item -LiteralPath $quarantineRoot -Force
            }
        } catch {
            throw "Could not quarantine all five sources, and automatic source restoration also failed. Preserve '$receiptPath': move=$($moveFailure.Exception.Message); restore=$($_.Exception.Message)"
        }
        throw "Could not quarantine all five source directories; all sources were restored from the verified staged tar: $($moveFailure.Exception.Message)"
    }

    try {
        $purged = 0
        foreach ($mapping in $mappings) {
            Assert-CaseArchiveLockHeld -Process $LockProcess
            if ($purged -eq $InjectPurgeFailureAfter) {
                throw "Injected purge failure after $InjectPurgeFailureAfter targets"
            }
            Remove-Item -LiteralPath $mapping.destination -Recurse -Force
            ++$purged
            $receipt.phase = 'purging'
            $receipt.purged_count = $purged
            Write-QuarantineReceipt -ReceiptPath $receiptPath -Receipt $receipt
        }
        Remove-Item -LiteralPath $quarantineGroup -Recurse -Force
        $receipt.phase = 'sources_deleted'
        Write-QuarantineReceipt -ReceiptPath $receiptPath -Receipt $receipt
    } catch {
        $purgeFailure = $_
        try {
            Restore-CheckpointSourcesFromTar -CaseRoot $CaseRoot -ExactTime $Checkpoint.Time -StagedAsset $StagedAsset -ExpectedAssetSha256 $AssetSha256 -ExpectedSourceFingerprint $ExpectedSourceFingerprint -QuarantineRoot $quarantineRoot -QuarantineGroup $quarantineGroup -TargetMetadata $mappings -LockProcess $LockProcess
            Remove-Item -LiteralPath $receiptPath -Force
            if (-not (Get-ChildItem -LiteralPath $quarantineRoot -Force)) {
                Remove-Item -LiteralPath $quarantineRoot -Force
            }
        } catch {
            throw "Remote archive is verified, but quarantine purge and automatic source restoration both failed. Preserve '$receiptPath': purge=$($purgeFailure.Exception.Message); restore=$($_.Exception.Message)"
        }
        throw "Quarantine purge failed, so all source directories were restored from the verified staged tar: $($purgeFailure.Exception.Message)"
    }
    $result = [pscustomobject]@{
        ReceiptPath = $receiptPath
        Receipt = $receipt
        AssetGuard = $assetGuard
    }
    $transferAssetGuard = $true
    return $result
    } finally {
        if ($ownsAssetGuard -and -not $transferAssetGuard -and $null -ne $assetGuard) {
            $assetGuard.Dispose()
        }
    }
}

if ($LoadFunctionsOnly) {
    return
}

try {
    Assert-SafeLeafName -Value $TimeDirectory -Label 'TimeDirectory' -Pattern '^[0-9]+(?:\.[0-9]+)?$'
    $null = ConvertTo-TimeValue -Value $TimeDirectory
    Assert-SafeLeafName -Value $AssetName -Label 'AssetName' -Pattern '^[A-Za-z0-9][A-Za-z0-9._-]*\.tar$'
    Assert-SafeLeafName -Value $ReleaseTag -Label 'ReleaseTag' -Pattern '^[A-Za-z0-9][A-Za-z0-9._-]*$'
    Assert-SafeLeafName -Value $Branch -Label 'Branch' -Pattern '^[A-Za-z0-9][A-Za-z0-9._-]*$'
    if ($AllowDeleteLatestTwo -and -not $DeleteSources) {
        throw 'AllowDeleteLatestTwo is valid only together with DeleteSources'
    }

    $caseRoot = Resolve-ExistingDirectory -LiteralPath $CasePath -Label 'CasePath'
    $script:ArchiveRoot = Resolve-ExistingDirectory -LiteralPath $ArchiveRepoPath -Label 'ArchiveRepoPath'
    if (
        $caseRoot.Equals($script:ArchiveRoot, [StringComparison]::OrdinalIgnoreCase) -or
        (Test-PathIsUnderRoot -Candidate $caseRoot -Root $script:ArchiveRoot) -or
        (Test-PathIsUnderRoot -Candidate $script:ArchiveRoot -Root $caseRoot)
    ) {
        throw 'CasePath and ArchiveRepoPath must be separate, non-nested directories'
    }
    Assert-CaseLayout -CaseRoot $caseRoot
    Assert-NoNumericTimeAliases -CaseRoot $caseRoot

    $gitMetadataPath = [IO.Path]::GetFullPath((Join-Path $script:ArchiveRoot '.git'))
    if (-not (Test-Path -LiteralPath $gitMetadataPath -PathType Container)) {
        throw "ArchiveRepoPath is not a Git working tree: $script:ArchiveRoot"
    }
    Assert-NoReparsePointInExistingPath -LiteralPath $gitMetadataPath -Label 'Archive Git metadata'
    if ($Mode -eq 'Archive') {
        $script:ArchiveRepoLock = Start-ArchiveRepoLock -GitMetadataPath $gitMetadataPath
        Assert-ArchiveRepoLockHeld -LockStream $script:ArchiveRepoLock
    }
    $manifestPath = [IO.Path]::GetFullPath((Join-Path $script:ArchiveRoot $ManifestName))
    Assert-PathUnderRoot -Candidate $manifestPath -Root $script:ArchiveRoot -Label 'Manifest path'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Archive manifest is missing: $manifestPath"
    }
    Assert-NoReparsePointInExistingPath -LiteralPath $manifestPath -Label 'Archive manifest'
    $manifestHeader = Get-Content -LiteralPath $manifestPath -TotalCount 1
    $expectedHeader = 'case,time_directory,asset,source_files,source_bytes,asset_bytes,sha256,github_digest,uploaded_at_utc,status'
    if ($manifestHeader -cne $expectedHeader) {
        throw "Unexpected checkpoints.csv header: '$manifestHeader'"
    }
    Assert-ManifestIntegrity -CsvText (Get-Content -LiteralPath $manifestPath -Raw)

    $caseName = Split-Path -Leaf $caseRoot
    $repositoryIdentity = Get-GitHubRepositorySlug
    $repositorySlug = [string]$repositoryIdentity.Slug
    $approvedPushUrl = [string]$repositoryIdentity.PushUrl
    if ($Mode -eq 'Archive') {
        $lockPath = Join-Path $caseRoot $SolverLockName
        $script:CaseLockProcess = Start-CaseArchiveLock -CaseRoot $caseRoot -LockFile $lockPath
        Assert-CaseArchiveLockHeld -Process $script:CaseLockProcess
    }

    if ($Mode -eq 'Archive') {
        Assert-CaseArchiveLockHeld -Process $script:CaseLockProcess
        $recoveryAssetDirectory = [IO.Path]::GetFullPath(
            (Join-Path $script:ArchiveRoot $ArchiveAssetDirectoryName))
        Assert-PathUnderRoot -Candidate $recoveryAssetDirectory -Root $script:ArchiveRoot -Label 'Recovery asset directory'
        if (Test-Path -LiteralPath $recoveryAssetDirectory -PathType Container) {
            $recoveryAssetDirectory = Resolve-ExistingDirectory -LiteralPath $recoveryAssetDirectory -Label 'Recovery asset directory'
            Assert-PathUnderRoot -Candidate $recoveryAssetDirectory -Root $script:ArchiveRoot -Label 'Resolved recovery asset directory'
        }
        $recoveryAsset = [IO.Path]::GetFullPath(
            (Join-Path $recoveryAssetDirectory $AssetName))
        Assert-PathUnderRoot -Candidate $recoveryAsset -Root $script:ArchiveRoot -Label 'Recovery staged asset'
        if (Test-Path -LiteralPath $recoveryAsset -PathType Leaf) {
            Assert-NoReparsePointInExistingPath -LiteralPath $recoveryAsset -Label 'Recovery staged asset'
            $script:StagedAssetGuard = [IO.File]::Open(
                $recoveryAsset,
                [IO.FileMode]::Open,
                [IO.FileAccess]::Read,
                [IO.FileShare]::Read)
        }
        $recoveryMetadata = "$recoveryAsset.checkpoint.json"
        Assert-PathUnderRoot -Candidate $recoveryMetadata -Root $script:ArchiveRoot -Label 'Recovery staged metadata'
        if (Test-Path -LiteralPath $recoveryMetadata -PathType Leaf) {
            Assert-NoReparsePointInExistingPath -LiteralPath $recoveryMetadata -Label 'Recovery staged metadata'
        }
        $recovery = Restore-PriorCheckpointQuarantine -CaseRoot $caseRoot -ExactTime $TimeDirectory -Asset $AssetName -ExpectedStagedAsset $recoveryAsset -LockProcess $script:CaseLockProcess
        if ($null -ne $recovery -and $recovery.State -ceq 'DeletionCompleted') {
            if (Test-Path -LiteralPath $recoveryAsset -PathType Leaf) {
                $recoverySha = Get-Sha256FromOpenStream -Stream $script:StagedAssetGuard
                if ($recoverySha -cne [string]$recovery.Receipt.sha256) {
                    throw 'Completed-deletion staged tar no longer matches its durable receipt'
                }
                $script:StagedAssetGuard.Dispose()
                $script:StagedAssetGuard = $null
                Remove-Item -LiteralPath $recoveryAsset -Force
            }
            if (Test-Path -LiteralPath $recoveryMetadata -PathType Leaf) {
                Remove-Item -LiteralPath $recoveryMetadata -Force
            }
            $recovery.Receipt.phase = 'completed'
            $recovery.Receipt | Add-Member -MemberType NoteProperty -Name staged_asset_removed -Value $true -Force
            Write-QuarantineReceipt -ReceiptPath $recovery.ReceiptPath -Receipt $recovery.Receipt
            [pscustomobject]@{
                Status = 'ARCHIVED_VERIFIED_AND_DELETED_RESUMED'
                TimeDirectory = $TimeDirectory
                AssetName = $AssetName
                Sha256 = [string]$recovery.Receipt.sha256
                RemoteCommit = [string]$recovery.Receipt.remote_commit
                SourcesDeleted = $true
            } | ConvertTo-Json
            exit 0
        }
    }
    $checkpoint = Get-ValidatedCheckpoint -CaseRoot $caseRoot -ExactTime $TimeDirectory
    if ($checkpoint.SourceFiles -ne 5 * $ExpectedFilesPerCopy) {
        throw "Checkpoint has $($checkpoint.SourceFiles) source files; expected 370"
    }
    $latestTwo = @(Get-LatestCompleteCheckpointTimes -CaseRoot $caseRoot)
    $latestNames = @($latestTwo | ForEach-Object { $_.Name })
    $isLatestTwo = $latestNames -ccontains $TimeDirectory
    if ($DeleteSources -and $isLatestTwo -and -not $AllowDeleteLatestTwo) {
        throw "Refusing deletion: '$TimeDirectory' is one of the latest two complete checkpoints ($($latestNames -join ', ')). Pass -AllowDeleteLatestTwo only for an intentional exception."
    }

    $plan = [pscustomobject]@{
        Mode = $Mode
        CasePath = $caseRoot
        ArchiveRepoPath = $script:ArchiveRoot
        Repository = $repositorySlug
        ReleaseTag = $ReleaseTag
        Branch = $Branch
        TimeDirectory = $TimeDirectory
        AssetName = $AssetName
        SourceFiles = $checkpoint.SourceFiles
        SourceBytes = $checkpoint.SourceBytes
        LatestTwoComplete = $latestNames -join ', '
        TargetIsLatestTwo = $isLatestTwo
        DeleteSources = [bool]$DeleteSources
        Targets = @($checkpoint.Targets | ForEach-Object { $_.FullPath })
    }
    if ($Mode -in @('Plan', 'Validate')) {
        if ($Mode -eq 'Plan') {
            $plan | ConvertTo-Json -Depth 4
        } else {
            Write-Output "VALID: five checkpoint copies contain 74 nonempty files each ($($checkpoint.SourceFiles) files, $($checkpoint.SourceBytes) bytes)."
        }
        exit 0
    }

    $statusRecords = @(Get-ArchiveGitStatusRecords)
    $dirtyPaths = @($statusRecords | ForEach-Object { $_.Path })
    $unrelatedDirty = @($dirtyPaths | Where-Object { $_ -cne $ManifestName })
    if ($unrelatedDirty.Count -ne 0) {
        throw "Archive repository has unrelated changes; refusing to commit: $($unrelatedDirty -join '; ')"
    }
    $currentBranch = (Invoke-ArchiveGit -Arguments @('branch', '--show-current')).Text.Trim()
    if ($currentBranch -cne $Branch) {
        throw "Archive checkout must be on '$Branch'; current branch is '$currentBranch'"
    }

    Assert-CaseArchiveLockHeld -Process $script:CaseLockProcess
    $sourceFingerprint = Get-CheckpointFingerprint -Checkpoint $checkpoint
    $assetDirectory = [IO.Path]::GetFullPath(
        (Join-Path $script:ArchiveRoot $ArchiveAssetDirectoryName))
    Assert-PathUnderRoot -Candidate $assetDirectory -Root $script:ArchiveRoot -Label 'Asset staging directory'
    if (-not (Test-Path -LiteralPath $assetDirectory)) {
        New-Item -ItemType Directory -Path $assetDirectory | Out-Null
    }
    $assetDirectory = Resolve-ExistingDirectory -LiteralPath $assetDirectory -Label 'Asset staging directory'

    $stagedAsset = [IO.Path]::GetFullPath((Join-Path $assetDirectory $AssetName))
    $partialAsset = "$stagedAsset.part"
    $stagedMetadata = "$stagedAsset.checkpoint.json"
    Assert-PathUnderRoot -Candidate $stagedAsset -Root $script:ArchiveRoot -Label 'Staged asset'
    Assert-PathUnderRoot -Candidate $partialAsset -Root $script:ArchiveRoot -Label 'Partial staged asset'
    Assert-PathUnderRoot -Candidate $stagedMetadata -Root $script:ArchiveRoot -Label 'Staged asset metadata'
    foreach ($stagingRelativePath in @(
        "$ArchiveAssetDirectoryName/$AssetName",
        "$ArchiveAssetDirectoryName/$AssetName.part",
        "$ArchiveAssetDirectoryName/$AssetName.checkpoint.json"
    )) {
        $ignored = Invoke-ArchiveGit -Arguments @(
            'check-ignore',
            '--quiet',
            '--',
            $stagingRelativePath
        ) -AllowFailure
        if ($ignored.ExitCode -ne 0) {
            throw "Archive staging path must be ignored by Git: $stagingRelativePath"
        }
    }
    $assetItem = New-CheckpointTar -CaseRoot $caseRoot -Checkpoint $checkpoint -FinalPath $stagedAsset -PartialPath $partialAsset -MetadataPath $stagedMetadata -SourceFingerprint $sourceFingerprint -LockProcess $script:CaseLockProcess
    if ($null -eq $script:StagedAssetGuard) {
        $script:StagedAssetGuard = [IO.File]::Open(
            $stagedAsset,
            [IO.FileMode]::Open,
            [IO.FileAccess]::Read,
            [IO.FileShare]::Read)
    }
    Assert-TarListingMatches -TarPath $stagedAsset -Checkpoint $checkpoint
    Assert-StagedTarContentMatchesCheckpoint -TarPath $stagedAsset -Checkpoint $checkpoint -ExpectedSourceFingerprint $sourceFingerprint -LockProcess $script:CaseLockProcess
    Assert-CaseArchiveLockHeld -Process $script:CaseLockProcess
    $postTarCheckpoint = Get-ValidatedCheckpoint -CaseRoot $caseRoot -ExactTime $TimeDirectory
    $postTarFingerprint = Get-CheckpointFingerprint -Checkpoint $postTarCheckpoint
    if ($postTarFingerprint -cne $sourceFingerprint) {
        throw 'Checkpoint contents changed while the tar was staged'
    }

    if ($assetItem.Length -le 0) {
        throw "Staged tar is empty: $stagedAsset"
    }
    $assetBytes = [int64]$assetItem.Length
    $assetSha256 = Get-Sha256FromOpenStream -Stream $script:StagedAssetGuard

    $headers = Get-GitHubHeaders
    $release = Get-Release -RepositorySlug $repositorySlug -Tag $ReleaseTag -Headers $headers
    $remoteAsset = Get-SingleReleaseAsset -Release $release -Name $AssetName
    if ($null -eq $remoteAsset) {
        $uploadBase = ([string]$release.upload_url) -replace '\{.*$', ''
        $uploadUri = [Uri]$uploadBase
        $releasePathPrefix = "/repos/$repositorySlug/releases/"
        $releasePathMatches = $uploadUri.AbsolutePath.StartsWith(
            $releasePathPrefix,
            [StringComparison]::OrdinalIgnoreCase)
        $releasePathTail = if ($releasePathMatches) {
            $uploadUri.AbsolutePath.Substring($releasePathPrefix.Length)
        } else {
            ''
        }
        if (
            $uploadUri.Scheme -cne 'https' -or
            -not $uploadUri.Host.Equals(
                'uploads.github.com',
                [StringComparison]::OrdinalIgnoreCase) -or
            -not $releasePathMatches -or
            $releasePathTail -notmatch '^[0-9]+/assets$'
        ) {
            throw "GitHub returned an unexpected upload endpoint: $uploadBase"
        }
        $uploadWithName = "${uploadBase}?name=$([Uri]::EscapeDataString($AssetName))"
        Invoke-RestMethod -Method Post -Uri $uploadWithName -Headers $headers -ContentType 'application/octet-stream' -InFile $stagedAsset -Verbose:$false -Debug:$false | Out-Null
        $release = Get-Release -RepositorySlug $repositorySlug -Tag $ReleaseTag -Headers $headers
        $remoteAsset = Get-SingleReleaseAsset -Release $release -Name $AssetName
        if ($null -eq $remoteAsset) {
            throw "Uploaded asset is absent from the release: $AssetName"
        }
    }
    Assert-RemoteAssetMatches -Asset $remoteAsset -ExpectedBytes $assetBytes -ExpectedSha256 $assetSha256

    $remoteDigest = "sha256:$assetSha256"
    $uploadedAt = ([datetime]$remoteAsset.created_at).ToUniversalTime().ToString(
        'yyyy-MM-ddTHH:mm:ssZ',
        $InvariantCulture)
    $expectedRow = [pscustomobject][ordered]@{
        case = $caseName
        time_directory = $TimeDirectory
        asset = $AssetName
        source_files = [string]$checkpoint.SourceFiles
        source_bytes = [string]$checkpoint.SourceBytes
        asset_bytes = [string]$assetBytes
        sha256 = $assetSha256
        github_digest = $remoteDigest
        uploaded_at_utc = $uploadedAt
        status = 'verified'
    }
    $csvLines = @($expectedRow | ConvertTo-Csv -NoTypeInformation)
    $expectedCsvLine = $csvLines[1]

    $encodedBranch = [Uri]::EscapeDataString($Branch)
    $preCommitLocalHead = (Invoke-ArchiveGit -Arguments @('rev-parse', 'HEAD')).Text.Trim()
    $preCommitRemoteBranch = Invoke-GitHubGet -Uri "https://api.github.com/repos/$repositorySlug/branches/$encodedBranch" -Headers $headers
    $preCommitRemoteHead = ([string]$preCommitRemoteBranch.commit.sha).Trim()
    $headManifestResult = Invoke-ArchiveGit -Arguments @('show', "HEAD:$ManifestName") -AllowFailure
    $rowInHead = $false
    if ($headManifestResult.ExitCode -eq 0) {
        $rowInHead = Assert-ManifestRowState -CsvText $headManifestResult.Text -Expected $expectedRow -Label 'HEAD manifest' -AllowMissing
    }
    if ($preCommitRemoteHead -cne $preCommitLocalHead) {
        if (-not $rowInHead) {
            throw "Remote $Branch changed before manifest commit; synchronize the archive checkout and resume"
        }
        $aheadCount = (Invoke-ArchiveGit -Arguments @(
            'rev-list',
            '--count',
            "$preCommitRemoteHead..$preCommitLocalHead"
        )).Text.Trim()
        $changedPaths = @(
            (Invoke-ArchiveGit -Arguments @(
                'diff-tree',
                '--no-commit-id',
                '--name-only',
                '-r',
                $preCommitLocalHead
            )).Text -split '\r?\n' | Where-Object { $_ }
        )
        if ($aheadCount -cne '1' -or ($changedPaths -join ',') -cne $ManifestName) {
            throw 'Local archive branch is ahead by changes other than one manifest-only commit'
        }
        $preCommitAncestor = Invoke-ArchiveGit -Arguments @(
            'merge-base',
            '--is-ancestor',
            $preCommitRemoteHead,
            $preCommitLocalHead
        ) -AllowFailure
        if ($preCommitAncestor.ExitCode -ne 0) {
            throw "Local verified manifest commit is not a fast-forward of remote $Branch"
        }
        $remoteBaseManifest = Invoke-GitHubGet -Uri "https://api.github.com/repos/$repositorySlug/contents/${ManifestName}?ref=$preCommitRemoteHead" -Headers $headers
        $remoteBaseText = [Text.Encoding]::UTF8.GetString(
            [Convert]::FromBase64String(([string]$remoteBaseManifest.content -replace '\s', '')))
        $remoteBaseNormalized = ($remoteBaseText -replace '\r\n?', [char]10).TrimEnd([char]10)
        $headNormalized = ($headManifestResult.Text -replace '\r\n?', [char]10).TrimEnd([char]10)
        if ($headNormalized -cne ($remoteBaseNormalized + [char]10 + $expectedCsvLine)) {
            throw 'Local ahead manifest commit is not exactly the expected appended row'
        }
    }

    Assert-ArchiveRepoLockHeld -LockStream $script:ArchiveRepoLock
    $freshLocalHead = (Invoke-ArchiveGit -Arguments @('rev-parse', 'HEAD')).Text.Trim()
    if ($freshLocalHead -cne $preCommitLocalHead) {
        throw 'Local archive HEAD changed after remote verification; refusing manifest mutation'
    }
    $freshStatusRecords = @(Get-ArchiveGitStatusRecords)
    $dirtyPaths = @($freshStatusRecords | ForEach-Object { $_.Path })
    $unrelatedDirty = @($dirtyPaths | Where-Object { $_ -cne $ManifestName })
    if ($unrelatedDirty.Count -ne 0) {
        throw "Archive repository changed during upload: $($unrelatedDirty -join ', ')"
    }
    $freshHeadManifest = Invoke-ArchiveGitRaw -Arguments @(
        'show',
        "$freshLocalHead`:$ManifestName"
    )
    $localManifestText = [IO.File]::ReadAllText($manifestPath)
    if ($dirtyPaths.Count -eq 0 -and $localManifestText -cne $freshHeadManifest) {
        throw 'checkpoints.csv bytes differ from HEAD despite clean status; refusing commit'
    }
    $rowInHead = Assert-ManifestRowState -CsvText $freshHeadManifest -Expected $expectedRow -Label 'Fresh HEAD manifest' -AllowMissing
    $intendedManifestText = $null
    $rowAlreadyPresent = Assert-ManifestRowState -CsvText $localManifestText -Expected $expectedRow -Label 'Local working manifest' -AllowMissing
    if ($rowInHead) {
        if ($dirtyPaths.Count -ne 0 -or $localManifestText -cne $freshHeadManifest) {
            throw 'Verified manifest row is committed, but the working tree changed afterward'
        }
    } elseif (-not $rowAlreadyPresent) {
        if ($dirtyPaths.Count -ne 0) {
            throw 'checkpoints.csv is already modified but does not contain this exact verified row'
        }
        $normalizedBase = ($localManifestText -replace '\r\n?', [char]10).TrimEnd([char]10)
        $nextManifestText = $normalizedBase + [char]10 + $expectedCsvLine + [char]10
        Assert-ManifestIntegrity -CsvText $nextManifestText
        Assert-ManifestRowState -CsvText $nextManifestText -Expected $expectedRow -Label 'Prospective local manifest' | Out-Null
        $intendedManifestText = $nextManifestText
        Set-TextFileIfUnchangedDurably -LiteralPath $manifestPath -ExpectedCurrentText $localManifestText -NewText $intendedManifestText
    } else {
        if (($dirtyPaths -join ',') -cne $ManifestName) {
            throw 'Uncommitted verified row is not the only archive working-tree change'
        }
        $headNormalized = ($freshHeadManifest -replace '\r\n?', [char]10).TrimEnd([char]10)
        $workingNormalized = ($localManifestText -replace '\r\n?', [char]10).TrimEnd([char]10)
        if ($workingNormalized -cne ($headNormalized + [char]10 + $expectedCsvLine)) {
            throw 'Dirty checkpoints.csv contains changes beyond the one exact appended verified row'
        }
        $canonicalManifestText = $workingNormalized + [char]10
        Assert-ManifestIntegrity -CsvText $canonicalManifestText
        $intendedManifestText = $canonicalManifestText
        Set-TextFileIfUnchangedDurably -LiteralPath $manifestPath -ExpectedCurrentText $localManifestText -NewText $intendedManifestText
    }

    if (-not $rowInHead) {
        Assert-ArchiveRepoLockHeld -LockStream $script:ArchiveRepoLock
        if ($null -eq $intendedManifestText) {
            throw 'No exact intended manifest was constructed for the new commit'
        }
        if ([IO.File]::ReadAllText($manifestPath) -cne $intendedManifestText) {
            throw 'checkpoints.csv changed concurrently after validation; refusing commit'
        }
        $expectedManifestBlob = (Invoke-ArchiveGit -Arguments @(
            'hash-object',
            '--no-filters',
            '--',
            $manifestPath
        )).Text.Trim()
        Invoke-ArchiveGit -Arguments @('add', '--', $ManifestName) | Out-Null
        Invoke-ArchiveGit -Arguments @(
            'commit',
            '-m',
            "Record verified OpenFOAM $TimeDirectory checkpoint asset"
        ) | Out-Null
    }

    $localHead = (Invoke-ArchiveGit -Arguments @('rev-parse', 'HEAD')).Text.Trim()
    if ($rowInHead -and $localHead -cne $preCommitLocalHead) {
        throw 'Local archive HEAD changed after the already-committed row was verified; refusing push'
    }
    if (-not $rowInHead) {
        $createdParent = (Invoke-ArchiveGit -Arguments @(
            'rev-parse',
            "$localHead^"
        )).Text.Trim()
        if ($createdParent -cne $preCommitLocalHead) {
            throw 'Manifest commit parent changed concurrently; refusing push'
        }
        $createdPaths = @(
            (Invoke-ArchiveGit -Arguments @(
                'diff-tree',
                '--no-commit-id',
                '--name-only',
                '-r',
                $localHead
            )).Text -split '\r?\n' | Where-Object { $_ }
        )
        if (($createdPaths -join ',') -cne $ManifestName) {
            throw 'Manifest commit contains paths other than checkpoints.csv; refusing push'
        }
        $committedManifestBlob = (Invoke-ArchiveGit -Arguments @(
            'rev-parse',
            "$localHead`:$ManifestName"
        )).Text.Trim()
        if ($committedManifestBlob -cne $expectedManifestBlob) {
            throw 'Committed checkpoints.csv differs from the fully validated manifest bytes'
        }
        $committedManifest = Invoke-ArchiveGitRaw -Arguments @(
            'show',
            "$localHead`:$ManifestName"
        )
        if ($committedManifest -cne $intendedManifestText) {
            throw 'Committed checkpoints.csv differs from the exact in-memory intended manifest'
        }
        Assert-ManifestIntegrity -CsvText $committedManifest
        Assert-ManifestRowState -CsvText $committedManifest -Expected $expectedRow -Label 'Committed manifest' | Out-Null
    }
    Assert-ArchiveRepoLockHeld -LockStream $script:ArchiveRepoLock
    $branchHead = (Invoke-ArchiveGit -Arguments @('rev-parse', $Branch)).Text.Trim()
    if ($branchHead -cne $localHead) {
        throw 'Local archive branch changed concurrently; refusing push'
    }
    $remoteBranch = Invoke-GitHubGet -Uri "https://api.github.com/repos/$repositorySlug/branches/$encodedBranch" -Headers $headers
    $remoteHead = ([string]$remoteBranch.commit.sha).Trim()
    if ($remoteHead -cne $localHead) {
        $ancestor = Invoke-ArchiveGit -Arguments @(
            'merge-base',
            '--is-ancestor',
            $remoteHead,
            $localHead
        ) -AllowFailure
        if ($ancestor.ExitCode -ne 0) {
            throw "Remote $Branch ($remoteHead) is not an ancestor of local HEAD ($localHead); refusing push"
        }
        $freshRepositoryIdentity = Get-GitHubRepositorySlug
        if (
            -not ([string]$freshRepositoryIdentity.Slug).Equals(
                $repositorySlug,
                [StringComparison]::OrdinalIgnoreCase) -or
            [string]$freshRepositoryIdentity.PushUrl -cne $approvedPushUrl
        ) {
            throw 'Archive origin changed after validation; refusing push'
        }
        Invoke-ArchiveGit -Arguments @(
            'push',
            $approvedPushUrl,
            "$localHead`:refs/heads/$Branch"
        ) | Out-Null
    }

    $remoteBranch = Invoke-GitHubGet -Uri "https://api.github.com/repos/$repositorySlug/branches/$encodedBranch" -Headers $headers
    $verifiedRemoteHead = ([string]$remoteBranch.commit.sha).Trim()
    if ($verifiedRemoteHead -cne $localHead) {
        throw "Remote $Branch did not advance to the manifest commit: remote=$verifiedRemoteHead local=$localHead"
    }
    $remoteManifest = Invoke-GitHubGet -Uri "https://api.github.com/repos/$repositorySlug/contents/${ManifestName}?ref=$encodedBranch" -Headers $headers
    $remoteManifestText = [Text.Encoding]::UTF8.GetString(
        [Convert]::FromBase64String(([string]$remoteManifest.content -replace '\s', '')))
    Assert-ManifestRowState -CsvText $remoteManifestText -Expected $expectedRow -Label "Remote $Branch manifest" | Out-Null

    $release = Get-Release -RepositorySlug $repositorySlug -Tag $ReleaseTag -Headers $headers
    $remoteAsset = Get-SingleReleaseAsset -Release $release -Name $AssetName
    if ($null -eq $remoteAsset) {
        throw "Verified release asset disappeared before deletion gate: $AssetName"
    }
    Assert-RemoteAssetMatches -Asset $remoteAsset -ExpectedBytes $assetBytes -ExpectedSha256 $assetSha256

    $postArchiveStatus = @(Get-ArchiveGitStatusRecords)
    if ($postArchiveStatus.Count -ne 0) {
        throw "Archive repository is not clean after verified push: $($postArchiveStatus.Path -join ', ')"
    }

    $deletionJournal = $null
    if ($DeleteSources) {
        Assert-CaseArchiveLockHeld -Process $script:CaseLockProcess
        $checkpoint = Get-ValidatedCheckpoint -CaseRoot $caseRoot -ExactTime $TimeDirectory
        $newFingerprint = Get-CheckpointFingerprint -Checkpoint $checkpoint
        if ($newFingerprint -cne $sourceFingerprint) {
            throw 'Checkpoint contents changed after staging; refusing source deletion'
        }
        Assert-NoNumericTimeAliases -CaseRoot $caseRoot
        $latestTwo = @(Get-LatestCompleteCheckpointTimes -CaseRoot $caseRoot)
        $latestNames = @($latestTwo | ForEach-Object { $_.Name })
        if (($latestNames -ccontains $TimeDirectory) -and -not $AllowDeleteLatestTwo) {
            throw "Deletion gate changed: '$TimeDirectory' is now one of the latest two complete checkpoints ($($latestNames -join ', '))"
        }

        foreach ($target in $checkpoint.Targets) {
            $resolvedTarget = Resolve-ExistingDirectory -LiteralPath $target.FullPath -Label 'Deletion target'
            Assert-PathUnderRoot -Candidate $resolvedTarget -Root $caseRoot -Label 'Deletion target'
        }
        $deletionJournal = Remove-CheckpointGroupSafely -CaseRoot $caseRoot -Checkpoint $checkpoint -Asset $AssetName -AssetSha256 $assetSha256 -ExpectedSourceFingerprint $sourceFingerprint -StagedAsset $stagedAsset -RemoteCommit $localHead -LockProcess $script:CaseLockProcess -AssetGuard $script:StagedAssetGuard
    }
    if ($null -ne $deletionJournal) {
        $script:StagedAssetGuard.Dispose()
        $script:StagedAssetGuard = $null
        Remove-Item -LiteralPath $stagedAsset -Force
        Remove-Item -LiteralPath $stagedMetadata -Force
        $deletionJournal.Receipt.phase = 'completed'
        $deletionJournal.Receipt | Add-Member -MemberType NoteProperty -Name staged_asset_removed -Value $true -Force
        Write-QuarantineReceipt -ReceiptPath $deletionJournal.ReceiptPath -Receipt $deletionJournal.Receipt
    }

    [pscustomobject]@{
        Status = if ($DeleteSources) {
            'ARCHIVED_VERIFIED_AND_DELETED'
        } else {
            'ARCHIVED_VERIFIED_RETAINED'
        }
        TimeDirectory = $TimeDirectory
        AssetName = $AssetName
        AssetBytes = $assetBytes
        Sha256 = $assetSha256
        RemoteCommit = $localHead
        SourcesDeleted = [bool]$DeleteSources
        LocalStagedAsset = if ($DeleteSources) { $null } else { $stagedAsset }
    } | ConvertTo-Json
    exit 0
} catch {
    Write-Error "Checkpoint archive failed before the deletion gate or during an explicitly authorized delete: $($_.Exception.Message) [$($_.InvocationInfo.ScriptLineNumber)]"
    exit 1
} finally {
    $script:GitHubToken = $null
    if ($null -ne $script:StagedAssetGuard) {
        $script:StagedAssetGuard.Dispose()
        $script:StagedAssetGuard = $null
    }
    Stop-CaseArchiveLock -Process $script:CaseLockProcess
    $script:CaseLockProcess = $null
    if ($null -ne $script:ArchiveRepoLock) {
        $script:ArchiveRepoLock.Dispose()
        $script:ArchiveRepoLock = $null
    }
}
