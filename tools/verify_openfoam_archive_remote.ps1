#requires -Version 7.0

<#
.SYNOPSIS
Read-only verification of the approved 2026-08-26 OpenFOAM archive set.

.DESCRIPTION
Queries the private GitHub repository and release, compares both remote
manifest blobs byte-for-byte with remote main, verifies the exact 28 approved
asset records (size, SHA-256 digest, and uploaded state), and audits the local
deletion/preservation boundary. It never uploads, commits, moves, or deletes.
#>

[CmdletBinding()]
param(
    [string] $ArchiveRepoPath = (Join-Path $PSScriptRoot '..\openfoam_archive_remote'),

    [string] $SourceRoot =
        'C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618',

    [string] $OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepositorySlug = 'hughes6/thermal-sim-openfoam-archive'
$ReleaseTag = 'openfoam-checkpoints-2026-08-25'
$CheckpointManifestPath = 'checkpoints.csv'
$LegacyManifestPath = 'manifests/openfoam-legacy-assets-2026-08-26.csv'
$ActiveCaseName = 'new_model_updated_openfoam_export_test'
$Utf8Strict = [Text.UTF8Encoding]::new($false, $true)
$Utf8NoBom = [Text.UTF8Encoding]::new($false)
$PathComparison = if ($IsWindows) {
    [StringComparison]::OrdinalIgnoreCase
} else {
    [StringComparison]::Ordinal
}

$CheckpointTimes = @(
    '0.40000000000000002',
    '0.5',
    '0.59999999999999998',
    '0.69999999999999996',
    '0.80000000000000004',
    '0.90000000000000002',
    '1',
    '1.1000000000000001',
    '1.2',
    '1.3',
    '1.3999999999999999'
)

$LegacyCases = @(
    'lab_screening_export',
    'lab_screening_export_clean',
    'new_model_airflow_mapping_22p5mm',
    'new_model_airflow_mapping_25mm',
    'new_model_airflow_mapping_30mm',
    'new_model_airflow_mapping_40mm',
    'new_model_openfoam_export_test',
    'thermal_lab_corrected_curves',
    'thermal_lab_fan_axial',
    'thermal_lab_fan_axial_intersection',
    'thermal_lab_fan_baffles',
    'thermal_lab_fan_baffles_runner_v2',
    'thermal_lab_runner_v3'
)

$LegacySnapshots = @(
    'decomposition_auto_case',
    'dt_baseline_0p0005_case',
    'dt_variant_0p00075_case',
    'pimple_outer2_auto_case'
)

$PreservedSnapshots = @(
    'thermal_dt_screen_20_case',
    'thermal_dt_screen_25_case',
    'thermal_dt_screen_30_case',
    'thermal_dt_screen_40_case',
    'thermal_dt_screen_60_case'
)

$PreservedTimes = @('0', '1.5', '1.6000000000000001')

function Assert-True {
    param(
        [Parameter(Mandatory)] [bool] $Condition,
        [Parameter(Mandatory)] [string] $Message
    )
    if (-not $Condition) { throw $Message }
}

function Assert-ExactSetLocal {
    param(
        [Parameter(Mandatory)] [object[]] $Actual,
        [Parameter(Mandatory)] [object[]] $Expected,
        [Parameter(Mandatory)] [string] $Label
    )
    $actualStrings = [string[]]@($Actual | ForEach-Object { [string]$_ })
    $expectedStrings = [string[]]@($Expected | ForEach-Object { [string]$_ })
    $actualDistinct = @($actualStrings | Sort-Object -Unique -CaseSensitive)
    $expectedDistinct = @($expectedStrings | Sort-Object -Unique -CaseSensitive)
    if (
        $actualDistinct.Count -ne $actualStrings.Count -or
        $expectedDistinct.Count -ne $expectedStrings.Count -or
        ($actualDistinct -join "`n") -cne ($expectedDistinct -join "`n")
    ) {
        throw "$Label is not the expected exact set"
    }
}

function Get-Sha256BytesLocal {
    param([Parameter(Mandatory)] [byte[]] $Bytes)
    return [Convert]::ToHexString(
        [Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}

function Get-RemoteContentBytesLocal {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [string] $Commit,
        [Parameter(Mandatory)] [hashtable] $Headers
    )
    $encodedPath = ($Path -split '/' | ForEach-Object {
        [Uri]::EscapeDataString($_)
    }) -join '/'
    $encodedCommit = [Uri]::EscapeDataString($Commit)
    $response = Invoke-GitHubGet `
        -Uri "https://api.github.com/repos/$RepositorySlug/contents/${encodedPath}?ref=$encodedCommit" `
        -Headers $Headers
    Assert-True `
        -Condition ([string]$response.type -ceq 'file') `
        -Message "Remote content is not a file: $Path"
    return [Convert]::FromBase64String(
        ([string]$response.content -replace '\s', ''))
}

function Get-BlobBytesLocal {
    param(
        [Parameter(Mandatory)] [string] $Commit,
        [Parameter(Mandatory)] [string] $Path
    )
    return Invoke-ArchiveGitBytes -Arguments @(
        'cat-file', 'blob', "$Commit`:$Path")
}

function Get-NumericDirectoryNamesLocal {
    param([Parameter(Mandatory)] [string] $Path)
    $names = [Collections.Generic.List[string]]::new()
    foreach ($directory in Get-ChildItem -LiteralPath $Path -Directory -Force) {
        $number = 0.0
        if ([double]::TryParse(
            $directory.Name,
            [Globalization.NumberStyles]::Float,
            [Globalization.CultureInfo]::InvariantCulture,
            [ref]$number
        )) {
            $names.Add($directory.Name)
        }
    }
    return $names.ToArray()
}

function Assert-RemoteRowLocal {
    param(
        [Parameter(Mandatory)] $Row,
        [Parameter(Mandatory)] [object[]] $Assets
    )
    Assert-True `
        -Condition ([string]$Row.status -ceq 'verified') `
        -Message "Manifest row is not verified: $($Row.asset)"
    $sha = [string]$Row.sha256
    Assert-True `
        -Condition ($sha -cmatch '^[0-9a-f]{64}$') `
        -Message "Manifest SHA-256 is malformed: $($Row.asset)"
    Assert-True `
        -Condition ([string]$Row.github_digest -ceq "sha256:$sha") `
        -Message "Manifest digest disagrees with SHA-256: $($Row.asset)"
    $asset = Get-SingleReleaseAsset -Assets $Assets -Name ([string]$Row.asset)
    Assert-True `
        -Condition ($null -ne $asset) `
        -Message "Remote asset is missing: $($Row.asset)"
    Assert-RemoteAssetMatches `
        -Asset $asset `
        -ExpectedBytes ([int64]$Row.asset_bytes) `
        -ExpectedSha256 $sha
    $remoteUpdated = [DateTimeOffset]$asset.updated_at
    $remoteCreated = [DateTimeOffset]$asset.created_at
    $manifestUpdated = [DateTimeOffset]::Parse(
        [string]$Row.uploaded_at_utc,
        [Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::AssumeUniversal)
    Assert-True `
        -Condition (
            $remoteUpdated.ToUniversalTime() -eq $manifestUpdated.ToUniversalTime() -or
            $remoteCreated.ToUniversalTime() -eq $manifestUpdated.ToUniversalTime()
        ) `
        -Message "Remote upload timestamp disagrees with manifest: $($Row.asset); created=$($remoteCreated.ToString('o')); updated=$($remoteUpdated.ToString('o')); manifest=$($manifestUpdated.ToString('o'))"
}

$RequestedArchiveRepoPath = $ArchiveRepoPath
$RequestedSourceRoot = $SourceRoot
$RequestedOutputPath = $OutputPath
$helper = Join-Path $PSScriptRoot 'archive_openfoam_legacy_asset.ps1'
. $helper -LoadFunctionsOnly

try {
    $resolvedArchive = [IO.Path]::GetFullPath(
        (Resolve-Path -LiteralPath $RequestedArchiveRepoPath -ErrorAction Stop).Path)
    $resolvedSource = [IO.Path]::GetFullPath(
        (Resolve-Path -LiteralPath $RequestedSourceRoot -ErrorAction Stop).Path)
    $script:ArchiveRoot = $resolvedArchive

    $identity = Get-GitHubRepositoryIdentity
    Assert-True `
        -Condition ($identity.Slug.Equals(
            $RepositorySlug, [StringComparison]::OrdinalIgnoreCase)) `
        -Message 'Archive repository identity changed'
    Assert-True `
        -Condition (@(Get-ArchiveGitStatusRecords).Count -eq 0) `
        -Message 'Archive checkout is not clean'

    $headers = Get-GitHubHeaders
    $repository = Invoke-GitHubGet `
        -Uri "https://api.github.com/repos/$RepositorySlug" `
        -Headers $headers
    Assert-True `
        -Condition ([bool]$repository.private) `
        -Message 'Archive repository is not private'

    $remoteHead = Get-RemoteBranchHead `
        -RepositorySlug $RepositorySlug `
        -BranchName 'main' `
        -Headers $headers
    $localHead = (Invoke-ArchiveGit -Arguments @('rev-parse', 'HEAD')).Text.Trim()
    $trackingHead = (Invoke-ArchiveGit -Arguments @(
        'rev-parse', 'refs/remotes/origin/main')).Text.Trim()
    Assert-True `
        -Condition ($localHead -ceq $remoteHead -and $trackingHead -ceq $remoteHead) `
        -Message 'Local HEAD, origin/main, and remote main do not match'

    $release = Get-Release `
        -RepositorySlug $RepositorySlug `
        -Tag $ReleaseTag `
        -Headers $headers
    $assets = @(Get-AllReleaseAssets `
        -RepositorySlug $RepositorySlug `
        -ReleaseId ([string]$release.id) `
        -Headers $headers)
    $duplicateAssets = @(
        $assets | Group-Object name -CaseSensitive | Where-Object Count -ne 1)
    Assert-True `
        -Condition ($duplicateAssets.Count -eq 0) `
        -Message 'Remote release contains duplicate asset names'

    $checkpointBlob = [byte[]](Get-BlobBytesLocal `
        -Commit $remoteHead -Path $CheckpointManifestPath)
    $legacyBlob = [byte[]](Get-BlobBytesLocal `
        -Commit $remoteHead -Path $LegacyManifestPath)
    $checkpointRemote = [byte[]](Get-RemoteContentBytesLocal `
        -Path $CheckpointManifestPath -Commit $remoteHead -Headers $headers)
    $legacyRemote = [byte[]](Get-RemoteContentBytesLocal `
        -Path $LegacyManifestPath -Commit $remoteHead -Headers $headers)
    Assert-ByteArraysEqual `
        -Expected $checkpointBlob -Actual $checkpointRemote `
        -Label 'Checkpoint manifest blob/API response'
    Assert-ByteArraysEqual `
        -Expected $legacyBlob -Actual $legacyRemote `
        -Label 'Legacy manifest blob/API response'

    $checkpointRows = @(
        $Utf8Strict.GetString($checkpointRemote) | ConvertFrom-Csv)
    $legacyRows = @($Utf8Strict.GetString($legacyRemote) | ConvertFrom-Csv)
    $approvedCheckpointRows = @(
        $checkpointRows | Where-Object {
            $CheckpointTimes -ccontains [string]$_.time_directory
        })
    Assert-ExactSetLocal `
        -Actual @($approvedCheckpointRows.time_directory) `
        -Expected $CheckpointTimes `
        -Label 'Approved checkpoint manifest times'

    $expectedLegacyLocators = @(
        $LegacyCases | ForEach-Object { "openfoam_cases/$_" }
        $LegacySnapshots | ForEach-Object { "openfoam_benchmark_snapshots/$_" }
    )
    Assert-ExactSetLocal `
        -Actual @($legacyRows.source_locator) `
        -Expected $expectedLegacyLocators `
        -Label 'Approved legacy manifest locators'

    foreach ($row in @($approvedCheckpointRows + $legacyRows)) {
        Assert-RemoteRowLocal -Row $row -Assets $assets
    }

    $caseParent = Join-Path $resolvedSource 'openfoam_cases'
    $snapshotParent = Join-Path $resolvedSource 'openfoam_benchmark_snapshots'
    $activeCase = Join-Path $caseParent $ActiveCaseName
    Assert-True `
        -Condition (Test-Path -LiteralPath $activeCase -PathType Container) `
        -Message 'Active OpenFOAM case is missing'

    foreach ($time in $CheckpointTimes) {
        foreach ($prefix in @('', 'processor0', 'processor1', 'processor2', 'processor3')) {
            $parent = if ($prefix) { Join-Path $activeCase $prefix } else { $activeCase }
            Assert-True `
                -Condition (-not (Test-Path -LiteralPath (Join-Path $parent $time))) `
                -Message "Approved checkpoint source still exists: $prefix/$time"
        }
    }
    foreach ($name in $LegacyCases) {
        Assert-True `
            -Condition (-not (Test-Path -LiteralPath (Join-Path $caseParent $name))) `
            -Message "Approved old case still exists: $name"
    }
    foreach ($name in $LegacySnapshots) {
        Assert-True `
            -Condition (-not (Test-Path -LiteralPath (Join-Path $snapshotParent $name))) `
            -Message "Approved benchmark snapshot still exists: $name"
    }

    foreach ($prefix in @('', 'processor0', 'processor1', 'processor2', 'processor3')) {
        $parent = if ($prefix) { Join-Path $activeCase $prefix } else { $activeCase }
        Assert-ExactSetLocal `
            -Actual @(Get-NumericDirectoryNamesLocal -Path $parent) `
            -Expected $PreservedTimes `
            -Label "Preserved active times for '$prefix'"
    }
    foreach ($tree in @('constant', 'system', 'postProcessing', 'provenance')) {
        Assert-True `
            -Condition (Test-Path -LiteralPath (Join-Path $activeCase $tree) -PathType Container) `
            -Message "Protected active tree is missing: $tree"
    }
    Assert-ExactSetLocal `
        -Actual @((Get-ChildItem -LiteralPath $snapshotParent -Directory -Force).Name) `
        -Expected $PreservedSnapshots `
        -Label 'Preserved benchmark snapshot roots'
    Assert-ExactSetLocal `
        -Actual @((Get-ChildItem -LiteralPath $caseParent -Directory -Force).Name) `
        -Expected @($ActiveCaseName) `
        -Label 'Remaining OpenFOAM case roots'

    $receiptRoot = Join-Path $activeCase '.checkpoint_archive_quarantine'
    $receipts = @(Get-ChildItem `
        -LiteralPath $receiptRoot -File -Force -Filter '*.archive_receipt.json')
    Assert-True `
        -Condition ($receipts.Count -eq $CheckpointTimes.Count) `
        -Message 'Checkpoint receipt count changed'
    $receiptTimes = [Collections.Generic.List[string]]::new()
    foreach ($receiptFile in $receipts) {
        $receipt = Get-Content -LiteralPath $receiptFile.FullName -Raw | ConvertFrom-Json
        $receiptTimes.Add([string]$receipt.time_directory)
        Assert-True `
            -Condition (
                [string]$receipt.phase -ceq 'completed' -and
                [int]$receipt.moved_count -eq 5 -and
                [int]$receipt.purged_count -eq 5 -and
                [bool]$receipt.staged_asset_removed
            ) `
            -Message "Checkpoint deletion receipt is incomplete: $($receiptFile.Name)"
        $manifestRow = @($approvedCheckpointRows | Where-Object {
            [string]$_.time_directory -ceq [string]$receipt.time_directory
        })
        Assert-True `
            -Condition (
                $manifestRow.Count -eq 1 -and
                [string]$manifestRow[0].asset -ceq [string]$receipt.asset -and
                [string]$manifestRow[0].sha256 -ceq [string]$receipt.sha256
            ) `
            -Message "Checkpoint receipt disagrees with manifest: $($receiptFile.Name)"
    }
    Assert-ExactSetLocal `
        -Actual $receiptTimes.ToArray() `
        -Expected $CheckpointTimes `
        -Label 'Checkpoint receipt times'

    $validationRoot = Join-Path (Split-Path $PSScriptRoot -Parent) 'validation'
    Assert-True `
        -Condition (Test-Path -LiteralPath $validationRoot -PathType Container) `
        -Message 'Validation evidence root is missing'
    $validationFiles = @(Get-ChildItem `
        -LiteralPath $validationRoot -File -Force -Recurse)
    $validationBytes = [int64](($validationFiles | Measure-Object Length -Sum).Sum)
    $activeLogs = @(Get-ChildItem `
        -LiteralPath $activeCase -File -Force -Filter '*.log')
    Assert-True `
        -Condition ($activeLogs.Count -gt 0) `
        -Message 'Active case logs are missing'

    $verifiedRows = @($approvedCheckpointRows + $legacyRows)
    $result = [pscustomobject][ordered]@{
        status = 'VERIFIED_REMOTE_AND_LOCAL_BOUNDARY'
        verified_at_utc = [DateTime]::UtcNow.ToString('o')
        repository = $RepositorySlug
        repository_private = [bool]$repository.private
        release = $ReleaseTag
        remote_main = $remoteHead
        release_asset_count = $assets.Count
        approved_asset_count = $verifiedRows.Count
        approved_asset_bytes = [int64](
            ($verifiedRows | Measure-Object asset_bytes -Sum).Sum)
        approved_source_bytes = [int64](
            ($verifiedRows | Measure-Object source_bytes -Sum).Sum)
        checkpoint_groups = $approvedCheckpointRows.Count
        legacy_cases = $LegacyCases.Count
        benchmark_snapshots = $LegacySnapshots.Count
        remote_asset_errors = 0
        checkpoint_manifest = [pscustomobject][ordered]@{
            bytes = $checkpointRemote.Length
            sha256 = Get-Sha256BytesLocal -Bytes $checkpointRemote
            selected_rows = $approvedCheckpointRows.Count
            blob_api_bytes_identical = $true
        }
        legacy_manifest = [pscustomobject][ordered]@{
            bytes = $legacyRemote.Length
            sha256 = Get-Sha256BytesLocal -Bytes $legacyRemote
            rows = $legacyRows.Count
            blob_api_bytes_identical = $true
        }
        absent_approved_local_paths =
            ($CheckpointTimes.Count * 5) + $LegacyCases.Count + $LegacySnapshots.Count
        checkpoint_receipts = $receipts.Count
        preserved_times_per_case = $PreservedTimes
        preserved_benchmark_roots = $PreservedSnapshots
        active_log_files = $activeLogs.Count
        validation_files = $validationFiles.Count
        validation_bytes = $validationBytes
    }
    $json = $result | ConvertTo-Json -Depth 6
    if (-not [string]::IsNullOrWhiteSpace($RequestedOutputPath)) {
        $fullOutput = [IO.Path]::GetFullPath($RequestedOutputPath)
        $projectRoot = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
        $prefix = $projectRoot.TrimEnd(
            [IO.Path]::DirectorySeparatorChar,
            [IO.Path]::AltDirectorySeparatorChar) +
            [IO.Path]::DirectorySeparatorChar
        Assert-True `
            -Condition ($fullOutput.StartsWith($prefix, $PathComparison)) `
            -Message 'OutputPath must remain under the project root'
        [IO.Directory]::CreateDirectory((Split-Path $fullOutput -Parent)) | Out-Null
        [IO.File]::WriteAllText($fullOutput, $json + "`n", $Utf8NoBom)
    }
    $json
} finally {
    $script:GitHubToken = $null
}
