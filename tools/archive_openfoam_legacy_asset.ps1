#requires -Version 7.0

<#
.SYNOPSIS
Publishes and optionally removes one approved legacy OpenFOAM case or benchmark.

.DESCRIPTION
The asset key is resolved through a fixed 2026-08-26 allowlist. Plan and
Validate are read-only. Archive stages one deterministic .tar.zst, verifies its
members and payload hashes, uploads it without ever replacing a same-name
GitHub Release asset, commits one verified manifest row, and reads the exact
manifest bytes back from remote main.

Source removal is separately gated by -DeleteSources. Immediately before the
one exact allowlisted root is quarantined, the script re-verifies the remote
byte count, GitHub SHA-256 digest, uploaded state, remote manifest bytes, staged
archive, parent allowlists, and source fingerprint. A durable journal and the
verified staged archive are used to roll back a failed quarantine/purge.

The script never deletes or replaces a GitHub asset and never force-pushes.
Recover is a separate, terminal local-recovery mode: it re-verifies the exact
remote asset, manifest bytes, and recorded remote commit using GET requests,
then restores and fingerprints the source without uploading, committing, or
continuing into deletion.
#>

[CmdletBinding()]
param(
    [ValidateSet('Plan', 'Validate', 'Archive', 'Recover')]
    [string] $Mode = 'Plan',

    [string] $AssetKey,

    [string] $CaseParent =
        'C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_cases',

    [string] $SnapshotParent =
        'C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_benchmark_snapshots',

    [string] $ArchiveRepoPath,

    [string] $ReleaseTag = 'openfoam-checkpoints-2026-08-25',

    [string] $Branch = 'main',

    [switch] $DeleteSources,

    [Parameter(DontShow)]
    [switch] $LoadFunctionsOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:ArchiveSetId = 'openfoam-archive-2026-08-26'
$script:ExpectedRepositorySlug = 'hughes6/thermal-sim-openfoam-archive'
$script:ExpectedReleaseTag = 'openfoam-checkpoints-2026-08-25'
$script:ExpectedCaseParent =
    'C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_cases'
$script:ExpectedSnapshotParent =
    'C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_benchmark_snapshots'
$script:ActiveCaseName = 'new_model_updated_openfoam_export_test'
$script:ManifestRelativePath = 'manifests/openfoam-legacy-assets-2026-08-26.csv'
$script:StagingRelativePath = '.archive-staging/legacy-assets'
$script:DeletionRelativePath = '.archive-staging/legacy-delete'
$script:ManifestHeader = 'archive_set,category,source_locator,asset,source_files,source_bytes,source_entries,source_directories,source_links,source_listing_sha256,source_fingerprint_sha256,asset_bytes,sha256,github_digest,tar_members,tar_listing_sha256,uploaded_at_utc,status'
$script:Utf8NoBom = [Text.UTF8Encoding]::new($false)
$script:InvariantCulture = [Globalization.CultureInfo]::InvariantCulture
$script:NumberStyle = [Globalization.NumberStyles]::Float
$script:PathComparison = if ($IsWindows) {
    [StringComparison]::OrdinalIgnoreCase
} else {
    [StringComparison]::Ordinal
}
$script:PathComparer = if ($IsWindows) {
    [StringComparer]::OrdinalIgnoreCase
} else {
    [StringComparer]::Ordinal
}
$script:GitHubToken = $null
$script:ArchiveRoot = $null
$script:ArchiveRepoLock = $null
$script:StagedAssetGuard = $null
$script:GitHubGetOverride = $null
$script:ReparseQueryOverride = $null
$script:ReparseQueryMaxAttempts = 9
$script:ReparseQueryDelayMilliseconds = 250

$script:ApprovedCases = @(
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

$script:ApprovedSnapshots = @(
    'decomposition_auto_case',
    'dt_baseline_0p0005_case',
    'dt_variant_0p00075_case',
    'pimple_outer2_auto_case'
)

$script:PreservedSnapshots = @(
    'thermal_dt_screen_20_case',
    'thermal_dt_screen_25_case',
    'thermal_dt_screen_30_case',
    'thermal_dt_screen_40_case',
    'thermal_dt_screen_60_case'
)

$script:ExpectedUniformLinkHash =
    '50c51c5834e5285413589f9f586e6b0701ad6453573d381f21fefa8a2fc395d4'
$script:ExpectedFluidUniformLinkHash =
    '93562f1e8fcb41f3d2156619bce683d61e8e08ea890be5339d4e22f323bc3878'
$script:EmptySha256 =
    'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'

function Resolve-ExistingDirectory {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Label
    )

    $resolved = @(Resolve-Path -LiteralPath $LiteralPath -ErrorAction Stop)
    if ($resolved.Count -ne 1) {
        throw "$Label must resolve to exactly one directory: $LiteralPath"
    }
    $full = [IO.Path]::GetFullPath($resolved[0].Path).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
    $root = [IO.Path]::GetPathRoot($full).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
    if ($full.Equals($root, $script:PathComparison)) {
        throw "$Label may not be a filesystem root: $full"
    }
    $item = Get-Item -LiteralPath $full -Force
    if (-not $item.PSIsContainer) {
        throw "$Label is not a directory: $full"
    }
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label may not itself be a reparse point: $full"
    }
    return $full
}

function Test-PathUnderRoot {
    param(
        [Parameter(Mandatory)] [string] $Candidate,
        [Parameter(Mandatory)] [string] $Root
    )

    $prefix = $Root.TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    return $Candidate.StartsWith($prefix, $script:PathComparison)
}

function Assert-PathUnderRoot {
    param(
        [Parameter(Mandatory)] [string] $Candidate,
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $Label
    )

    if (-not (Test-PathUnderRoot -Candidate $Candidate -Root $Root)) {
        throw "$Label escapes '$Root': $Candidate"
    }
}

function Assert-SeparateTrees {
    param(
        [Parameter(Mandatory)] [string] $First,
        [Parameter(Mandatory)] [string] $Second,
        [Parameter(Mandatory)] [string] $Label
    )

    if (
        $First.Equals($Second, $script:PathComparison) -or
        (Test-PathUnderRoot -Candidate $First -Root $Second) -or
        (Test-PathUnderRoot -Candidate $Second -Root $First)
    ) {
        throw "$Label must be separate, non-nested trees: '$First' and '$Second'"
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
        $Value -in @('.', '..') -or
        $Value -notmatch $Pattern
    ) {
        throw "$Label is not one safe leaf name: '$Value'"
    }
}

function Assert-SafeRelativePath {
    param(
        [Parameter(Mandatory)] [string] $Value,
        [Parameter(Mandatory)] [string] $Label
    )

    if (
        [string]::IsNullOrWhiteSpace($Value) -or
        [IO.Path]::IsPathRooted($Value) -or
        $Value.Contains("`r") -or
        $Value.Contains("`n") -or
        $Value -match '^[A-Za-z]:' -or
        $Value -split '[/\\]' -contains '..'
    ) {
        throw "$Label is not a safe relative path: '$Value'"
    }
}

function Assert-ExactSet {
    param(
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $Actual,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $Expected,
        [Parameter(Mandatory)] [string] $Label
    )

    $actualSet = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $expectedSet = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($value in $Actual) {
        if (-not $actualSet.Add([string]$value)) {
            throw "$Label contains duplicate '$value'"
        }
    }
    foreach ($value in $Expected) {
        if (-not $expectedSet.Add([string]$value)) {
            throw "Internal $Label allowlist contains duplicate '$value'"
        }
    }
    $missing = @($expectedSet | Where-Object { -not $actualSet.Contains($_) })
    $unexpected = @($actualSet | Where-Object { -not $expectedSet.Contains($_) })
    if ($missing.Count -ne 0 -or $unexpected.Count -ne 0) {
        throw "$Label mismatch; missing=[$($missing -join ', ')]; unexpected=[$($unexpected -join ', ')]"
    }
}

function Get-Sha256Bytes {
    param([Parameter(Mandatory)] [byte[]] $Bytes)

    return [Convert]::ToHexString(
        [Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}

function Get-Sha256Text {
    param([Parameter(Mandatory)] [string] $Text)

    return Get-Sha256Bytes -Bytes $script:Utf8NoBom.GetBytes($Text)
}

function Get-Sha256File {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $stream = [IO.File]::Open(
        $LiteralPath,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    try {
        $algorithm = [Security.Cryptography.SHA256]::Create()
        try {
            return [Convert]::ToHexString(
                $algorithm.ComputeHash($stream)).ToLowerInvariant()
        } finally {
            $algorithm.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

function ConvertTo-AssetSlug {
    param([Parameter(Mandatory)] [string] $Value)

    $slug = ($Value.ToLowerInvariant() -replace '[^a-z0-9]+', '-').Trim('-')
    if ([string]::IsNullOrWhiteSpace($slug)) {
        throw "Could not derive asset slug from '$Value'"
    }
    return $slug
}

function Get-AllowedSnapshotReparseHashes {
    param([Parameter(Mandatory)] [string] $SnapshotName)

    $allowed = @{}
    if ($SnapshotName -in @('decomposition_auto_case', 'pimple_outer2_auto_case')) {
        foreach ($rank in 0..3) {
            $prefix = "$SnapshotName/processor$rank/1.6000000000000001"
            $allowed["$prefix/uniform"] = $script:ExpectedUniformLinkHash
            $allowed["$prefix/fluid/uniform"] = $script:ExpectedFluidUniformLinkHash
        }
    }
    return $allowed
}

function Get-ApprovedJob {
    param(
        [Parameter(Mandatory)] [string] $Key,
        [Parameter(Mandatory)] [string] $ResolvedCaseParent,
        [Parameter(Mandatory)] [string] $ResolvedSnapshotParent
    )

    Assert-SafeLeafName -Value $Key -Label 'AssetKey' -Pattern '^[A-Za-z0-9][A-Za-z0-9._-]*$'
    if ($script:ApprovedCases -ccontains $Key) {
        $slug = ConvertTo-AssetSlug -Value $Key
        return [pscustomobject]@{
            Key = $Key
            Category = 'sibling_time0_case'
            SourceLocator = "openfoam_cases/$Key"
            AssetName = "openfoam-case-$slug-time0-20260826.tar.zst"
            ParentRoot = $ResolvedCaseParent
            RelativeRoot = $Key
            AllowedReparseHashes = @{}
        }
    }
    if ($script:ApprovedSnapshots -ccontains $Key) {
        $slug = ConvertTo-AssetSlug -Value $Key
        return [pscustomobject]@{
            Key = $Key
            Category = 'benchmark_snapshot'
            SourceLocator = "openfoam_benchmark_snapshots/$Key"
            AssetName = "openfoam-benchmark-$slug-20260826.tar.zst"
            ParentRoot = $ResolvedSnapshotParent
            RelativeRoot = $Key
            AllowedReparseHashes = Get-AllowedSnapshotReparseHashes -SnapshotName $Key
        }
    }
    throw "AssetKey is not one of the 17 approved legacy roots: '$Key'"
}

function Assert-ApprovedParentLayout {
    param(
        [Parameter(Mandatory)] [string] $ResolvedCaseParent,
        [Parameter(Mandatory)] [string] $ResolvedSnapshotParent
    )

    foreach ($parent in @($ResolvedCaseParent, $ResolvedSnapshotParent)) {
        $directFiles = @(Get-ChildItem -LiteralPath $parent -File -Force)
        if ($directFiles.Count -ne 0) {
            throw "Unexpected file directly under approved source parent: $($directFiles[0].FullName)"
        }
    }

    $caseNames = @(
        Get-ChildItem -LiteralPath $ResolvedCaseParent -Directory -Force |
            ForEach-Object Name)
    $allowedCases = @($script:ApprovedCases + $script:ActiveCaseName)
    $unexpectedCases = @($caseNames | Where-Object { $allowedCases -cnotcontains $_ })
    if ($unexpectedCases.Count -ne 0) {
        throw "Unexpected OpenFOAM case root(s): $($unexpectedCases -join ', ')"
    }
    if ($caseNames -cnotcontains $script:ActiveCaseName) {
        throw "Preserved active case is missing: $($script:ActiveCaseName)"
    }

    $snapshotNames = @(
        Get-ChildItem -LiteralPath $ResolvedSnapshotParent -Directory -Force |
            ForEach-Object Name)
    $allowedSnapshots = @($script:ApprovedSnapshots + $script:PreservedSnapshots)
    $unexpectedSnapshots = @(
        $snapshotNames | Where-Object { $allowedSnapshots -cnotcontains $_ })
    if ($unexpectedSnapshots.Count -ne 0) {
        throw "Unexpected benchmark root(s): $($unexpectedSnapshots -join ', ')"
    }
    foreach ($preserved in $script:PreservedSnapshots) {
        if ($snapshotNames -cnotcontains $preserved) {
            throw "Preserved validation snapshot is missing: $preserved"
        }
    }
}

function Get-NumericDirectoryNames {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $names = @()
    foreach ($directory in Get-ChildItem -LiteralPath $LiteralPath -Directory -Force) {
        $number = 0.0
        if ([double]::TryParse(
            $directory.Name,
            $script:NumberStyle,
            $script:InvariantCulture,
            [ref]$number
        )) {
            if ([double]::IsNaN($number) -or [double]::IsInfinity($number)) {
                throw "Non-finite numeric directory under '$LiteralPath': $($directory.Name)"
            }
            $names += $directory.Name
        }
    }
    return $names
}

function Assert-CaseTargetLayout {
    param([Parameter(Mandatory)] $Job)

    if ($Job.Category -cne 'sibling_time0_case') {
        return
    }
    $target = [IO.Path]::GetFullPath((Join-Path $Job.ParentRoot $Job.RelativeRoot))
    Assert-PathUnderRoot -Candidate $target -Root $Job.ParentRoot -Label 'Approved case target'
    Assert-ExactSet `
        -Actual @(Get-NumericDirectoryNames -LiteralPath $target) `
        -Expected @('0') `
        -Label "Numeric times for '$($Job.Key)'"
    $processors = @(
        Get-ChildItem -LiteralPath $target -Directory -Force |
            Where-Object Name -Match '^processor[0-9]+$')
    if ($processors.Count -ne 0) {
        throw "Approved old case contains processor output: $target"
    }
    if (Test-Path -LiteralPath (Join-Path $target 'postProcessing')) {
        throw "Approved old case contains postProcessing: $target"
    }
}

function New-FsutilQueryPathLease {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    if (-not $IsWindows) {
        throw 'Approved benchmark reparse points require Windows fsutil validation'
    }
    $full = [IO.Path]::GetFullPath($LiteralPath)
    if ($full.Length -lt 260) {
        return [pscustomobject]@{
            QueryPath = $full
            SubstDrive = $null
            SubstPath = $null
        }
    }

    # fsutil still applies the legacy MAX_PATH limit when opening an LX reparse
    # point, even when its argument uses the \\?\ extended-length prefix. Map a
    # verified ordinary ancestor to an unused temporary drive so the path that
    # fsutil receives remains comfortably below that limit. subst also receives
    # a sub-MAX_PATH target; no source content is copied or followed.
    $anchor = [IO.Path]::GetDirectoryName($full)
    $relative = $null
    while (-not [string]::IsNullOrEmpty($anchor)) {
        if (
            $anchor.Length -le 240 -and
            [IO.Directory]::Exists($anchor) -and
            (([IO.File]::GetAttributes($anchor) -band [IO.FileAttributes]::ReparsePoint) -eq 0)
        ) {
            $candidateRelative = [IO.Path]::GetRelativePath($anchor, $full)
            if (
                -not [IO.Path]::IsPathRooted($candidateRelative) -and
                $candidateRelative -cne '..' -and
                -not $candidateRelative.StartsWith(
                    '..' + [IO.Path]::DirectorySeparatorChar,
                    [StringComparison]::Ordinal) -and
                (3 + $candidateRelative.Length) -le 240
            ) {
                $relative = $candidateRelative
                break
            }
        }
        $parent = [IO.Path]::GetDirectoryName($anchor)
        if ([string]::IsNullOrEmpty($parent) -or $parent -ceq $anchor) {
            $anchor = $null
        } else {
            $anchor = $parent
        }
    }
    if ([string]::IsNullOrEmpty($anchor) -or [string]::IsNullOrEmpty($relative)) {
        throw "Could not derive a sub-MAX_PATH fsutil alias for '$full'"
    }

    $subst = @(Get-Command subst.exe -CommandType Application -ErrorAction Stop)[0]
    $failures = [Collections.Generic.List[string]]::new()
    foreach ($codePoint in ([int][char]'Z')..([int][char]'D')) {
        $drive = ([char]$codePoint).ToString() + ':'
        if (
            [IO.Directory]::Exists($drive + '\') -or
            [IO.File]::Exists($drive + '\')
        ) {
            continue
        }
        $output = @(& $subst.Source $drive $anchor 2>&1)
        $exitCode = [int]$LASTEXITCODE
        if ($exitCode -ne 0) {
            $failures.Add("$drive exit $exitCode`: $($output -join ' ')")
            continue
        }
        $queryPath = $drive + '\' + $relative
        if ($queryPath.Length -gt 240) {
            $cleanupOutput = @(& $subst.Source $drive /d 2>&1)
            $cleanupExit = [int]$LASTEXITCODE
            if ($cleanupExit -ne 0) {
                throw "Temporary fsutil alias '$drive' exceeded its bound and could not be removed: $($cleanupOutput -join ' ')"
            }
            throw "Temporary fsutil alias unexpectedly exceeds its path bound: $queryPath"
        }
        return [pscustomobject]@{
            QueryPath = $queryPath
            SubstDrive = $drive
            SubstPath = $subst.Source
        }
    }
    $details = if ($failures.Count -eq 0) {
        'no unused drive letter was available'
    } else {
        $failures -join '; '
    }
    throw "Could not create a temporary fsutil path alias for '$full': $details"
}

function Close-FsutilQueryPathLease {
    param([Parameter(Mandatory)] $Lease)

    if ($null -eq $Lease.SubstDrive) {
        return
    }
    $output = @(& $Lease.SubstPath $Lease.SubstDrive /d 2>&1)
    $exitCode = [int]$LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "Could not remove temporary fsutil alias '$($Lease.SubstDrive)': $($output -join ' ')"
    }
}

function Invoke-ReparsePointQuery {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [int] $Attempt
    )

    if ($null -ne $script:ReparseQueryOverride) {
        $result = & $script:ReparseQueryOverride $LiteralPath $Attempt
        if (
            $null -eq $result -or
            $null -eq $result.PSObject.Properties['ExitCode'] -or
            $null -eq $result.PSObject.Properties['Output']
        ) {
            throw 'Reparse-query test override returned an invalid result'
        }
        return [pscustomobject]@{
            ExitCode = [int]$result.ExitCode
            Output = @($result.Output | ForEach-Object { [string]$_ })
        }
    }
    $fsutil = @(Get-Command fsutil -CommandType Application -ErrorAction Stop)[0]
    $lease = New-FsutilQueryPathLease -LiteralPath $LiteralPath
    try {
        $output = @(& $fsutil.Source reparsepoint query $lease.QueryPath 2>&1)
        $exitCode = [int]$LASTEXITCODE
    } finally {
        Close-FsutilQueryPathLease -Lease $lease
    }
    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = @($output | ForEach-Object { [string]$_ })
    }
}

function Get-ReparsePayloadHash {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $lastFailure = $null
    for ($attempt = 1; $attempt -le $script:ReparseQueryMaxAttempts; ++$attempt) {
        $query = Invoke-ReparsePointQuery -LiteralPath $LiteralPath -Attempt $attempt
        if ($query.ExitCode -eq 0) {
            $normalized = (($query.Output -join "`n").Replace("`r", '').Trim() + "`n")
            if ($normalized -notmatch '(?i)0xa000001d') {
                throw "Unexpected reparse tag for '$LiteralPath'"
            }
            return Get-Sha256Text -Text $normalized
        }
        $lastFailure = $query.Output -join [Environment]::NewLine
        if ($attempt -lt $script:ReparseQueryMaxAttempts) {
            [Threading.Thread]::Sleep($script:ReparseQueryDelayMilliseconds)
        }
    }
    throw "fsutil could not read reparse data for '$LiteralPath' after $($script:ReparseQueryMaxAttempts) attempts: $lastFailure"
}

function Get-ArchiveEntries {
    param(
        [Parameter(Mandatory)] $Job,
        [Parameter(Mandatory)] [bool] $DeepHash
    )

    $rootPath = [IO.Path]::GetFullPath((Join-Path $Job.ParentRoot $Job.RelativeRoot))
    Assert-PathUnderRoot -Candidate $rootPath -Root $Job.ParentRoot -Label 'Archive source root'
    if (-not (Test-Path -LiteralPath $rootPath -PathType Container)) {
        throw "Approved archive source is missing: $rootPath"
    }
    $resolvedRoot = Resolve-ExistingDirectory -LiteralPath $rootPath -Label 'Archive source root'
    if (-not $resolvedRoot.Equals($rootPath, $script:PathComparison)) {
        throw "Archive source resolved to a different path: $resolvedRoot"
    }

    $entries = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::Ordinal)
    $stack = [Collections.Generic.Stack[string]]::new()
    $stack.Push($rootPath)
    while ($stack.Count -gt 0) {
        $directoryPath = $stack.Pop()
        $relativeDirectory = [IO.Path]::GetRelativePath(
            $Job.ParentRoot, $directoryPath).Replace('\', '/')
        Assert-SafeRelativePath -Value $relativeDirectory -Label 'Directory entry'
        if ($entries.ContainsKey($relativeDirectory)) {
            throw "Duplicate archive entry: $relativeDirectory"
        }
        $entries.Add($relativeDirectory, [pscustomobject]@{
            ArchivePath = $relativeDirectory
            FullPath = $directoryPath
            Kind = 'directory'
            Length = [int64]0
            Sha256 = $null
            ReparseSha256 = $null
        })

        foreach ($child in [IO.DirectoryInfo]::new($directoryPath).EnumerateFileSystemInfos()) {
            $childPath = [IO.Path]::GetFullPath($child.FullName)
            Assert-PathUnderRoot -Candidate $childPath -Root $Job.ParentRoot -Label 'Archive entry'
            $relative = [IO.Path]::GetRelativePath(
                $Job.ParentRoot, $childPath).Replace('\', '/')
            Assert-SafeRelativePath -Value $relative -Label 'Archive entry'
            $isReparse =
                ($child.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
            $isDirectory =
                ($child.Attributes -band [IO.FileAttributes]::Directory) -ne 0
            if ($isReparse) {
                if (-not $Job.AllowedReparseHashes.ContainsKey($relative)) {
                    throw "Unexpected reparse point: $childPath"
                }
                $payloadHash = Get-ReparsePayloadHash -LiteralPath $childPath
                if ($payloadHash -cne [string]$Job.AllowedReparseHashes[$relative]) {
                    throw "Approved reparse payload changed for '$relative'"
                }
                $entries.Add($relative, [pscustomobject]@{
                    ArchivePath = $relative
                    FullPath = $childPath
                    Kind = 'link'
                    Length = [int64]0
                    Sha256 = $null
                    ReparseSha256 = $payloadHash
                })
                continue
            }
            if ($isDirectory) {
                $stack.Push($childPath)
                continue
            }
            if (-not ($child -is [IO.FileInfo])) {
                throw "Unsupported filesystem entry: $childPath"
            }
            $lengthBefore = [int64]$child.Length
            $sha = if ($DeepHash) { Get-Sha256File -LiteralPath $childPath } else { $null }
            $lengthAfter = [int64](Get-Item -LiteralPath $childPath -Force).Length
            if ($lengthBefore -ne $lengthAfter) {
                throw "Source length changed while inventorying: $childPath"
            }
            $entries.Add($relative, [pscustomobject]@{
                ArchivePath = $relative
                FullPath = $childPath
                Kind = 'file'
                Length = $lengthBefore
                Sha256 = $sha
                ReparseSha256 = $null
            })
        }
    }

    $actualLinks = @(
        $entries.Values | Where-Object Kind -eq 'link' | ForEach-Object ArchivePath)
    Assert-ExactSet `
        -Actual $actualLinks `
        -Expected @($Job.AllowedReparseHashes.Keys) `
        -Label 'Approved reparse-point set'
    $keys = [string[]]@($entries.Keys)
    [Array]::Sort($keys, [StringComparer]::Ordinal)
    return @($keys | ForEach-Object { $entries[$_] })
}

function Get-SourceInventory {
    param(
        [Parameter(Mandatory)] $Job,
        [bool] $DeepHash = $true
    )

    $entries = @(Get-ArchiveEntries -Job $Job -DeepHash $DeepHash)
    $records = [Text.StringBuilder]::new()
    foreach ($entry in $entries) {
        switch ($entry.Kind) {
            'directory' { [void]$records.Append("D`t$($entry.ArchivePath)`n") }
            'link' {
                [void]$records.Append(
                    "L`t$($entry.ArchivePath)`t$($entry.ReparseSha256)`n")
            }
            'file' {
                if ($DeepHash -and [string]$entry.Sha256 -notmatch '^[0-9a-f]{64}$') {
                    throw "Deep inventory lacks file hash: $($entry.ArchivePath)"
                }
                [void]$records.Append(
                    "F`t$($entry.ArchivePath)`t$($entry.Length)`t$($entry.Sha256)`n")
            }
        }
    }
    $files = @($entries | Where-Object Kind -eq 'file')
    $directories = @($entries | Where-Object Kind -eq 'directory')
    $links = @($entries | Where-Object Kind -eq 'link')
    return [pscustomobject]@{
        Entries = $entries
        EntryCount = $entries.Count
        SourceFileCount = $files.Count
        SourceBytes = [int64](($files | Measure-Object Length -Sum).Sum)
        DirectoryCount = $directories.Count
        LinkCount = $links.Count
        ListingSha256 = Get-Sha256Text -Text (($entries.ArchivePath -join "`n") + "`n")
        SourceFingerprintSha256 = if ($DeepHash) {
            Get-Sha256Text -Text $records.ToString()
        } else {
            $null
        }
    }
}

function Assert-InventoryEqual {
    param(
        [Parameter(Mandatory)] $Expected,
        [Parameter(Mandatory)] $Actual,
        [Parameter(Mandatory)] [string] $Label
    )

    foreach ($property in @(
        'EntryCount', 'SourceFileCount', 'SourceBytes', 'DirectoryCount',
        'LinkCount', 'ListingSha256', 'SourceFingerprintSha256'
    )) {
        if ([string]$Expected.$property -cne [string]$Actual.$property) {
            throw "$Label changed ($property)"
        }
    }
}

function Test-InventoryMatchesDurableMetadata {
    param(
        [Parameter(Mandatory)] $Inventory,
        [Parameter(Mandatory)] $Metadata
    )

    if (
        [int64]$Inventory.SourceFileCount -ne [int64]$Metadata.source_files -or
        [int64]$Inventory.SourceBytes -ne [int64]$Metadata.source_bytes -or
        [int64]$Inventory.EntryCount -ne [int64]$Metadata.source_entries -or
        [int64]$Inventory.DirectoryCount -ne [int64]$Metadata.source_directories -or
        [int64]$Inventory.LinkCount -ne [int64]$Metadata.source_links -or
        [string]$Inventory.ListingSha256 -cne [string]$Metadata.source_listing_sha256 -or
        [string]$Inventory.SourceFingerprintSha256 -cne
            [string]$Metadata.source_fingerprint_sha256
    ) {
        return $false
    }
    $expected = @(ConvertFrom-DurableEntryRecords -Records @($Metadata.entries))
    $actual = @($Inventory.Entries)
    if ($expected.Count -ne $actual.Count) {
        return $false
    }
    for ($index = 0; $index -lt $expected.Count; ++$index) {
        foreach ($property in @(
            'ArchivePath', 'Kind', 'Length', 'Sha256', 'ReparseSha256'
        )) {
            if ([string]$expected[$index].$property -cne [string]$actual[$index].$property) {
                return $false
            }
        }
    }
    return $true
}

function ConvertTo-DurableEntryRecords {
    param([Parameter(Mandatory)] $Inventory)

    return @($Inventory.Entries | ForEach-Object {
        [pscustomobject][ordered]@{
            path = $_.ArchivePath
            kind = $_.Kind
            bytes = [int64]$_.Length
            sha256 = $_.Sha256
            reparse_sha256 = $_.ReparseSha256
        }
    })
}

function ConvertFrom-DurableEntryRecords {
    param([Parameter(Mandatory)] [object[]] $Records)

    return @($Records | ForEach-Object {
        [pscustomobject]@{
            ArchivePath = [string]$_.path
            Kind = [string]$_.kind
            Length = [int64]$_.bytes
            Sha256 = if ($null -eq $_.sha256) { $null } else { [string]$_.sha256 }
            ReparseSha256 = if ($null -eq $_.reparse_sha256) {
                $null
            } else {
                [string]$_.reparse_sha256
            }
        }
    })
}

function Write-DurableUtf8File {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Content,
        [Parameter(Mandatory)] [string] $ApprovedRoot,
        [switch] $Replace
    )

    $full = [IO.Path]::GetFullPath($LiteralPath)
    Assert-PathUnderRoot -Candidate $full -Root $ApprovedRoot -Label 'Durable output'
    $parent = [IO.Path]::GetDirectoryName($full)
    [IO.Directory]::CreateDirectory($parent) | Out-Null
    $temporary = [IO.Path]::GetFullPath(
        (Join-Path $parent (([IO.Path]::GetFileName($full)) + '.tmp.' +
            [guid]::NewGuid().ToString('N'))))
    Assert-PathUnderRoot -Candidate $temporary -Root $ApprovedRoot -Label 'Durable temporary output'
    $bytes = $script:Utf8NoBom.GetBytes($Content)
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
        if ([IO.File]::Exists($full) -and -not $Replace) {
            throw "Refusing to replace existing durable output: $full"
        }
        [IO.File]::Move($temporary, $full, [bool]$Replace)
    } finally {
        if ($null -ne $stream) { $stream.Dispose() }
        if ([IO.File]::Exists($temporary)) {
            [IO.File]::Delete($temporary)
        }
    }
}

function Test-ZstdArchive {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $zstd = @(Get-Command zstd -CommandType Application -ErrorAction Stop)[0]
    $output = @(& $zstd.Source --quiet --test -- $LiteralPath 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "zstd integrity test failed: $($output -join [Environment]::NewLine)"
    }
}

function Normalize-TarMemberName {
    param([Parameter(Mandatory)] [string] $Name)

    $value = $Name.Replace('\', '/')
    while ($value.StartsWith('./', [StringComparison]::Ordinal)) {
        $value = $value.Substring(2)
    }
    $value = $value.TrimEnd('/')
    Assert-SafeRelativePath -Value $value -Label 'Tar member'
    return $value
}

function Assert-SafeTarLinkTarget {
    param(
        [Parameter(Mandatory)] [string] $MemberName,
        [Parameter(Mandatory)] [string] $LinkName,
        [Parameter(Mandatory)] [string] $TopRoot
    )

    if (
        [string]::IsNullOrWhiteSpace($LinkName) -or
        [IO.Path]::IsPathRooted($LinkName) -or
        $LinkName -match '^[A-Za-z]:' -or
        $LinkName.Contains("`r") -or
        $LinkName.Contains("`n")
    ) {
        throw "Tar link '$MemberName' has unsafe target '$LinkName'"
    }
    $logicalBase = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) 'openfoam_tar_guard'))
    $logicalTop = [IO.Path]::GetFullPath((Join-Path $logicalBase $TopRoot))
    $logicalMemberParent = [IO.Path]::GetFullPath((Join-Path $logicalBase (
        [IO.Path]::GetDirectoryName($MemberName.Replace('/', [IO.Path]::DirectorySeparatorChar)))))
    $resolvedTarget = [IO.Path]::GetFullPath((Join-Path $logicalMemberParent $LinkName))
    if (
        -not $resolvedTarget.Equals($logicalTop, $script:PathComparison) -and
        -not (Test-PathUnderRoot -Candidate $resolvedTarget -Root $logicalTop)
    ) {
        throw "Tar link '$MemberName' escapes its approved archive root: '$LinkName'"
    }
}

function Get-NativeTarMemberNames {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [object[]] $ExpectedEntries
    )

    $tar = @(Get-Command tar -CommandType Application -ErrorAction Stop)[0]
    $output = @(& $tar.Source -tf $LiteralPath 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Native tar listing failed: $($output -join [Environment]::NewLine)"
    }
    $names = [Collections.Generic.List[string]]::new()
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($line in $output) {
        $name = Normalize-TarMemberName -Name ([string]$line)
        if (-not $seen.Add($name)) {
            throw "Native tar listing contains duplicate member '$name'"
        }
        $names.Add($name)
    }
    if ($names.Count -ne $ExpectedEntries.Count) {
        throw "Native tar member count mismatch: expected $($ExpectedEntries.Count), got $($names.Count)"
    }
    for ($index = 0; $index -lt $ExpectedEntries.Count; ++$index) {
        if ($names[$index] -cne [string]$ExpectedEntries[$index].ArchivePath) {
            throw "Native tar member mismatch at index $index`: expected '$($ExpectedEntries[$index].ArchivePath)', got '$($names[$index])'"
        }
    }
    return $names.ToArray()
}

function Get-TarRegularFileSha256 {
    param(
        [Parameter(Mandatory)] $Entry,
        [Parameter(Mandatory)] [string] $Name
    )

    if ($null -eq $Entry.DataStream) {
        if ([int64]$Entry.Length -ne 0) {
            throw "Nonempty tar regular file lacks a data stream: $Name"
        }
        return $script:EmptySha256
    }
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        return [Convert]::ToHexString(
            $algorithm.ComputeHash($Entry.DataStream)).ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
}

function Get-VerifiedZstdTarInventory {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [object[]] $ExpectedEntries,
        [Parameter(Mandatory)] [string] $TopRoot
    )

    Test-ZstdArchive -LiteralPath $LiteralPath
    $expectedOrdered = [object[]]@($ExpectedEntries)
    $null = @(Get-NativeTarMemberNames `
        -LiteralPath $LiteralPath `
        -ExpectedEntries $expectedOrdered)
    $zstd = @(Get-Command zstd -CommandType Application -ErrorAction Stop)[0]
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $zstd.Source
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $true
    foreach ($argument in @('-d', '-c', '--quiet', '--', $LiteralPath)) {
        $startInfo.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw 'Failed to start zstd archive verifier'
    }
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $reader = $null
    $actual = [Collections.Generic.List[object]]::new()
    try {
        $reader = [System.Formats.Tar.TarReader]::new(
            $process.StandardOutput.BaseStream, $true)
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        while ($null -ne ($entry = $reader.GetNextEntry($false))) {
            $name = Normalize-TarMemberName -Name ([string]$entry.Name)
            if (-not $seen.Add($name)) {
                throw "Tar contains duplicate normalized member '$name'"
            }
            $entryType = $entry.EntryType.ToString()
            if ($entryType -in @('RegularFile', 'V7RegularFile')) {
                $sha = Get-TarRegularFileSha256 -Entry $entry -Name $name
                $actual.Add([pscustomobject]@{
                    ArchivePath = $name
                    Kind = 'file'
                    Length = [int64]$entry.Length
                    Sha256 = $sha
                    LinkName = $null
                })
            } elseif ($entryType -ceq 'Directory') {
                if ([int64]$entry.Length -ne 0) {
                    throw "Tar directory has nonzero payload: $name"
                }
                $actual.Add([pscustomobject]@{
                    ArchivePath = $name
                    Kind = 'directory'
                    Length = [int64]0
                    Sha256 = $null
                    LinkName = $null
                })
            } elseif ($entryType -ceq 'SymbolicLink') {
                Assert-SafeTarLinkTarget `
                    -MemberName $name `
                    -LinkName ([string]$entry.LinkName) `
                    -TopRoot $TopRoot
                $actual.Add([pscustomobject]@{
                    ArchivePath = $name
                    Kind = 'link'
                    Length = [int64]0
                    Sha256 = $null
                    LinkName = [string]$entry.LinkName
                })
            } else {
                throw "Tar member '$name' has forbidden type '$entryType'"
            }
        }
        # TarReader stops at the logical end-of-archive marker. Drain any block
        # padding before closing the pipe so zstd does not report a broken pipe.
        $process.StandardOutput.BaseStream.CopyTo([IO.Stream]::Null)
    } finally {
        if ($null -ne $reader) { $reader.Dispose() }
        $process.StandardOutput.Close()
        $process.WaitForExit()
        $stderr = $stderrTask.Result.Trim()
        $exitCode = $process.ExitCode
        $process.Dispose()
    }
    if ($exitCode -ne 0) {
        throw "zstd streaming verifier failed with exit $exitCode`: $stderr"
    }

    $canonicalSeen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($entry in $actual) {
        if (-not $canonicalSeen.Add([string]$entry.ArchivePath)) {
            throw "TarReader produced duplicate normalized member '$($entry.ArchivePath)'"
        }
    }

    $expected = [object[]]@($expectedOrdered)
    [Array]::Sort($expected, [Comparison[object]]{
        param($left, $right)
        return [StringComparer]::Ordinal.Compare(
            [string]$left.ArchivePath, [string]$right.ArchivePath)
    })
    $actualOrdered = [object[]]@($actual)
    [Array]::Sort($actualOrdered, [Comparison[object]]{
        param($left, $right)
        return [StringComparer]::Ordinal.Compare(
            [string]$left.ArchivePath, [string]$right.ArchivePath)
    })
    if ($actualOrdered.Count -ne $expected.Count) {
        throw "Tar member count mismatch: expected $($expected.Count), got $($actualOrdered.Count)"
    }
    $linkNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    for ($index = 0; $index -lt $expected.Count; ++$index) {
        $wanted = $expected[$index]
        $found = $actualOrdered[$index]
        if (
            [string]$wanted.ArchivePath -cne [string]$found.ArchivePath -or
            [string]$wanted.Kind -cne [string]$found.Kind -or
            [int64]$wanted.Length -ne [int64]$found.Length
        ) {
            throw "Tar member mismatch at '$($found.ArchivePath)'"
        }
        if ($wanted.Kind -ceq 'file' -and [string]$wanted.Sha256 -cne [string]$found.Sha256) {
            throw "Tar payload SHA-256 mismatch at '$($wanted.ArchivePath)'"
        }
        if ($found.Kind -ceq 'link') {
            [void]$linkNames.Add([string]$found.ArchivePath)
        }
    }
    foreach ($linkPath in $linkNames) {
        $prefix = "$linkPath/"
        if ($actualOrdered | Where-Object {
            $_.ArchivePath.StartsWith($prefix, [StringComparison]::Ordinal)
        } | Select-Object -First 1) {
            throw "Tar contains a member beneath symbolic link '$linkPath'"
        }
    }
    $listingRecords = @($actualOrdered | ForEach-Object {
        "$($_.Kind)`t$($_.ArchivePath)`t$($_.Length)`t$($_.Sha256)`t$($_.LinkName)"
    })
    return [pscustomobject]@{
        MemberCount = $actualOrdered.Count
        ListingSha256 = Get-Sha256Text -Text (($listingRecords -join "`n") + "`n")
        Entries = $actualOrdered
    }
}

function Get-StagingPaths {
    param(
        [Parameter(Mandatory)] [string] $ArchiveRoot,
        [Parameter(Mandatory)] $Job
    )

    $root = [IO.Path]::GetFullPath((Join-Path $ArchiveRoot $script:StagingRelativePath))
    Assert-PathUnderRoot -Candidate $root -Root $ArchiveRoot -Label 'Staging root'
    $asset = [IO.Path]::GetFullPath((Join-Path $root $Job.AssetName))
    $partial = "$asset.part"
    $metadata = "$asset.json"
    $list = "$asset.files.txt"
    foreach ($path in @($asset, $partial, $metadata, $list)) {
        Assert-PathUnderRoot -Candidate $path -Root $ArchiveRoot -Label 'Staging path'
    }
    return [pscustomobject]@{
        Root = $root
        Asset = $asset
        Partial = $partial
        Metadata = $metadata
        List = $list
    }
}

function Get-StagedMetadataObject {
    param(
        [Parameter(Mandatory)] $Job,
        [Parameter(Mandatory)] $Inventory,
        [Parameter(Mandatory)] [string] $AssetPath,
        [Parameter(Mandatory)] $TarInventory
    )

    $item = Get-Item -LiteralPath $AssetPath -Force
    return [pscustomobject][ordered]@{
        schema_version = 1
        status = 'staged_verified'
        archive_set = $script:ArchiveSetId
        repository = $script:ExpectedRepositorySlug
        category = $Job.Category
        source_locator = $Job.SourceLocator
        relative_root = $Job.RelativeRoot
        asset_name = $Job.AssetName
        asset_path = [IO.Path]::GetFullPath($AssetPath)
        source_files = [int64]$Inventory.SourceFileCount
        source_bytes = [int64]$Inventory.SourceBytes
        source_entries = [int64]$Inventory.EntryCount
        source_directories = [int64]$Inventory.DirectoryCount
        source_links = [int64]$Inventory.LinkCount
        source_listing_sha256 = $Inventory.ListingSha256
        source_fingerprint_sha256 = $Inventory.SourceFingerprintSha256
        asset_bytes = [int64]$item.Length
        asset_sha256 = Get-Sha256File -LiteralPath $AssetPath
        tar_members = [int64]$TarInventory.MemberCount
        tar_listing_sha256 = $TarInventory.ListingSha256
        entries = @(ConvertTo-DurableEntryRecords -Inventory $Inventory)
    }
}

function ConvertTo-WslAbsolutePath {
    param([Parameter(Mandatory)] [string] $WindowsPath)

    if (-not $IsWindows) {
        throw 'LX-reparse archive creation requires Windows with WSL'
    }
    $wsl = @(Get-Command wsl.exe -CommandType Application -ErrorAction Stop)[0]
    $output = @(& $wsl.Source --exec wslpath -a -u $WindowsPath 2>&1)
    if ($LASTEXITCODE -ne 0 -or $output.Count -ne 1) {
        throw "Could not map archive path into WSL: $($output -join [Environment]::NewLine)"
    }
    $value = ([string]$output[0]).Trim()
    if (-not $value.StartsWith('/', [StringComparison]::Ordinal)) {
        throw "WSL returned a non-absolute path: '$value'"
    }
    return $value
}

function New-DeterministicArchiveFile {
    param(
        [Parameter(Mandatory)] $Job,
        [Parameter(Mandatory)] $Inventory,
        [Parameter(Mandatory)] $Paths
    )

    # Use one producer for every legacy asset. Windows libarchive cannot stat
    # WSL LX symlink reparse points and its PAX/USTAR prefix output is parsed
    # incorrectly by System.Formats.Tar for certain >100-byte member names.
    # GNU tar inside WSL handles both cases and writes a deterministic PAX path.
    # The intermediate uncompressed tar remains confined to this one asset's
    # ignored staging directory and is removed immediately after compression.
    $uncompressed = "$($Paths.Partial).uncompressed.tar"
    Assert-PathUnderRoot `
        -Candidate ([IO.Path]::GetFullPath($uncompressed)) `
        -Root $Paths.Root `
        -Label 'WSL intermediate tar'
    if (Test-Path -LiteralPath $uncompressed) {
        [IO.File]::Delete($uncompressed)
    }
    $wsl = @(Get-Command wsl.exe -CommandType Application -ErrorAction Stop)[0]
    $wslParent = ConvertTo-WslAbsolutePath -WindowsPath $Job.ParentRoot
    $wslList = ConvertTo-WslAbsolutePath -WindowsPath $Paths.List
    $wslTar = ConvertTo-WslAbsolutePath -WindowsPath $uncompressed
    $arguments = @(
        '--exec', 'tar', '--create', '--format=pax', '--no-recursion',
        '--no-acls', '--no-xattrs', '--numeric-owner', '--owner=0', '--group=0',
        '--mtime=@0', '--pax-option=delete=atime,delete=ctime',
        '--verbatim-files-from', '--file', $wslTar,
        '--directory', $wslParent, '--files-from', $wslList)
    try {
        $output = @(& $wsl.Source @arguments 2>&1)
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $uncompressed -PathType Leaf)) {
            throw "WSL tar creation failed: $($output -join [Environment]::NewLine)"
        }
        $zstd = @(Get-Command zstd -CommandType Application -ErrorAction Stop)[0]
        $output = @(& $zstd.Source --quiet -f -o $Paths.Partial -- $uncompressed 2>&1)
        if ($LASTEXITCODE -ne 0) {
            throw "zstd compression failed: $($output -join [Environment]::NewLine)"
        }
    } finally {
        if (Test-Path -LiteralPath $uncompressed) {
            [IO.File]::Delete($uncompressed)
        }
    }
}

function Assert-StagedMetadataMatches {
    param(
        [Parameter(Mandatory)] $Actual,
        [Parameter(Mandatory)] $Expected
    )

    $actualJson = $Actual | ConvertTo-Json -Depth 8 -Compress
    $expectedJson = $Expected | ConvertTo-Json -Depth 8 -Compress
    if ($actualJson -cne $expectedJson) {
        throw 'Existing staged metadata conflicts with the exact source/archive bytes'
    }
}

function New-OrResumeStagedArchive {
    param(
        [Parameter(Mandatory)] $Job,
        [Parameter(Mandatory)] $Inventory,
        [Parameter(Mandatory)] [string] $ArchiveRoot
    )

    $paths = Get-StagingPaths -ArchiveRoot $ArchiveRoot -Job $Job
    [IO.Directory]::CreateDirectory($paths.Root) | Out-Null
    $assetExists = Test-Path -LiteralPath $paths.Asset -PathType Leaf
    $metadataExists = Test-Path -LiteralPath $paths.Metadata -PathType Leaf
    if ($assetExists -xor $metadataExists) {
        throw 'Staged archive and its durable metadata must both exist or both be absent'
    }
    if ($assetExists) {
        $tarInventory = Get-VerifiedZstdTarInventory `
            -LiteralPath $paths.Asset `
            -ExpectedEntries $Inventory.Entries `
            -TopRoot $Job.RelativeRoot
        $expected = Get-StagedMetadataObject `
            -Job $Job `
            -Inventory $Inventory `
            -AssetPath $paths.Asset `
            -TarInventory $tarInventory
        $actual = Get-Content -LiteralPath $paths.Metadata -Raw | ConvertFrom-Json
        Assert-StagedMetadataMatches -Actual $actual -Expected $expected
        return [pscustomobject]@{
            Paths = $paths
            Metadata = $expected
            Resumed = $true
        }
    }

    if (Test-Path -LiteralPath $paths.Partial) {
        [IO.File]::Delete($paths.Partial)
    }
    if (Test-Path -LiteralPath $paths.List) {
        [IO.File]::Delete($paths.List)
    }
    $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($paths.Asset))
    $requiredFree = ([int64]$Inventory.SourceBytes * 2) + [int64](512MB)
    if ($drive.AvailableFreeSpace -lt $requiredFree) {
        throw "Insufficient free space for one-asset staging: need $requiredFree, have $($drive.AvailableFreeSpace)"
    }
    Write-DurableUtf8File `
        -LiteralPath $paths.List `
        -Content (($Inventory.Entries.ArchivePath -join "`n") + "`n") `
        -ApprovedRoot $ArchiveRoot
    New-DeterministicArchiveFile -Job $Job -Inventory $Inventory -Paths $paths
    if (-not (Test-Path -LiteralPath $paths.Partial -PathType Leaf)) {
        throw 'Archive creation returned without its exact partial asset'
    }
    $postBuild = Get-SourceInventory -Job $Job -DeepHash $true
    Assert-InventoryEqual -Expected $Inventory -Actual $postBuild -Label $Job.SourceLocator
    $tarInventory = Get-VerifiedZstdTarInventory `
        -LiteralPath $paths.Partial `
        -ExpectedEntries $Inventory.Entries `
        -TopRoot $Job.RelativeRoot
    [IO.File]::Move($paths.Partial, $paths.Asset)
    $metadata = Get-StagedMetadataObject `
        -Job $Job `
        -Inventory $Inventory `
        -AssetPath $paths.Asset `
        -TarInventory $tarInventory
    try {
        Write-DurableUtf8File `
            -LiteralPath $paths.Metadata `
            -Content (($metadata | ConvertTo-Json -Depth 8) + "`n") `
            -ApprovedRoot $ArchiveRoot
    } catch {
        [IO.File]::Delete($paths.Asset)
        throw
    } finally {
        if (Test-Path -LiteralPath $paths.List) {
            [IO.File]::Delete($paths.List)
        }
    }
    return [pscustomobject]@{
        Paths = $paths
        Metadata = $metadata
        Resumed = $false
    }
}

function Invoke-ArchiveGit {
    param(
        [Parameter(Mandatory)] [string[]] $Arguments,
        [switch] $AllowFailure
    )

    $output = @(& git -c "safe.directory=$script:ArchiveRoot" -C $script:ArchiveRoot @Arguments 2>&1)
    $exit = $LASTEXITCODE
    if ($exit -ne 0 -and -not $AllowFailure) {
        throw "git $($Arguments -join ' ') failed ($exit): $($output -join [Environment]::NewLine)"
    }
    return [pscustomobject]@{
        ExitCode = $exit
        Text = ($output -join [Environment]::NewLine).Trim()
    }
}

function Invoke-ArchiveGitBytes {
    param([Parameter(Mandatory)] [string[]] $Arguments)

    $git = @(Get-Command git -CommandType Application -ErrorAction Stop)[0]
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $git.Source
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $true
    foreach ($argument in @(
        '-c', "safe.directory=$script:ArchiveRoot", '-C', $script:ArchiveRoot
    ) + $Arguments) {
        $startInfo.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) { throw 'Failed to start git byte reader' }
    $memory = [IO.MemoryStream]::new()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    try {
        $process.StandardOutput.BaseStream.CopyTo($memory)
        $process.WaitForExit()
        $stderr = $stderrTask.Result.Trim()
        if ($process.ExitCode -ne 0) {
            throw "git byte reader failed ($($process.ExitCode)): $stderr"
        }
        $resultBytes = [byte[]]$memory.ToArray()
        Write-Output -NoEnumerate $resultBytes
        return
    } finally {
        $memory.Dispose()
        $process.Dispose()
    }
}

function Get-ArchiveGitStatusRecords {
    $bytes = Invoke-ArchiveGitBytes -Arguments @(
        'status', '--porcelain=v1', '-z', '--untracked-files=all')
    $text = [Text.Encoding]::UTF8.GetString($bytes)
    $records = @($text -split [char]0 | Where-Object { $_ })
    return @($records | ForEach-Object {
        if ($_.Length -lt 4 -or $_[2] -cne ' ') {
            throw 'Unsupported Git porcelain record'
        }
        if ($_.Substring(0, 2) -match '[RC]') {
            throw 'Archive repository rename/copy state is not supported'
        }
        [pscustomobject]@{
            Status = $_.Substring(0, 2)
            Path = $_.Substring(3).Replace('\', '/')
        }
    })
}

function ConvertTo-GitHubRepositorySlug {
    param([Parameter(Mandatory)] [string] $RemoteUrl)

    $value = $RemoteUrl.Trim()
    if ($value -match '^https://github\.com/(?<slug>[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+?)(?:\.git)?/?$') {
        return $Matches.slug
    }
    if ($value -match '^git@github\.com:(?<slug>[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+?)(?:\.git)?$') {
        return $Matches.slug
    }
    if ($value -match '^ssh://git@github\.com/(?<slug>[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+?)(?:\.git)?/?$') {
        return $Matches.slug
    }
    throw 'Archive origin must be an exact credential-free github.com URL'
}

function Get-GitHubRepositoryIdentity {
    $fetch = (Invoke-ArchiveGit -Arguments @('remote', 'get-url', 'origin')).Text.Trim()
    $push = @(
        (Invoke-ArchiveGit -Arguments @('remote', 'get-url', '--push', '--all', 'origin')).Text `
            -split '\r?\n' | Where-Object { $_ })
    if ($push.Count -ne 1) {
        throw "Archive origin must have one push URL; found $($push.Count)"
    }
    $fetchSlug = ConvertTo-GitHubRepositorySlug -RemoteUrl $fetch
    $pushSlug = ConvertTo-GitHubRepositorySlug -RemoteUrl $push[0]
    if (-not $fetchSlug.Equals($pushSlug, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Archive fetch and push origins differ'
    }
    if (-not $fetchSlug.Equals(
        $script:ExpectedRepositorySlug,
        [StringComparison]::OrdinalIgnoreCase
    )) {
        throw "Wrong archive repository: $fetchSlug"
    }
    return [pscustomobject]@{
        Slug = $fetchSlug
        FetchUrl = $fetch
        PushUrl = $push[0].Trim()
    }
}

function Start-ArchiveRepoLock {
    param([Parameter(Mandatory)] [string] $GitMetadataPath)

    $path = [IO.Path]::GetFullPath(
        (Join-Path $GitMetadataPath 'thermal_checkpoint_archive.lock'))
    Assert-PathUnderRoot -Candidate $path -Root $GitMetadataPath -Label 'Archive repository lock'
    try {
        return [IO.File]::Open(
            $path,
            [IO.FileMode]::OpenOrCreate,
            [IO.FileAccess]::ReadWrite,
            [IO.FileShare]::None)
    } catch [IO.IOException] {
        throw 'Another archive helper holds the archive repository lock'
    }
}

function Assert-ArchiveRepoLockHeld {
    if ($null -eq $script:ArchiveRepoLock -or -not $script:ArchiveRepoLock.CanWrite) {
        throw 'Archive repository lock is no longer held'
    }
}

function Get-GitHubHeaders {
    $newline = [Environment]::NewLine
    $credentialInput = 'protocol=https' + $newline + 'host=github.com' + $newline + $newline
    $lines = @($credentialInput | git credential fill)
    if ($LASTEXITCODE -ne 0) {
        throw 'git credential fill failed for github.com'
    }
    $password = $lines | Where-Object { $_ -like 'password=*' } | Select-Object -First 1
    if (-not $password) {
        throw 'No stored GitHub credential was returned'
    }
    $script:GitHubToken = $password.Substring(9)
    $lines = $null
    return @{
        'User-Agent' = 'thermal-sim-openfoam-legacy-archive'
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

    if ($null -ne $script:GitHubGetOverride) {
        return & $script:GitHubGetOverride $Uri $Headers
    }
    $response = Invoke-WebRequest `
        -Method Get `
        -Uri $Uri `
        -Headers $Headers `
        -UseBasicParsing `
        -Verbose:$false `
        -Debug:$false
    $parsed = ConvertFrom-Json -InputObject ([string]$response.Content)
    if ($parsed -is [Array]) {
        foreach ($item in $parsed) {
            Write-Output $item
        }
        return
    }
    return $parsed
}

function Get-Release {
    param(
        [Parameter(Mandatory)] [string] $RepositorySlug,
        [Parameter(Mandatory)] [string] $Tag,
        [Parameter(Mandatory)] [hashtable] $Headers
    )

    $encodedTag = [Uri]::EscapeDataString($Tag)
    $release = Invoke-GitHubGet `
        -Uri "https://api.github.com/repos/$RepositorySlug/releases/tags/$encodedTag" `
        -Headers $Headers
    if ([string]$release.tag_name -cne $Tag -or [string]$release.id -notmatch '^[0-9]+$') {
        throw 'GitHub returned an unexpected release identity'
    }
    return $release
}

function Get-AllReleaseAssets {
    param(
        [Parameter(Mandatory)] [string] $RepositorySlug,
        [Parameter(Mandatory)] [string] $ReleaseId,
        [Parameter(Mandatory)] [hashtable] $Headers
    )

    if ($ReleaseId -notmatch '^[0-9]+$') {
        throw "Unsafe release id: $ReleaseId"
    }
    $assetsBase = "https://api.github.com/repos/$RepositorySlug/releases/$ReleaseId/assets"
    $all = [Collections.Generic.List[object]]::new()
    $ids = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    for ($page = 1; $page -le 1000; ++$page) {
        $pageUri = "${assetsBase}?per_page=100&page=$page"
        $items = @(Invoke-GitHubGet -Uri $pageUri -Headers $Headers)
        if ($items.Count -gt 100) {
            throw "GitHub returned more than 100 release assets on page $page"
        }
        foreach ($item in $items) {
            $id = [string]$item.id
            if ($id -notmatch '^[0-9]+$' -or -not $ids.Add($id)) {
                throw "GitHub returned an invalid or duplicate release asset id '$id'"
            }
            $all.Add($item)
        }
        if ($items.Count -lt 100) {
            return $all.ToArray()
        }
    }
    throw 'GitHub release-asset pagination exceeded 1000 pages'
}

function Get-SingleReleaseAsset {
    param(
        [Parameter(Mandatory)] [object[]] $Assets,
        [Parameter(Mandatory)] [string] $Name
    )

    $matches = @($Assets | Where-Object { [string]$_.name -ceq $Name })
    if ($matches.Count -gt 1) {
        throw "Release contains duplicate same-name assets '$Name'"
    }
    if ($matches.Count -eq 0) { return $null }
    return $matches[0]
}

function Assert-RemoteAssetMatches {
    param(
        [Parameter(Mandatory)] $Asset,
        [Parameter(Mandatory)] [int64] $ExpectedBytes,
        [Parameter(Mandatory)] [string] $ExpectedSha256
    )

    if ($ExpectedSha256 -notmatch '^[0-9a-f]{64}$') {
        throw 'Expected remote SHA-256 must be lowercase hexadecimal'
    }
    if ([int64]$Asset.size -ne $ExpectedBytes) {
        throw "GitHub asset byte mismatch: remote=$($Asset.size), local=$ExpectedBytes"
    }
    $digest = "sha256:$ExpectedSha256"
    if (
        [string]::IsNullOrWhiteSpace([string]$Asset.digest) -or
        -not ([string]$Asset.digest).Equals($digest, [StringComparison]::OrdinalIgnoreCase)
    ) {
        throw "GitHub asset digest mismatch: remote='$($Asset.digest)', expected='$digest'"
    }
    if ([string]$Asset.state -cne 'uploaded') {
        throw "GitHub asset state is '$($Asset.state)', not uploaded"
    }
}

function Get-VerifiedRemoteAsset {
    param(
        [Parameter(Mandatory)] [string] $RepositorySlug,
        [Parameter(Mandatory)] $Release,
        [Parameter(Mandatory)] [string] $AssetName,
        [Parameter(Mandatory)] [int64] $ExpectedBytes,
        [Parameter(Mandatory)] [string] $ExpectedSha256,
        [Parameter(Mandatory)] [hashtable] $Headers
    )

    $assets = @(Get-AllReleaseAssets `
        -RepositorySlug $RepositorySlug `
        -ReleaseId ([string]$Release.id) `
        -Headers $Headers)
    $asset = Get-SingleReleaseAsset -Assets $assets -Name $AssetName
    if ($null -eq $asset) {
        return $null
    }
    Assert-RemoteAssetMatches `
        -Asset $asset `
        -ExpectedBytes $ExpectedBytes `
        -ExpectedSha256 $ExpectedSha256
    return $asset
}

function Publish-OrResumeReleaseAsset {
    param(
        [Parameter(Mandatory)] [string] $RepositorySlug,
        [Parameter(Mandatory)] $Release,
        [Parameter(Mandatory)] [string] $AssetName,
        [Parameter(Mandatory)] [string] $StagedPath,
        [Parameter(Mandatory)] [int64] $ExpectedBytes,
        [Parameter(Mandatory)] [string] $ExpectedSha256,
        [Parameter(Mandatory)] [hashtable] $Headers
    )

    $existing = Get-VerifiedRemoteAsset `
        -RepositorySlug $RepositorySlug `
        -Release $Release `
        -AssetName $AssetName `
        -ExpectedBytes $ExpectedBytes `
        -ExpectedSha256 $ExpectedSha256 `
        -Headers $Headers
    if ($null -ne $existing) { return $existing }

    $uploadBase = ([string]$Release.upload_url) -replace '\{.*$', ''
    $uploadUri = [Uri]$uploadBase
    $prefix = "/repos/$RepositorySlug/releases/"
    $pathMatches = $uploadUri.AbsolutePath.StartsWith(
        $prefix, [StringComparison]::OrdinalIgnoreCase)
    $tail = if ($pathMatches) {
        $uploadUri.AbsolutePath.Substring($prefix.Length)
    } else { '' }
    if (
        $uploadUri.Scheme -cne 'https' -or
        -not $uploadUri.Host.Equals('uploads.github.com', [StringComparison]::OrdinalIgnoreCase) -or
        -not $pathMatches -or
        $tail -notmatch '^[0-9]+/assets$'
    ) {
        throw "GitHub returned an unexpected upload endpoint: $uploadBase"
    }
    $uploadWithName = "${uploadBase}?name=$([Uri]::EscapeDataString($AssetName))"
    try {
        Invoke-RestMethod `
            -Method Post `
            -Uri $uploadWithName `
            -Headers $Headers `
            -ContentType 'application/zstd' `
            -InFile $StagedPath `
            -Verbose:$false `
            -Debug:$false | Out-Null
    } catch {
        $raceAsset = Get-VerifiedRemoteAsset `
            -RepositorySlug $RepositorySlug `
            -Release $Release `
            -AssetName $AssetName `
            -ExpectedBytes $ExpectedBytes `
            -ExpectedSha256 $ExpectedSha256 `
            -Headers $Headers
        if ($null -eq $raceAsset) {
            throw
        }
        return $raceAsset
    }
    $uploaded = Get-VerifiedRemoteAsset `
        -RepositorySlug $RepositorySlug `
        -Release $Release `
        -AssetName $AssetName `
        -ExpectedBytes $ExpectedBytes `
        -ExpectedSha256 $ExpectedSha256 `
        -Headers $Headers
    if ($null -eq $uploaded) {
        throw "Uploaded asset is absent from the paginated release listing: $AssetName"
    }
    return $uploaded
}

function Get-ExpectedManifestRow {
    param(
        [Parameter(Mandatory)] $Job,
        [Parameter(Mandatory)] $Metadata,
        [Parameter(Mandatory)] $RemoteAsset
    )

    $uploadedAt = ([datetime]$RemoteAsset.created_at).ToUniversalTime().ToString(
        'yyyy-MM-ddTHH:mm:ssZ', $script:InvariantCulture)
    return [pscustomobject][ordered]@{
        archive_set = $script:ArchiveSetId
        category = $Job.Category
        source_locator = $Job.SourceLocator
        asset = $Job.AssetName
        source_files = [string]$Metadata.source_files
        source_bytes = [string]$Metadata.source_bytes
        source_entries = [string]$Metadata.source_entries
        source_directories = [string]$Metadata.source_directories
        source_links = [string]$Metadata.source_links
        source_listing_sha256 = [string]$Metadata.source_listing_sha256
        source_fingerprint_sha256 = [string]$Metadata.source_fingerprint_sha256
        asset_bytes = [string]$Metadata.asset_bytes
        sha256 = [string]$Metadata.asset_sha256
        github_digest = "sha256:$($Metadata.asset_sha256)"
        tar_members = [string]$Metadata.tar_members
        tar_listing_sha256 = [string]$Metadata.tar_listing_sha256
        uploaded_at_utc = $uploadedAt
        status = 'verified'
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

function Assert-ManifestIntegrity {
    param([Parameter(Mandatory)] [string] $CsvText)

    $firstLine = ($CsvText -split '\r?\n', 2)[0]
    if ($firstLine -cne $script:ManifestHeader) {
        throw "Unexpected legacy manifest header: '$firstLine'"
    }
    $rows = @($CsvText | ConvertFrom-Csv)
    $assets = @{}
    $sources = @{}
    foreach ($row in $rows) {
        if ($assets.ContainsKey([string]$row.asset)) {
            throw "Legacy manifest duplicates asset '$($row.asset)'"
        }
        if ($sources.ContainsKey([string]$row.source_locator)) {
            throw "Legacy manifest duplicates source '$($row.source_locator)'"
        }
        $assets[[string]$row.asset] = $true
        $sources[[string]$row.source_locator] = $true
    }
}

function Assert-ManifestRowState {
    param(
        [Parameter(Mandatory)] [string] $CsvText,
        [Parameter(Mandatory)] $Expected,
        [Parameter(Mandatory)] [string] $Label,
        [switch] $AllowMissing
    )

    Assert-ManifestIntegrity -CsvText $CsvText
    $matches = @($CsvText | ConvertFrom-Csv | Where-Object {
        $_.asset -ceq $Expected.asset -or
        $_.source_locator -ceq $Expected.source_locator
    })
    if ($matches.Count -eq 0) {
        if ($AllowMissing) { return $false }
        throw "$Label lacks the verified legacy manifest row"
    }
    if ($matches.Count -ne 1 -or -not (
        Test-ManifestRowMatches -Actual $matches[0] -Expected $Expected
    )) {
        throw "$Label contains a conflicting source/asset manifest row"
    }
    return $true
}

function Get-ManifestTextFromHead {
    param([Parameter(Mandatory)] [string] $Commit)

    $result = Invoke-ArchiveGit -Arguments @(
        'cat-file', '-e', "$Commit`:$($script:ManifestRelativePath)") -AllowFailure
    if ($result.ExitCode -ne 0) {
        return $script:ManifestHeader + "`n"
    }
    $bytes = Invoke-ArchiveGitBytes -Arguments @(
        'cat-file', 'blob', "$Commit`:$($script:ManifestRelativePath)")
    return [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
}

function Get-RemoteBranchHead {
    param(
        [Parameter(Mandatory)] [string] $RepositorySlug,
        [Parameter(Mandatory)] [string] $BranchName,
        [Parameter(Mandatory)] [hashtable] $Headers
    )

    $encoded = [Uri]::EscapeDataString($BranchName)
    $response = Invoke-GitHubGet `
        -Uri "https://api.github.com/repos/$RepositorySlug/branches/$encoded" `
        -Headers $Headers
    $sha = ([string]$response.commit.sha).Trim()
    if ($sha -notmatch '^[0-9a-f]{40,64}$') {
        throw "GitHub returned an invalid branch head '$sha'"
    }
    return $sha
}

function Get-RemoteManifestBytes {
    param(
        [Parameter(Mandatory)] [string] $RepositorySlug,
        [Parameter(Mandatory)] [string] $BranchName,
        [Parameter(Mandatory)] [hashtable] $Headers
    )

    $encodedBranch = [Uri]::EscapeDataString($BranchName)
    $contentsBase = "https://api.github.com/repos/$RepositorySlug/contents/$($script:ManifestRelativePath)"
    $response = Invoke-GitHubGet `
        -Uri "${contentsBase}?ref=$encodedBranch" `
        -Headers $Headers
    $resultBytes = [byte[]][Convert]::FromBase64String(
        ([string]$response.content -replace '\s', ''))
    Write-Output -NoEnumerate $resultBytes
    return
}

function Assert-ByteArraysEqual {
    param(
        [Parameter(Mandatory)] [byte[]] $Expected,
        [Parameter(Mandatory)] [byte[]] $Actual,
        [Parameter(Mandatory)] [string] $Label
    )

    if (
        $Expected.Length -ne $Actual.Length -or
        [Convert]::ToBase64String($Expected) -cne [Convert]::ToBase64String($Actual)
    ) {
        throw "$Label is not byte-for-byte identical"
    }
}

function Add-Push-AndVerifyManifestRow {
    param(
        [Parameter(Mandatory)] $ExpectedRow,
        [Parameter(Mandatory)] [string] $RepositorySlug,
        [Parameter(Mandatory)] [string] $ApprovedPushUrl,
        [Parameter(Mandatory)] [string] $BranchName,
        [Parameter(Mandatory)] [hashtable] $Headers
    )

    Assert-ArchiveRepoLockHeld
    $localHead = (Invoke-ArchiveGit -Arguments @('rev-parse', 'HEAD')).Text.Trim()
    $remoteHead = Get-RemoteBranchHead `
        -RepositorySlug $RepositorySlug `
        -BranchName $BranchName `
        -Headers $Headers
    $approvedRemoteBase = $remoteHead
    $headText = Get-ManifestTextFromHead -Commit $localHead
    $rowInHead = Assert-ManifestRowState `
        -CsvText $headText `
        -Expected $ExpectedRow `
        -Label 'Local HEAD manifest' `
        -AllowMissing
    $status = @(Get-ArchiveGitStatusRecords)
    $unrelated = @($status | Where-Object Path -cne $script:ManifestRelativePath)
    if ($unrelated.Count -ne 0) {
        throw "Archive repository has unrelated changes: $($unrelated.Path -join ', ')"
    }

    if ($localHead -cne $remoteHead) {
        if (-not $rowInHead -or $status.Count -ne 0) {
            throw "Local/remote $BranchName differ without one clean resumable manifest commit"
        }
        $ancestor = Invoke-ArchiveGit -Arguments @(
            'merge-base', '--is-ancestor', $remoteHead, $localHead) -AllowFailure
        $ahead = (Invoke-ArchiveGit -Arguments @(
            'rev-list', '--count', "$remoteHead..$localHead")).Text.Trim()
        $paths = @(
            (Invoke-ArchiveGit -Arguments @(
                'diff-tree', '--no-commit-id', '--name-only', '-r', $localHead
            )).Text -split '\r?\n' | Where-Object { $_ })
        if (
            $ancestor.ExitCode -ne 0 -or
            $ahead -cne '1' -or
            ($paths -join ',') -cne $script:ManifestRelativePath
        ) {
            throw 'Local branch is not exactly one manifest-only commit ahead of remote'
        }
    } else {
        $manifestPath = [IO.Path]::GetFullPath(
            (Join-Path $script:ArchiveRoot $script:ManifestRelativePath))
        Assert-PathUnderRoot -Candidate $manifestPath -Root $script:ArchiveRoot -Label 'Legacy manifest'
        $rowCsv = @($ExpectedRow | ConvertTo-Csv -NoTypeInformation -UseQuotes AsNeeded)
        if (($rowCsv[0] -replace '^\uFEFF', '') -cne $script:ManifestHeader) {
            throw 'Internal legacy manifest schema drifted'
        }
        $nextText = ($headText -replace '\r\n?', "`n").TrimEnd("`n") + "`n"
        if (-not $rowInHead) {
            $nextText += $rowCsv[1] + "`n"
        }
        Assert-ManifestRowState `
            -CsvText $nextText `
            -Expected $ExpectedRow `
            -Label 'Prospective legacy manifest' | Out-Null
        if ($rowInHead) {
            if ($status.Count -ne 0) {
                throw 'Verified row is committed but legacy manifest is dirty'
            }
        } else {
            if ($status.Count -ne 0) {
                $working = [IO.File]::ReadAllText($manifestPath, $script:Utf8NoBom)
                if ($working -cne $nextText) {
                    throw 'Dirty legacy manifest is not the exact resumable append'
                }
            } else {
                Write-DurableUtf8File `
                    -LiteralPath $manifestPath `
                    -Content $nextText `
                    -ApprovedRoot $script:ArchiveRoot `
                    -Replace
            }
            $remoteRecheck = Get-RemoteBranchHead `
                -RepositorySlug $RepositorySlug `
                -BranchName $BranchName `
                -Headers $Headers
            if ($remoteRecheck -cne $remoteHead) {
                throw "Remote $BranchName changed before manifest commit"
            }
            Invoke-ArchiveGit -Arguments @('add', '--', $script:ManifestRelativePath) | Out-Null
            Invoke-ArchiveGit -Arguments @(
                'commit', '-m', "Record verified OpenFOAM legacy asset $($ExpectedRow.asset)"
            ) | Out-Null
            $newHead = (Invoke-ArchiveGit -Arguments @('rev-parse', 'HEAD')).Text.Trim()
            $parent = (Invoke-ArchiveGit -Arguments @('rev-parse', "$newHead^")).Text.Trim()
            $paths = @(
                (Invoke-ArchiveGit -Arguments @(
                    'diff-tree', '--no-commit-id', '--name-only', '-r', $newHead
                )).Text -split '\r?\n' | Where-Object { $_ })
            if ($parent -cne $localHead -or ($paths -join ',') -cne $script:ManifestRelativePath) {
                throw 'Created commit is not the exact manifest-only child expected'
            }
            $localHead = $newHead
        }
    }

    $freshRemote = Get-RemoteBranchHead `
        -RepositorySlug $RepositorySlug `
        -BranchName $BranchName `
        -Headers $Headers
    if ($freshRemote -cne $localHead) {
        if ($freshRemote -cne $approvedRemoteBase) {
            throw "Remote $BranchName changed after manifest validation; refusing push"
        }
        $identity = Get-GitHubRepositoryIdentity
        if (
            [string]$identity.PushUrl -cne $ApprovedPushUrl -or
            -not ([string]$identity.Slug).Equals(
                $RepositorySlug, [StringComparison]::OrdinalIgnoreCase)
        ) {
            throw 'Archive origin changed before push'
        }
        Invoke-ArchiveGit -Arguments @(
            'push', $ApprovedPushUrl, "$localHead`:refs/heads/$BranchName") | Out-Null
    }

    $verifiedHead = Get-RemoteBranchHead `
        -RepositorySlug $RepositorySlug `
        -BranchName $BranchName `
        -Headers $Headers
    if ($verifiedHead -cne $localHead) {
        throw "Remote $BranchName did not advance to the verified manifest commit"
    }
    $localBytes = Invoke-ArchiveGitBytes -Arguments @(
        'cat-file', 'blob', "$localHead`:$($script:ManifestRelativePath)")
    $remoteBytes = Get-RemoteManifestBytes `
        -RepositorySlug $RepositorySlug `
        -BranchName $BranchName `
        -Headers $Headers
    Assert-ByteArraysEqual `
        -Expected $localBytes `
        -Actual $remoteBytes `
        -Label "Remote $BranchName legacy manifest"
    $remoteText = [Text.UTF8Encoding]::new($false, $true).GetString($remoteBytes)
    Assert-ManifestRowState `
        -CsvText $remoteText `
        -Expected $ExpectedRow `
        -Label "Remote $BranchName legacy manifest" | Out-Null
    return [pscustomobject]@{
        Commit = $localHead
        ManifestBytes = $localBytes
    }
}

function Move-DirectoryAtomically {
    param(
        [Parameter(Mandatory)] [string] $Source,
        [Parameter(Mandatory)] [string] $Destination,
        [Parameter(Mandatory)] [string] $Label
    )

    $sourceFull = [IO.Path]::GetFullPath($Source).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
    $destinationFull = [IO.Path]::GetFullPath($Destination).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
    if ($sourceFull.Equals($destinationFull, $script:PathComparison)) {
        throw "$Label source and destination are identical"
    }
    if (-not [IO.Directory]::Exists($sourceFull)) {
        throw "$Label source directory is absent: $sourceFull"
    }
    if ([IO.Directory]::Exists($destinationFull) -or [IO.File]::Exists($destinationFull)) {
        throw "$Label destination must not exist: $destinationFull"
    }
    $sourceVolume = [IO.Path]::GetPathRoot($sourceFull).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
    $destinationVolume = [IO.Path]::GetPathRoot($destinationFull).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
    if (-not $sourceVolume.Equals($destinationVolume, $script:PathComparison)) {
        throw "$Label requires a same-volume atomic directory rename"
    }
    $destinationParent = [IO.Path]::GetDirectoryName($destinationFull)
    if (-not [IO.Directory]::Exists($destinationParent)) {
        throw "$Label destination parent is absent: $destinationParent"
    }
    [IO.Directory]::Move($sourceFull, $destinationFull)
    if ([IO.Directory]::Exists($sourceFull) -or -not [IO.Directory]::Exists($destinationFull)) {
        throw "$Label did not complete as one exact directory rename"
    }
}

function Remove-RecoveryTreeNoFollow {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $RelativeBase,
        [Parameter(Mandatory)] [string] $ApprovedRoot,
        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [Collections.Generic.HashSet[string]] $AllowedReparsePaths
    )

    $full = [IO.Path]::GetFullPath($LiteralPath)
    Assert-PathUnderRoot -Candidate $full -Root $ApprovedRoot -Label 'No-follow recovery cleanup'
    $rootItem = Get-Item -LiteralPath $full -Force
    if (
        -not $rootItem.PSIsContainer -or
        ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
    ) {
        throw "No-follow recovery cleanup root is not an ordinary directory: $full"
    }
    foreach ($child in @([IO.DirectoryInfo]::new($full).EnumerateFileSystemInfos())) {
        $childFull = [IO.Path]::GetFullPath($child.FullName)
        Assert-PathUnderRoot `
            -Candidate $childFull `
            -Root $ApprovedRoot `
            -Label 'No-follow recovery cleanup entry'
        $isReparse =
            ($child.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
        $isDirectory =
            ($child.Attributes -band [IO.FileAttributes]::Directory) -ne 0
        if ($isReparse) {
            $relative = [IO.Path]::GetRelativePath(
                $RelativeBase, $childFull).Replace('\', '/')
            Assert-SafeRelativePath -Value $relative -Label 'Recovery cleanup reparse entry'
            if (-not $AllowedReparsePaths.Contains($relative)) {
                throw "Unexpected nested reparse point blocks recovery cleanup: $childFull"
            }
            if ($isDirectory) {
                [IO.Directory]::Delete($childFull)
            } else {
                [IO.File]::Delete($childFull)
            }
            continue
        }
        if ($isDirectory) {
            Remove-RecoveryTreeNoFollow `
                -LiteralPath $childFull `
                -RelativeBase $RelativeBase `
                -ApprovedRoot $ApprovedRoot `
                -AllowedReparsePaths $AllowedReparsePaths
            continue
        }
        if (-not ($child -is [IO.FileInfo])) {
            throw "Unsupported entry blocks no-follow recovery cleanup: $childFull"
        }
        [IO.File]::Delete($childFull)
    }
    [IO.Directory]::Delete($full)
}

function Remove-RecoveryArtifactsAfterVerifiedRestore {
    param(
        [Parameter(Mandatory)] $Job,
        [Parameter(Mandatory)] [string] $QuarantineRoot
    )

    if (-not (Test-Path -LiteralPath $QuarantineRoot -PathType Container)) {
        return
    }
    $root = [IO.Path]::GetFullPath($QuarantineRoot)
    $rootItem = Get-Item -LiteralPath $root -Force
    if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Recovery quarantine root is a reparse point and will be preserved: $root"
    }
    $allowedReparsePaths = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    foreach ($relative in @($Job.AllowedReparseHashes.Keys)) {
        [void]$allowedReparsePaths.Add([string]$relative)
    }
    foreach ($entry in @(Get-ChildItem -LiteralPath $root -Force)) {
        $entryFull = [IO.Path]::GetFullPath($entry.FullName)
        Assert-PathUnderRoot -Candidate $entryFull -Root $root -Label 'Verified recovery cleanup'
        $allowed =
            $entry.PSIsContainer -and
            (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) -and (
                $entry.Name -ceq $Job.RelativeRoot -or
                $entry.Name -cmatch '^\.restore\.[0-9a-f]{32}$'
            )
        if (-not $allowed) {
            throw "Unexpected recovery artifact retained after source restore: $entryFull"
        }
    }
    foreach ($entry in @(Get-ChildItem -LiteralPath $root -Force)) {
        if ($entry.Name -ceq $Job.RelativeRoot) {
            Remove-RecoveryTreeNoFollow `
                -LiteralPath $entry.FullName `
                -RelativeBase $root `
                -ApprovedRoot $root `
                -AllowedReparsePaths $allowedReparsePaths
            continue
        }
        $children = @(Get-ChildItem -LiteralPath $entry.FullName -Force)
        $unexpected = @($children | Where-Object Name -cne $Job.RelativeRoot)
        if ($unexpected.Count -ne 0) {
            throw "Unexpected root blocks candidate cleanup: $($unexpected[0].FullName)"
        }
        if ($children.Count -gt 1) {
            throw "Conflicting roots block candidate cleanup: $($entry.FullName)"
        }
        if ($children.Count -eq 1) {
            $candidateSource = $children[0]
            if (
                -not $candidateSource.PSIsContainer -or
                ($candidateSource.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
            ) {
                throw "Candidate cleanup root is not an ordinary directory: $($candidateSource.FullName)"
            }
            Remove-RecoveryTreeNoFollow `
                -LiteralPath $candidateSource.FullName `
                -RelativeBase $entry.FullName `
                -ApprovedRoot $root `
                -AllowedReparsePaths $allowedReparsePaths
        }
        if (Get-ChildItem -LiteralPath $entry.FullName -Force) {
            throw "Candidate wrapper is not empty after no-follow cleanup: $($entry.FullName)"
        }
        [IO.Directory]::Delete($entry.FullName)
    }
    if (Get-ChildItem -LiteralPath $root -Force) {
        throw "Verified recovery cleanup left content in quarantine: $root"
    }
    [IO.Directory]::Delete($root)
}

function Get-PreservedRecoveryCandidateState {
    param(
        [Parameter(Mandatory)] $Job,
        [Parameter(Mandatory)] $Metadata,
        [Parameter(Mandatory)] [string] $QuarantineRoot,
        [Parameter(Mandatory)] [string] $QuarantineGroup
    )

    $candidates = [Collections.Generic.List[object]]::new()
    $valid = [Collections.Generic.List[object]]::new()
    $invalid = [Collections.Generic.List[object]]::new()
    $conflicts = [Collections.Generic.List[object]]::new()
    if (-not (Test-Path -LiteralPath $QuarantineRoot -PathType Container)) {
        return [pscustomobject]@{
            Candidates = $candidates.ToArray()
            Valid = $valid.ToArray()
            Invalid = $invalid.ToArray()
            Conflicts = $conflicts.ToArray()
        }
    }

    $root = [IO.Path]::GetFullPath($QuarantineRoot)
    $group = [IO.Path]::GetFullPath($QuarantineGroup)
    $rootItem = Get-Item -LiteralPath $root -Force
    if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Recovery quarantine root is a reparse point: $root"
    }
    foreach ($entry in @(Get-ChildItem -LiteralPath $root -Force)) {
        $entryFull = [IO.Path]::GetFullPath($entry.FullName)
        Assert-PathUnderRoot `
            -Candidate $entryFull `
            -Root $root `
            -Label 'Preserved recovery entry'
        if ($entryFull.Equals($group, $script:PathComparison)) {
            if (
                -not $entry.PSIsContainer -or
                ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
            ) {
                throw "Recovery quarantine group is not an ordinary directory: $entryFull"
            }
            continue
        }
        if (-not ($entry.Name -cmatch '^\.restore\.[0-9a-f]{32}$')) {
            throw "Unexpected entry conflicts with preserved recovery candidates: $entryFull"
        }
        if (
            -not $entry.PSIsContainer -or
            ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
        ) {
            throw "Preserved recovery candidate is not an ordinary directory: $entryFull"
        }
        $candidate = [pscustomobject]@{
            Root = $entryFull
            SourceRoot = [IO.Path]::GetFullPath((Join-Path $entryFull $Job.RelativeRoot))
            Inventory = $null
            Failure = $null
        }
        [void]$candidates.Add($candidate)
    }

    foreach ($candidate in $candidates) {
        $children = @(Get-ChildItem -LiteralPath $candidate.Root -Force)
        $unexpected = @($children | Where-Object Name -cne $Job.RelativeRoot)
        if ($unexpected.Count -ne 0) {
            throw "Preserved recovery candidate contains an unexpected root: $($unexpected[0].FullName)"
        }
        if ($children.Count -eq 0) {
            $candidate.Failure = 'candidate extraction root is empty'
            [void]$invalid.Add($candidate)
            continue
        }
        if ($children.Count -ne 1) {
            throw "Preserved recovery candidate has conflicting top-level entries: $($candidate.Root)"
        }
        $sourceItem = $children[0]
        if (
            -not $sourceItem.PSIsContainer -or
            ($sourceItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
        ) {
            throw "Preserved candidate source root is not an ordinary directory: $($sourceItem.FullName)"
        }
        try {
            $candidateJob = [pscustomobject]@{
                ParentRoot = $candidate.Root
                RelativeRoot = $Job.RelativeRoot
                AllowedReparseHashes = $Job.AllowedReparseHashes
            }
            $candidate.Inventory = Get-SourceInventory -Job $candidateJob -DeepHash $true
            if (-not (Test-InventoryMatchesDurableMetadata `
                -Inventory $candidate.Inventory `
                -Metadata $Metadata
            )) {
                $candidate.Failure = 'candidate inventory does not match durable archived source metadata'
                [void]$invalid.Add($candidate)
                continue
            }
            [void]$valid.Add($candidate)
        } catch {
            $candidate.Failure = $_.Exception.Message
            [void]$conflicts.Add($candidate)
        }
    }
    return [pscustomobject]@{
        Candidates = $candidates.ToArray()
        Valid = $valid.ToArray()
        Invalid = $invalid.ToArray()
        Conflicts = $conflicts.ToArray()
    }
}

function Get-DeletionPaths {
    param(
        [Parameter(Mandatory)] [string] $ArchiveRoot,
        [Parameter(Mandatory)] $Job
    )

    $root = [IO.Path]::GetFullPath((Join-Path $ArchiveRoot $script:DeletionRelativePath))
    Assert-PathUnderRoot -Candidate $root -Root $ArchiveRoot -Label 'Deletion journal root'
    $container = [IO.Path]::GetFullPath((Join-Path $root $Job.AssetName))
    $group = [IO.Path]::GetFullPath((Join-Path $container $Job.RelativeRoot))
    $receipt = [IO.Path]::GetFullPath((Join-Path $root "$($Job.AssetName).delete.json"))
    foreach ($path in @($container, $group, $receipt)) {
        Assert-PathUnderRoot -Candidate $path -Root $root -Label 'Deletion journal path'
    }
    return [pscustomobject]@{
        Root = $root
        Container = $container
        Group = $group
        Receipt = $receipt
    }
}

function Assert-NoForeignDeletionState {
    param(
        [Parameter(Mandatory)] [string] $ArchiveRoot,
        [Parameter(Mandatory)] $Job
    )

    $paths = Get-DeletionPaths -ArchiveRoot $ArchiveRoot -Job $Job
    if (-not (Test-Path -LiteralPath $paths.Root -PathType Container)) {
        return
    }
    $allowed = [Collections.Generic.HashSet[string]]::new($script:PathComparer)
    [void]$allowed.Add([IO.Path]::GetFullPath($paths.Container))
    [void]$allowed.Add([IO.Path]::GetFullPath($paths.Receipt))
    foreach ($entry in @(Get-ChildItem -LiteralPath $paths.Root -Force)) {
        $full = [IO.Path]::GetFullPath($entry.FullName)
        Assert-PathUnderRoot -Candidate $full -Root $paths.Root -Label 'Deletion state entry'
        if (-not $allowed.Contains($full)) {
            throw "Another recovery/deletion artifact blocks a fresh Archive run: $full"
        }
    }
}

function Restore-PriorDeletionJournal {
    param(
        [Parameter(Mandatory)] $Job,
        [Parameter(Mandatory)] [string] $ArchiveRoot,
        [switch] $InspectOnly,
        [switch] $RecoveryAuthorized,
        [bool] $RemoteAssetVerified = $false,
        [bool] $RemoteManifestVerified = $false,
        [Parameter(DontShow)] [scriptblock] $RestoreCandidateProbe,
        [Parameter(DontShow)] [scriptblock] $CandidateScanProbe,
        [Parameter(DontShow)] [switch] $AllowInvalidQuarantineArchiveFallback
    )

    $paths = Get-DeletionPaths -ArchiveRoot $ArchiveRoot -Job $Job
    if (-not (Test-Path -LiteralPath $paths.Receipt -PathType Leaf)) {
        if (Test-Path -LiteralPath $paths.Container) {
            throw "Quarantine exists without its durable journal: $($paths.Container)"
        }
        return $null
    }
    $receipt = Get-Content -LiteralPath $paths.Receipt -Raw | ConvertFrom-Json
    $stagingPaths = Get-StagingPaths -ArchiveRoot $ArchiveRoot -Job $Job
    $source = [IO.Path]::GetFullPath((Join-Path $Job.ParentRoot $Job.RelativeRoot))
    $expectedReceiptSource = [IO.Path]::GetFullPath([string]$receipt.source_path)
    $expectedReceiptAsset = [IO.Path]::GetFullPath([string]$receipt.asset_path)
    if (
        [int]$receipt.schema_version -ne 1 -or
        [string]$receipt.source_locator -cne $Job.SourceLocator -or
        [string]$receipt.asset -cne $Job.AssetName -or
        -not $expectedReceiptSource.Equals($source, $script:PathComparison) -or
        -not $expectedReceiptAsset.Equals($stagingPaths.Asset, $script:PathComparison) -or
        [string]$receipt.asset_sha256 -notmatch '^[0-9a-f]{64}$' -or
        [string]$receipt.source_fingerprint_sha256 -notmatch '^[0-9a-f]{64}$' -or
        [string]$receipt.remote_commit -notmatch '^[0-9a-f]{40,64}$' -or
        [string]$receipt.phase -notin @('prepared', 'quarantined', 'sources_deleted')
    ) {
        throw "Prior deletion journal conflicts with the exact approved request: $($paths.Receipt)"
    }
    if (
        -not (Test-Path -LiteralPath $stagingPaths.Asset -PathType Leaf) -or
        -not (Test-Path -LiteralPath $stagingPaths.Metadata -PathType Leaf)
    ) {
        throw "Prior deletion journal requires its verified staged archive and metadata: $($paths.Receipt)"
    }
    $metadata = Get-Content -LiteralPath $stagingPaths.Metadata -Raw | ConvertFrom-Json
    if (
        [string]$metadata.asset_name -cne $Job.AssetName -or
        [string]$metadata.source_locator -cne $Job.SourceLocator -or
        [string]$metadata.asset_sha256 -cne [string]$receipt.asset_sha256 -or
        [string]$metadata.source_fingerprint_sha256 -cne
            [string]$receipt.source_fingerprint_sha256 -or
        (Get-Sha256File -LiteralPath $stagingPaths.Asset) -cne
            [string]$metadata.asset_sha256
    ) {
        throw 'Prior deletion journal does not match the staged archive metadata'
    }

    $staging = [pscustomobject]@{
        Paths = $stagingPaths
        Metadata = $metadata
        Resumed = $true
    }
    $sourceExists = Test-Path -LiteralPath $source -PathType Container
    $groupExists = Test-Path -LiteralPath $paths.Group -PathType Container
    if ([string]$receipt.phase -ceq 'sources_deleted') {
        if ($sourceExists -or $groupExists) {
            throw 'Completed-deletion journal conflicts with an existing source/quarantine root'
        }
        if (
            (Test-Path -LiteralPath $paths.Container -PathType Container) -and
            (Get-ChildItem -LiteralPath $paths.Container -Force)
        ) {
            throw 'Completed-deletion journal has unexpected retained recovery artifacts'
        }
        if (
            -not $InspectOnly -and
            (Test-Path -LiteralPath $paths.Container -PathType Container) -and
            -not (Get-ChildItem -LiteralPath $paths.Container -Force)
        ) {
            [IO.Directory]::Delete($paths.Container)
        }
        return [pscustomobject]@{
            State = 'DeletionCompleted'
            ReceiptPath = $paths.Receipt
            Receipt = $receipt
            Staging = $staging
        }
    }
    if ($sourceExists) {
        $retainedInventory = Get-SourceInventory -Job $Job -DeepHash $true
        if (
            [string]$retainedInventory.SourceFingerprintSha256 -cne
            [string]$metadata.source_fingerprint_sha256
        ) {
            throw 'Prior deletion journal source exists but fails its archived fingerprint'
        }
        if ($InspectOnly) {
            return [pscustomobject]@{
                State = 'SourceRetained'
                ReceiptPath = $paths.Receipt
                Receipt = $receipt
                Staging = $staging
                Inventory = $retainedInventory
                QuarantineGroupPresent = $groupExists
            }
        }
        Remove-RecoveryArtifactsAfterVerifiedRestore `
            -Job $Job `
            -QuarantineRoot $paths.Container
        $postCleanupInventory = Get-SourceInventory -Job $Job -DeepHash $true
        Assert-InventoryEqual `
            -Expected $retainedInventory `
            -Actual $postCleanupInventory `
            -Label 'Post-cleanup retained source'
        Remove-Item -LiteralPath $paths.Receipt -Force
        return [pscustomobject]@{
            State = 'JournalCleared'
            Receipt = $receipt
            Staging = $staging
            Inventory = $postCleanupInventory
        }
    }
    if (-not $sourceExists) {
        if ($InspectOnly) {
            return [pscustomobject]@{
                State = 'NeedsRecovery'
                ReceiptPath = $paths.Receipt
                Receipt = $receipt
                Staging = $staging
                QuarantineGroupPresent = $groupExists
            }
        }
        if (
            -not $RecoveryAuthorized -or
            -not $RemoteAssetVerified -or
            -not $RemoteManifestVerified
        ) {
            throw 'Source recovery requires explicit Recover mode and fresh remote asset/manifest verification'
        }
        $restoreResult = Restore-SourceRootFromArchive `
            -Job $Job `
            -StagedAsset $stagingPaths.Asset `
            -Metadata $metadata `
            -QuarantineRoot $paths.Container `
            -QuarantineGroup $paths.Group `
            -RestoreCandidateProbe $RestoreCandidateProbe `
            -CandidateScanProbe $CandidateScanProbe `
            -AllowInvalidQuarantineArchiveFallback:$AllowInvalidQuarantineArchiveFallback
        $restoredInventory = Get-SourceInventory -Job $Job -DeepHash $true
        if (
            [string]$restoredInventory.SourceFingerprintSha256 -cne
            [string]$metadata.source_fingerprint_sha256
        ) {
            throw 'Recovered source changed before deletion-journal finalization'
        }
        if (Test-Path -LiteralPath $paths.Container) {
            throw "Verified source was restored but recovery artifacts remain; journal retained: $($paths.Container)"
        }
        Remove-Item -LiteralPath $paths.Receipt -Force
        return [pscustomobject]@{
            State = 'SourcesRestored'
            Receipt = $receipt
            Staging = $staging
            Inventory = $restoredInventory
            RestoreMethod = $restoreResult.Method
        }
    }
    throw 'Prior deletion journal is in an unsupported state'
}

function Expand-ArchiveForRollback {
    param(
        [Parameter(Mandatory)] [string] $StagedAsset,
        [Parameter(Mandatory)] [string] $RestoreRoot,
        [Parameter(Mandatory)] [string] $QuarantineRoot,
        [Parameter(Mandatory)] [int64] $LinkCount
    )

    Assert-PathUnderRoot `
        -Candidate ([IO.Path]::GetFullPath($RestoreRoot)) `
        -Root ([IO.Path]::GetFullPath($QuarantineRoot)) `
        -Label 'Rollback extraction root'
    if ($LinkCount -eq 0) {
        $tar = @(Get-Command tar -CommandType Application -ErrorAction Stop)[0]
        $output = @(& $tar.Source -xf $StagedAsset -C $RestoreRoot 2>&1)
        if ($LASTEXITCODE -ne 0) {
            throw "Rollback extraction failed: $($output -join [Environment]::NewLine)"
        }
        return
    }

    # Windows libarchive cannot create WSL LX symlink reparse points. Expand
    # the already member-verified archive with WSL GNU tar so those exact
    # relative links are recreated, then the caller deep-fingerprints them.
    $rawTar = [IO.Path]::GetFullPath((Join-Path $QuarantineRoot (
        '.rollback.' + [guid]::NewGuid().ToString('N') + '.tar')))
    Assert-PathUnderRoot `
        -Candidate $rawTar `
        -Root ([IO.Path]::GetFullPath($QuarantineRoot)) `
        -Label 'Rollback intermediate tar'
    try {
        $zstd = @(Get-Command zstd -CommandType Application -ErrorAction Stop)[0]
        $output = @(& $zstd.Source -d --quiet -f -o $rawTar -- $StagedAsset 2>&1)
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $rawTar -PathType Leaf)) {
            throw "Rollback decompression failed: $($output -join [Environment]::NewLine)"
        }
        $wsl = @(Get-Command wsl.exe -CommandType Application -ErrorAction Stop)[0]
        $wslTar = ConvertTo-WslAbsolutePath -WindowsPath $rawTar
        $wslRestore = ConvertTo-WslAbsolutePath -WindowsPath $RestoreRoot
        $output = @(& $wsl.Source --exec tar --extract --no-same-owner `
            --no-same-permissions --file $wslTar --directory $wslRestore 2>&1)
        if ($LASTEXITCODE -ne 0) {
            throw "WSL rollback extraction failed: $($output -join [Environment]::NewLine)"
        }
    } finally {
        if (Test-Path -LiteralPath $rawTar) {
            [IO.File]::Delete($rawTar)
        }
    }
}

function Restore-SourceRootFromArchive {
    param(
        [Parameter(Mandatory)] $Job,
        [Parameter(Mandatory)] [string] $StagedAsset,
        [Parameter(Mandatory)] $Metadata,
        [Parameter(Mandatory)] [string] $QuarantineRoot,
        [Parameter(Mandatory)] [string] $QuarantineGroup,
        [Parameter(DontShow)] [scriptblock] $RestoreCandidateProbe,
        [Parameter(DontShow)] [scriptblock] $CandidateScanProbe,
        [Parameter(DontShow)] [switch] $AllowInvalidQuarantineArchiveFallback
    )

    $expectedEntries = @(ConvertFrom-DurableEntryRecords -Records @($Metadata.entries))
    $actualAssetSha = Get-Sha256File -LiteralPath $StagedAsset
    if ($actualAssetSha -cne [string]$Metadata.asset_sha256) {
        throw 'Rollback archive SHA-256 no longer matches durable metadata'
    }
    Get-VerifiedZstdTarInventory `
        -LiteralPath $StagedAsset `
        -ExpectedEntries $expectedEntries `
        -TopRoot $Job.RelativeRoot | Out-Null

    $source = [IO.Path]::GetFullPath((Join-Path $Job.ParentRoot $Job.RelativeRoot))
    Assert-PathUnderRoot -Candidate $source -Root $Job.ParentRoot -Label 'Rollback source target'
    if (Test-Path -LiteralPath $source) {
        throw "Rollback source target already exists: $source"
    }

    $quarantineValidationFailure = $null
    $quarantineGroupEntries = @(
        if (Test-Path -LiteralPath $QuarantineRoot -PathType Container) {
            Get-ChildItem -LiteralPath $QuarantineRoot -Force | Where-Object {
                $_.Name -ceq $Job.RelativeRoot
            }
        }
    )
    if ($quarantineGroupEntries.Count -gt 1) {
        throw 'Multiple exact quarantine-group entries conflict; recovery is blocked'
    }
    if (
        $quarantineGroupEntries.Count -eq 1 -or
        (Test-Path -LiteralPath $QuarantineGroup)
    ) {
        $quarantineGroupItem = if ($quarantineGroupEntries.Count -eq 1) {
            $quarantineGroupEntries[0]
        } else {
            Get-Item -LiteralPath $QuarantineGroup -Force
        }
        $quarantineGroupFull = [IO.Path]::GetFullPath($quarantineGroupItem.FullName)
        if (
            -not $quarantineGroupFull.Equals(
                [IO.Path]::GetFullPath($QuarantineGroup), $script:PathComparison) -or
            -not $quarantineGroupItem.PSIsContainer -or
            ($quarantineGroupItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
        ) {
            throw "Exact quarantine group is not an ordinary approved directory: $quarantineGroupFull"
        }
        try {
            $quarantineJob = [pscustomobject]@{
                ParentRoot = $QuarantineRoot
                RelativeRoot = $Job.RelativeRoot
                AllowedReparseHashes = $Job.AllowedReparseHashes
            }
            $quarantineInventory = Get-SourceInventory -Job $quarantineJob -DeepHash $true
            if (-not (Test-InventoryMatchesDurableMetadata `
                -Inventory $quarantineInventory `
                -Metadata $Metadata
            )) {
                throw 'Exact quarantine group does not match durable archived source metadata'
            }
            Move-DirectoryAtomically `
                -Source $QuarantineGroup `
                -Destination $source `
                -Label 'Verified quarantine rollback'
            $finalInventory = Get-SourceInventory -Job $Job -DeepHash $true
            if (-not (Test-InventoryMatchesDurableMetadata `
                -Inventory $finalInventory `
                -Metadata $Metadata
            )) {
                throw 'Quarantine rollback source failed its final durable-metadata check'
            }
            Remove-RecoveryArtifactsAfterVerifiedRestore `
                -Job $Job `
                -QuarantineRoot $QuarantineRoot
            return [pscustomobject]@{
                Method = 'quarantine'
                Inventory = $finalInventory
            }
        } catch {
            $quarantineValidationFailure = $_.Exception.Message
            if (Test-Path -LiteralPath $source -PathType Container) {
                throw "Quarantine rollback moved the exact root but did not finish final verification; journal and source are retained: $quarantineValidationFailure"
            }
            if (-not $AllowInvalidQuarantineArchiveFallback) {
                throw "Exact quarantine group failed deep validation and is retained; candidate fallback is forbidden: $quarantineValidationFailure"
            }
        }
    }

    if ($null -ne $CandidateScanProbe) {
        $null = & $CandidateScanProbe $QuarantineRoot
    }
    $candidateState = Get-PreservedRecoveryCandidateState `
        -Job $Job `
        -Metadata $Metadata `
        -QuarantineRoot $QuarantineRoot `
        -QuarantineGroup $QuarantineGroup
    if ($candidateState.Candidates.Count -ne 0) {
        if ($candidateState.Conflicts.Count -ne 0) {
            throw "Indeterminate or hostile preserved recovery candidate blocks mutation and is retained: $($candidateState.Conflicts[0].Failure)"
        }
        if ($candidateState.Valid.Count -gt 1) {
            throw "Multiple valid preserved recovery candidates conflict and are retained: total=$($candidateState.Candidates.Count), valid=$($candidateState.Valid.Count)"
        }
        if ($candidateState.Valid.Count -eq 0) {
            $failure = [string]$candidateState.Invalid[0].Failure
            throw "Preserved recovery candidate failed deep validation and is retained for a later retry: $failure"
        }
        $candidate = $candidateState.Valid[0]
        try {
            $candidateJob = [pscustomobject]@{
                ParentRoot = $candidate.Root
                RelativeRoot = $Job.RelativeRoot
                AllowedReparseHashes = $Job.AllowedReparseHashes
            }
            $candidateRecheck = Get-SourceInventory -Job $candidateJob -DeepHash $true
            Assert-InventoryEqual `
                -Expected $candidate.Inventory `
                -Actual $candidateRecheck `
                -Label 'Immediate preserved-candidate recovery gate'
            Move-DirectoryAtomically `
                -Source $candidate.SourceRoot `
                -Destination $source `
                -Label 'Verified preserved-candidate recovery'
            $finalInventory = Get-SourceInventory -Job $Job -DeepHash $true
            if (
                [string]$finalInventory.SourceFingerprintSha256 -cne
                [string]$Metadata.source_fingerprint_sha256
            ) {
                throw 'Preserved-candidate source failed its final fingerprint check'
            }
            Remove-RecoveryArtifactsAfterVerifiedRestore `
                -Job $Job `
                -QuarantineRoot $QuarantineRoot
            return [pscustomobject]@{
                Method = 'preserved_candidate'
                Inventory = $finalInventory
            }
        } catch {
            throw "Preserved-candidate recovery did not complete; source, candidate container, and journal are retained: $($_.Exception.Message)"
        }
    }

    [IO.Directory]::CreateDirectory($QuarantineRoot) | Out-Null
    $restoreRoot = [IO.Path]::GetFullPath((Join-Path $QuarantineRoot (
        '.restore.' + [guid]::NewGuid().ToString('N'))))
    Assert-PathUnderRoot -Candidate $restoreRoot -Root $QuarantineRoot -Label 'Rollback extraction root'
    [IO.Directory]::CreateDirectory($restoreRoot) | Out-Null
    try {
        Expand-ArchiveForRollback `
            -StagedAsset $StagedAsset `
            -RestoreRoot $restoreRoot `
            -QuarantineRoot $QuarantineRoot `
            -LinkCount ([int64]$Metadata.source_links)
        $restored = [IO.Path]::GetFullPath((Join-Path $restoreRoot $Job.RelativeRoot))
        Assert-PathUnderRoot -Candidate $restored -Root $restoreRoot -Label 'Restored archive root'
        if (-not (Test-Path -LiteralPath $restored -PathType Container)) {
            throw 'Rollback archive did not recreate its exact approved root'
        }
        $unexpected = @(
            Get-ChildItem -LiteralPath $restoreRoot -Force |
                Where-Object Name -cne $Job.RelativeRoot)
        if ($unexpected.Count -ne 0) {
            throw "Rollback extraction created an unexpected root: $($unexpected[0].FullName)"
        }
        if ($null -ne $RestoreCandidateProbe) {
            $null = & $RestoreCandidateProbe $restored $restoreRoot
        }
        $restoreJob = [pscustomobject]@{
            ParentRoot = $restoreRoot
            RelativeRoot = $Job.RelativeRoot
            AllowedReparseHashes = $Job.AllowedReparseHashes
        }
        $restoredInventory = Get-SourceInventory -Job $restoreJob -DeepHash $true
        if (
            [string]$restoredInventory.SourceFingerprintSha256 -cne
            [string]$Metadata.source_fingerprint_sha256
        ) {
            throw 'Rollback extraction fingerprint does not match archived source'
        }
        Move-DirectoryAtomically `
            -Source $restored `
            -Destination $source `
            -Label 'Verified archive recovery'
        $finalInventory = Get-SourceInventory -Job $Job -DeepHash $true
        if (
            [string]$finalInventory.SourceFingerprintSha256 -cne
            [string]$Metadata.source_fingerprint_sha256
        ) {
            throw 'Rollback source failed its final fingerprint check'
        }
        Remove-RecoveryArtifactsAfterVerifiedRestore `
            -Job $Job `
            -QuarantineRoot $QuarantineRoot
        return [pscustomobject]@{
            Method = 'archive'
            Inventory = $finalInventory
        }
    } catch {
        $quarantineNote = if ([string]::IsNullOrWhiteSpace($quarantineValidationFailure)) {
            ''
        } else {
            " Quarantine copy was also retained: $quarantineValidationFailure."
        }
        throw "Archive recovery failed; preserve journal, quarantine, and candidate '$restoreRoot'.$quarantineNote $($_.Exception.Message)"
    }
}

function Remove-SourceRootSafely {
    param(
        [Parameter(Mandatory)] $Job,
        [Parameter(Mandatory)] $Inventory,
        [Parameter(Mandatory)] [string] $ArchiveRoot,
        [Parameter(Mandatory)] [string] $StagedAsset,
        [Parameter(Mandatory)] $Metadata,
        [Parameter(Mandatory)] [string] $RemoteCommit,
        [Parameter(Mandatory)] [bool] $DeletionAuthorized,
        [Parameter(Mandatory)] [bool] $RemoteAssetVerified,
        [Parameter(Mandatory)] [bool] $RemoteManifestVerified,
        [Parameter(DontShow)] [switch] $InjectMoveFailure,
        [Parameter(DontShow)] [switch] $InjectPurgeFailureAfterDamage,
        [Parameter(DontShow)] [scriptblock] $PostMoveProbe
    )

    if (-not $DeletionAuthorized) {
        throw 'Source deletion requires explicit -DeleteSources authorization'
    }
    if (-not $RemoteAssetVerified -or -not $RemoteManifestVerified) {
        throw 'Source deletion requires verified remote asset and remote manifest gates'
    }
    if ($RemoteCommit -notmatch '^[0-9a-f]{40,64}$') {
        throw 'Deletion gate lacks a valid remote manifest commit'
    }
    $approvedAgain = Get-ApprovedJob `
        -Key $Job.Key `
        -ResolvedCaseParent $Job.ParentRoot `
        -ResolvedSnapshotParent $Job.ParentRoot
    if (
        [string]$approvedAgain.Category -cne [string]$Job.Category -or
        [string]$approvedAgain.AssetName -cne [string]$Job.AssetName -or
        [string]$approvedAgain.SourceLocator -cne [string]$Job.SourceLocator -or
        [string]$approvedAgain.RelativeRoot -cne [string]$Job.RelativeRoot -or
        -not ([string]$approvedAgain.ParentRoot).Equals(
            [string]$Job.ParentRoot, $script:PathComparison)
    ) {
        throw 'Deletion target no longer resolves to the fixed allowlist entry'
    }
    $source = [IO.Path]::GetFullPath((Join-Path $Job.ParentRoot $Job.RelativeRoot))
    $resolvedSource = Resolve-ExistingDirectory -LiteralPath $source -Label 'Deletion source root'
    Assert-PathUnderRoot -Candidate $resolvedSource -Root $Job.ParentRoot -Label 'Deletion source root'
    if (-not $resolvedSource.Equals($source, $script:PathComparison)) {
        throw 'Deletion source resolved to a different path'
    }
    $assetSha = Get-Sha256File -LiteralPath $StagedAsset
    if ($assetSha -cne [string]$Metadata.asset_sha256) {
        throw 'Staged archive changed after remote verification'
    }
    Get-VerifiedZstdTarInventory `
        -LiteralPath $StagedAsset `
        -ExpectedEntries $Inventory.Entries `
        -TopRoot $Job.RelativeRoot | Out-Null
    $fresh = Get-SourceInventory -Job $Job -DeepHash $true
    Assert-InventoryEqual -Expected $Inventory -Actual $fresh -Label 'Immediate deletion-gate source'

    $paths = Get-DeletionPaths -ArchiveRoot $ArchiveRoot -Job $Job
    [IO.Directory]::CreateDirectory($paths.Root) | Out-Null
    if (
        (Test-Path -LiteralPath $paths.Container) -or
        (Test-Path -LiteralPath $paths.Receipt)
    ) {
        throw 'A prior deletion journal must be recovered before a new deletion'
    }
    $receipt = [pscustomobject][ordered]@{
        schema_version = 1
        source_locator = $Job.SourceLocator
        source_path = $source
        asset = $Job.AssetName
        asset_path = $StagedAsset
        asset_sha256 = [string]$Metadata.asset_sha256
        source_fingerprint_sha256 = [string]$Metadata.source_fingerprint_sha256
        remote_commit = $RemoteCommit
        phase = 'prepared'
    }
    Write-DurableUtf8File `
        -LiteralPath $paths.Receipt `
        -Content (($receipt | ConvertTo-Json -Depth 5) + "`n") `
        -ApprovedRoot $ArchiveRoot

    try {
        if ($InjectMoveFailure) {
            throw 'Injected failure before exact-root quarantine move'
        }
        [IO.Directory]::CreateDirectory($paths.Container) | Out-Null
        Move-DirectoryAtomically `
            -Source $source `
            -Destination $paths.Group `
            -Label 'Exact source quarantine'
        $receipt.phase = 'quarantined'
        Write-DurableUtf8File `
            -LiteralPath $paths.Receipt `
            -Content (($receipt | ConvertTo-Json -Depth 5) + "`n") `
            -ApprovedRoot $ArchiveRoot `
            -Replace
        if ($null -ne $PostMoveProbe) {
            $null = & $PostMoveProbe $paths.Group
        }
        $quarantineJob = [pscustomobject]@{
            ParentRoot = $paths.Container
            RelativeRoot = $Job.RelativeRoot
            AllowedReparseHashes = $Job.AllowedReparseHashes
        }
        $quarantinedInventory = Get-SourceInventory -Job $quarantineJob -DeepHash $true
        Assert-InventoryEqual `
            -Expected $Inventory `
            -Actual $quarantinedInventory `
            -Label 'Post-move quarantine source'
    } catch {
        $moveFailure = $_
        try {
            $sourcePresent = Test-Path -LiteralPath $source -PathType Container
            $groupPresent = Test-Path -LiteralPath $paths.Group -PathType Container
            if ($sourcePresent -and $groupPresent) {
                throw 'Both source and quarantine roots exist after the failed atomic move'
            }
            if (-not $sourcePresent) {
                Restore-SourceRootFromArchive `
                    -Job $Job `
                    -StagedAsset $StagedAsset `
                    -Metadata $Metadata `
                    -QuarantineRoot $paths.Container `
                    -QuarantineGroup $paths.Group `
                    -AllowInvalidQuarantineArchiveFallback | Out-Null
            }
            $retainedInventory = Get-SourceInventory -Job $Job -DeepHash $true
            Assert-InventoryEqual `
                -Expected $Inventory `
                -Actual $retainedInventory `
                -Label 'Quarantine-failure restored source'
            Remove-RecoveryArtifactsAfterVerifiedRestore `
                -Job $Job `
                -QuarantineRoot $paths.Container
            if (Test-Path -LiteralPath $paths.Receipt) {
                Remove-Item -LiteralPath $paths.Receipt -Force
            }
        } catch {
            throw "Quarantine move validation and automatic rollback both failed; preserve '$($paths.Receipt)': move=$($moveFailure.Exception.Message); rollback=$($_.Exception.Message)"
        }
        throw "Exact source root was not safely quarantined; the source is retained/restored from the verified archive: $($moveFailure.Exception.Message)"
    }

    try {
        if ($InjectPurgeFailureAfterDamage) {
            $damage = Get-ChildItem -LiteralPath $paths.Group -File -Recurse -Force |
                Select-Object -First 1
            if ($null -ne $damage) {
                Remove-Item -LiteralPath $damage.FullName -Force
            }
            throw 'Injected purge failure after partial quarantine damage'
        }
        Remove-Item -LiteralPath $paths.Group -Recurse -Force
        Remove-Item -LiteralPath $paths.Container -Force
        $receipt.phase = 'sources_deleted'
        Write-DurableUtf8File `
            -LiteralPath $paths.Receipt `
            -Content (($receipt | ConvertTo-Json -Depth 5) + "`n") `
            -ApprovedRoot $ArchiveRoot `
            -Replace
    } catch {
        $purgeFailure = $_
        try {
            if (Test-Path -LiteralPath $source) {
                throw 'Source unexpectedly exists before purge rollback'
            }
            Restore-SourceRootFromArchive `
                -Job $Job `
                -StagedAsset $StagedAsset `
                -Metadata $Metadata `
                -QuarantineRoot $paths.Container `
                -QuarantineGroup $paths.Group `
                -AllowInvalidQuarantineArchiveFallback | Out-Null
            $restoredInventory = Get-SourceInventory -Job $Job -DeepHash $true
            Assert-InventoryEqual `
                -Expected $Inventory `
                -Actual $restoredInventory `
                -Label 'Purge-failure restored source'
            Remove-Item -LiteralPath $paths.Receipt -Force
        } catch {
            throw "Quarantine purge and automatic rollback both failed; preserve '$($paths.Receipt)': purge=$($purgeFailure.Exception.Message); rollback=$($_.Exception.Message)"
        }
        throw "Quarantine purge failed; the exact source root was restored from the verified archive: $($purgeFailure.Exception.Message)"
    }
    if (Test-Path -LiteralPath $source) {
        throw 'Source target still exists after completed exact-root deletion'
    }
    return [pscustomobject]@{
        ReceiptPath = $paths.Receipt
        Receipt = $receipt
    }
}

function Remove-CompletedStaging {
    param(
        [Parameter(Mandatory)] [string] $ArchiveRoot,
        [Parameter(Mandatory)] $Staging,
        [Parameter(Mandatory)] [string] $DeletionReceipt
    )

    foreach ($path in @(
        $Staging.Paths.Asset,
        $Staging.Paths.Metadata,
        $Staging.Paths.Partial,
        $Staging.Paths.List,
        $DeletionReceipt
    )) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $full = [IO.Path]::GetFullPath($path)
            Assert-PathUnderRoot -Candidate $full -Root $ArchiveRoot -Label 'Completed staging cleanup'
            [IO.File]::Delete($full)
        }
    }
    foreach ($directory in @(
        [IO.Path]::GetDirectoryName($DeletionReceipt),
        $Staging.Paths.Root
    )) {
        if (
            (Test-Path -LiteralPath $directory -PathType Container) -and
            -not (Get-ChildItem -LiteralPath $directory -Force)
        ) {
            Remove-Item -LiteralPath $directory -Force
        }
    }
}

function Assert-ArchiveRepository {
    param(
        [Parameter(Mandatory)] [string] $ArchiveRoot,
        [Parameter(Mandatory)] [string] $BranchName
    )

    $gitMetadata = [IO.Path]::GetFullPath((Join-Path $ArchiveRoot '.git'))
    if (-not (Test-Path -LiteralPath $gitMetadata -PathType Container)) {
        throw 'ArchiveRepoPath is not a normal Git working tree'
    }
    $top = (Invoke-ArchiveGit -Arguments @('rev-parse', '--show-toplevel')).Text.Trim()
    if (-not ([IO.Path]::GetFullPath($top)).Equals($ArchiveRoot, $script:PathComparison)) {
        throw 'ArchiveRepoPath must be the Git working-tree root'
    }
    $currentBranch = (Invoke-ArchiveGit -Arguments @('branch', '--show-current')).Text.Trim()
    if ($currentBranch -cne $BranchName) {
        throw "Archive repository must be on '$BranchName'; found '$currentBranch'"
    }
    foreach ($relative in @(
        "$($script:StagingRelativePath)/probe",
        "$($script:DeletionRelativePath)/probe"
    )) {
        $ignored = Invoke-ArchiveGit -Arguments @(
            'check-ignore', '--quiet', '--', $relative) -AllowFailure
        if ($ignored.ExitCode -ne 0) {
            throw "Archive repository must ignore staging path '$relative'"
        }
    }
    $manifestIgnored = Invoke-ArchiveGit -Arguments @(
        'check-ignore', '--quiet', '--', $script:ManifestRelativePath) -AllowFailure
    if ($manifestIgnored.ExitCode -eq 0) {
        throw 'Legacy manifest must not be ignored by Git'
    }
    return $gitMetadata
}

function Assert-RemoteRepositoryWritableAndPrivate {
    param(
        [Parameter(Mandatory)] [string] $RepositorySlug,
        [Parameter(Mandatory)] [hashtable] $Headers
    )

    $repository = Invoke-GitHubGet `
        -Uri "https://api.github.com/repos/$RepositorySlug" `
        -Headers $Headers
    if (
        -not ([string]$repository.full_name).Equals(
            $script:ExpectedRepositorySlug,
            [StringComparison]::OrdinalIgnoreCase) -or
        -not [bool]$repository.private -or
        -not [bool]$repository.permissions.push
    ) {
        throw 'Archive repository must be the approved private, writable repository'
    }
}

function Confirm-PriorDeletionRemoteProof {
    param(
        [Parameter(Mandatory)] $Job,
        [Parameter(Mandatory)] $PriorDeletion,
        [Parameter(Mandatory)] [string] $RepositorySlug,
        [Parameter(Mandatory)] [string] $ReleaseTag,
        [Parameter(Mandatory)] [string] $BranchName,
        [Parameter(Mandatory)] [hashtable] $Headers
    )

    Assert-RemoteRepositoryWritableAndPrivate `
        -RepositorySlug $RepositorySlug `
        -Headers $Headers
    $status = @(Get-ArchiveGitStatusRecords)
    if ($status.Count -ne 0) {
        throw 'Recovery requires a clean archive manifest working tree'
    }
    $release = Get-Release `
        -RepositorySlug $RepositorySlug `
        -Tag $ReleaseTag `
        -Headers $Headers
    $metadata = $PriorDeletion.Staging.Metadata
    $remoteAsset = Get-VerifiedRemoteAsset `
        -RepositorySlug $RepositorySlug `
        -Release $release `
        -AssetName $Job.AssetName `
        -ExpectedBytes ([int64]$metadata.asset_bytes) `
        -ExpectedSha256 ([string]$metadata.asset_sha256) `
        -Headers $Headers
    if ($null -eq $remoteAsset) {
        throw 'Recovery journal lacks its exact verified remote asset'
    }
    $expectedRow = Get-ExpectedManifestRow `
        -Job $Job `
        -Metadata $metadata `
        -RemoteAsset $remoteAsset
    $remoteHead = Get-RemoteBranchHead `
        -RepositorySlug $RepositorySlug `
        -BranchName $BranchName `
        -Headers $Headers
    if ($remoteHead -cne [string]$PriorDeletion.Receipt.remote_commit) {
        throw "Remote $BranchName no longer equals the deletion journal's verified manifest commit"
    }
    $localManifestBytes = Invoke-ArchiveGitBytes -Arguments @(
        'cat-file', 'blob', "$remoteHead`:$($script:ManifestRelativePath)")
    $remoteManifestBytes = Get-RemoteManifestBytes `
        -RepositorySlug $RepositorySlug `
        -BranchName $BranchName `
        -Headers $Headers
    Assert-ByteArraysEqual `
        -Expected $localManifestBytes `
        -Actual $remoteManifestBytes `
        -Label "Recovery remote $BranchName manifest"
    $remoteManifestText = [Text.UTF8Encoding]::new($false, $true).GetString(
        $remoteManifestBytes)
    Assert-ManifestRowState `
        -CsvText $remoteManifestText `
        -Expected $expectedRow `
        -Label "Recovery remote $BranchName manifest" | Out-Null

    $releaseRecheck = Get-Release `
        -RepositorySlug $RepositorySlug `
        -Tag $ReleaseTag `
        -Headers $Headers
    $assetRecheck = Get-VerifiedRemoteAsset `
        -RepositorySlug $RepositorySlug `
        -Release $releaseRecheck `
        -AssetName $Job.AssetName `
        -ExpectedBytes ([int64]$metadata.asset_bytes) `
        -ExpectedSha256 ([string]$metadata.asset_sha256) `
        -Headers $Headers
    if (
        $null -eq $assetRecheck -or
        [string]$assetRecheck.id -cne [string]$remoteAsset.id
    ) {
        throw 'Remote asset changed during recovery authorization'
    }
    return [pscustomobject]@{
        Commit = $remoteHead
        RemoteAsset = $assetRecheck
        ExpectedManifestRow = $expectedRow
    }
}

if ($LoadFunctionsOnly) {
    return
}

try {
    if ([string]::IsNullOrWhiteSpace($AssetKey)) {
        throw 'AssetKey is required'
    }
    if ([string]::IsNullOrWhiteSpace($ArchiveRepoPath)) {
        throw 'ArchiveRepoPath is required'
    }
    Assert-SafeLeafName -Value $ReleaseTag -Label 'ReleaseTag' -Pattern '^[A-Za-z0-9][A-Za-z0-9._-]*$'
    Assert-SafeLeafName -Value $Branch -Label 'Branch' -Pattern '^[A-Za-z0-9][A-Za-z0-9._-]*$'
    if ($ReleaseTag -cne $script:ExpectedReleaseTag) {
        throw "ReleaseTag is fixed for this approved archive set: $($script:ExpectedReleaseTag)"
    }
    if ($Branch -cne 'main') {
        throw 'The verified manifest destination is fixed to remote main'
    }
    if ($DeleteSources -and $Mode -cne 'Archive') {
        throw '-DeleteSources is valid only in Archive mode'
    }

    $resolvedCaseParent = Resolve-ExistingDirectory -LiteralPath $CaseParent -Label 'CaseParent'
    $resolvedSnapshotParent = Resolve-ExistingDirectory -LiteralPath $SnapshotParent -Label 'SnapshotParent'
    if (-not $resolvedCaseParent.Equals(
        [IO.Path]::GetFullPath($script:ExpectedCaseParent),
        $script:PathComparison
    )) {
        throw "CaseParent is not the exact approved source parent: $resolvedCaseParent"
    }
    if (-not $resolvedSnapshotParent.Equals(
        [IO.Path]::GetFullPath($script:ExpectedSnapshotParent),
        $script:PathComparison
    )) {
        throw "SnapshotParent is not the exact approved source parent: $resolvedSnapshotParent"
    }
    Assert-SeparateTrees -First $resolvedCaseParent -Second $resolvedSnapshotParent -Label 'Source parents'
    Assert-ApprovedParentLayout `
        -ResolvedCaseParent $resolvedCaseParent `
        -ResolvedSnapshotParent $resolvedSnapshotParent
    $job = Get-ApprovedJob `
        -Key $AssetKey `
        -ResolvedCaseParent $resolvedCaseParent `
        -ResolvedSnapshotParent $resolvedSnapshotParent
    $sourcePath = [IO.Path]::GetFullPath((Join-Path $job.ParentRoot $job.RelativeRoot))
    Assert-PathUnderRoot -Candidate $sourcePath -Root $job.ParentRoot -Label 'Approved source'

    $script:ArchiveRoot = Resolve-ExistingDirectory `
        -LiteralPath $ArchiveRepoPath `
        -Label 'ArchiveRepoPath'
    Assert-SeparateTrees -First $script:ArchiveRoot -Second $resolvedCaseParent -Label 'Archive repo/case parent'
    Assert-SeparateTrees -First $script:ArchiveRoot -Second $resolvedSnapshotParent -Label 'Archive repo/snapshot parent'
    $gitMetadata = Assert-ArchiveRepository -ArchiveRoot $script:ArchiveRoot -BranchName $Branch
    $identity = Get-GitHubRepositoryIdentity

    $priorDeletion = $null
    if ($Mode -in @('Archive', 'Recover')) {
        $script:ArchiveRepoLock = Start-ArchiveRepoLock -GitMetadataPath $gitMetadata
        Assert-ArchiveRepoLockHeld
        if ($Mode -ceq 'Archive') {
            Assert-NoForeignDeletionState `
                -ArchiveRoot $script:ArchiveRoot `
                -Job $job
        }
        $priorInspection = Restore-PriorDeletionJournal `
            -Job $job `
            -ArchiveRoot $script:ArchiveRoot `
            -InspectOnly
        if ($Mode -ceq 'Recover') {
            if ($null -eq $priorInspection) {
                throw 'Recover mode requires an exact durable deletion journal'
            }
            if ($priorInspection.State -ceq 'DeletionCompleted') {
                throw 'Recover mode refuses a journal durably marked sources_deleted'
            }
            if ($priorInspection.State -notin @('NeedsRecovery', 'SourceRetained')) {
                throw "Recover mode cannot handle journal state '$($priorInspection.State)'"
            }
            $script:StagedAssetGuard = [IO.File]::Open(
                $priorInspection.Staging.Paths.Asset,
                [IO.FileMode]::Open,
                [IO.FileAccess]::Read,
                [IO.FileShare]::Read)
            $headers = Get-GitHubHeaders
            $remoteProof = Confirm-PriorDeletionRemoteProof `
                -Job $job `
                -PriorDeletion $priorInspection `
                -RepositorySlug $identity.Slug `
                -ReleaseTag $ReleaseTag `
                -BranchName $Branch `
                -Headers $headers
            $recovery = Restore-PriorDeletionJournal `
                -Job $job `
                -ArchiveRoot $script:ArchiveRoot `
                -RecoveryAuthorized `
                -RemoteAssetVerified $true `
                -RemoteManifestVerified $true
            if ($recovery.State -notin @('SourcesRestored', 'JournalCleared')) {
                throw "Recover mode ended in unexpected state '$($recovery.State)'"
            }
            Assert-ApprovedParentLayout `
                -ResolvedCaseParent $resolvedCaseParent `
                -ResolvedSnapshotParent $resolvedSnapshotParent
            [pscustomobject][ordered]@{
                status = if ($recovery.State -ceq 'SourcesRestored') {
                    'RECOVERED_VERIFIED_SOURCE_RETAINED'
                } else {
                    'PRIOR_DELETE_SOURCE_VERIFIED_RETAINED'
                }
                source_locator = $job.SourceLocator
                asset = $job.AssetName
                source_fingerprint_sha256 =
                    [string]$recovery.Inventory.SourceFingerprintSha256
                remote_commit = $remoteProof.Commit
                sources_deleted = $false
                staged_asset = $recovery.Staging.Paths.Asset
            } | ConvertTo-Json
            exit 0
        }
        if (
            $null -ne $priorInspection -and
            $priorInspection.State -ceq 'NeedsRecovery'
        ) {
            throw "An incomplete deletion journal requires one explicit recovery-only run: -Mode Recover -AssetKey $($job.Key)"
        }
        if (
            $null -ne $priorInspection -and
            $priorInspection.State -ceq 'SourceRetained'
        ) {
            $retained = Restore-PriorDeletionJournal `
                -Job $job `
                -ArchiveRoot $script:ArchiveRoot
            if ($retained.State -cne 'JournalCleared') {
                throw "Prior retained-source journal ended in unexpected state '$($retained.State)'"
            }
            [pscustomobject][ordered]@{
                status = 'PRIOR_DELETE_SOURCE_VERIFIED_RETAINED'
                source_locator = $job.SourceLocator
                asset = $job.AssetName
                source_fingerprint_sha256 =
                    [string]$retained.Inventory.SourceFingerprintSha256
                sources_deleted = $false
                staged_asset = $retained.Staging.Paths.Asset
            } | ConvertTo-Json
            exit 0
        }
        $priorDeletion = $priorInspection
        if ($null -ne $priorDeletion -and $priorDeletion.State -ceq 'DeletionCompleted') {
            if (-not $DeleteSources) {
                throw 'A completed deletion journal can be finalized only with explicit -DeleteSources'
            }
            $staging = $priorDeletion.Staging
            $headers = Get-GitHubHeaders
            Assert-RemoteRepositoryWritableAndPrivate `
                -RepositorySlug $identity.Slug `
                -Headers $headers
            $release = Get-Release `
                -RepositorySlug $identity.Slug `
                -Tag $ReleaseTag `
                -Headers $headers
            $remoteAsset = Get-VerifiedRemoteAsset `
                -RepositorySlug $identity.Slug `
                -Release $release `
                -AssetName $job.AssetName `
                -ExpectedBytes ([int64]$staging.Metadata.asset_bytes) `
                -ExpectedSha256 ([string]$staging.Metadata.asset_sha256) `
                -Headers $headers
            if ($null -eq $remoteAsset) {
                throw 'Completed deletion journal lacks its exact verified remote asset'
            }
            $manifestRow = Get-ExpectedManifestRow `
                -Job $job `
                -Metadata $staging.Metadata `
                -RemoteAsset $remoteAsset
            $manifestVerification = Add-Push-AndVerifyManifestRow `
                -ExpectedRow $manifestRow `
                -RepositorySlug $identity.Slug `
                -ApprovedPushUrl $identity.PushUrl `
                -BranchName $Branch `
                -Headers $headers
            $release = Get-Release `
                -RepositorySlug $identity.Slug `
                -Tag $ReleaseTag `
                -Headers $headers
            $remoteAsset = Get-VerifiedRemoteAsset `
                -RepositorySlug $identity.Slug `
                -Release $release `
                -AssetName $job.AssetName `
                -ExpectedBytes ([int64]$staging.Metadata.asset_bytes) `
                -ExpectedSha256 ([string]$staging.Metadata.asset_sha256) `
                -Headers $headers
            if ($null -eq $remoteAsset) {
                throw 'Remote asset disappeared during completed-deletion resumption'
            }
            $completedCleanup = Restore-PriorDeletionJournal `
                -Job $job `
                -ArchiveRoot $script:ArchiveRoot
            if ($completedCleanup.State -cne 'DeletionCompleted') {
                throw "Completed-deletion cleanup changed state to '$($completedCleanup.State)'"
            }
            Remove-CompletedStaging `
                -ArchiveRoot $script:ArchiveRoot `
                -Staging $staging `
                -DeletionReceipt $priorDeletion.ReceiptPath
            Assert-ApprovedParentLayout `
                -ResolvedCaseParent $resolvedCaseParent `
                -ResolvedSnapshotParent $resolvedSnapshotParent
            [pscustomobject][ordered]@{
                status = 'ARCHIVED_VERIFIED_AND_DELETED_RESUMED'
                source_locator = $job.SourceLocator
                asset = $job.AssetName
                asset_bytes = [int64]$staging.Metadata.asset_bytes
                sha256 = [string]$staging.Metadata.asset_sha256
                remote_commit = $manifestVerification.Commit
                sources_deleted = $true
                staged_asset = $null
            } | ConvertTo-Json
            exit 0
        }
        if ($null -ne $priorDeletion) {
            throw "Unsupported prior deletion journal state '$($priorDeletion.State)'"
        }
    }

    if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) {
        throw "Approved source is absent and has no recoverable local deletion journal: $sourcePath"
    }
    Assert-CaseTargetLayout -Job $job

    $inventory = Get-SourceInventory -Job $job -DeepHash $true
    $plan = [pscustomobject][ordered]@{
        mode = $Mode
        asset_key = $job.Key
        category = $job.Category
        source = $sourcePath
        asset = $job.AssetName
        source_files = $inventory.SourceFileCount
        source_bytes = $inventory.SourceBytes
        source_entries = $inventory.EntryCount
        source_links = $inventory.LinkCount
        source_fingerprint_sha256 = $inventory.SourceFingerprintSha256
        delete_sources = [bool]$DeleteSources
    }
    if ($Mode -cne 'Archive') {
        if ($Mode -ceq 'Plan') {
            $plan | ConvertTo-Json -Depth 4
        } else {
            "VALID: $($job.SourceLocator) has $($inventory.SourceFileCount) files, $($inventory.SourceBytes) bytes, fingerprint $($inventory.SourceFingerprintSha256)."
        }
        exit 0
    }

    $staging = New-OrResumeStagedArchive `
        -Job $job `
        -Inventory $inventory `
        -ArchiveRoot $script:ArchiveRoot
    $script:StagedAssetGuard = [IO.File]::Open(
        $staging.Paths.Asset,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    $postStage = Get-SourceInventory -Job $job -DeepHash $true
    Assert-InventoryEqual -Expected $inventory -Actual $postStage -Label 'Post-stage source'

    $headers = Get-GitHubHeaders
    Assert-RemoteRepositoryWritableAndPrivate `
        -RepositorySlug $identity.Slug `
        -Headers $headers
    $release = Get-Release `
        -RepositorySlug $identity.Slug `
        -Tag $ReleaseTag `
        -Headers $headers
    $remoteAsset = Publish-OrResumeReleaseAsset `
        -RepositorySlug $identity.Slug `
        -Release $release `
        -AssetName $job.AssetName `
        -StagedPath $staging.Paths.Asset `
        -ExpectedBytes ([int64]$staging.Metadata.asset_bytes) `
        -ExpectedSha256 ([string]$staging.Metadata.asset_sha256) `
        -Headers $headers
    $manifestRow = Get-ExpectedManifestRow `
        -Job $job `
        -Metadata $staging.Metadata `
        -RemoteAsset $remoteAsset
    $manifestVerification = Add-Push-AndVerifyManifestRow `
        -ExpectedRow $manifestRow `
        -RepositorySlug $identity.Slug `
        -ApprovedPushUrl $identity.PushUrl `
        -BranchName $Branch `
        -Headers $headers

    $release = Get-Release `
        -RepositorySlug $identity.Slug `
        -Tag $ReleaseTag `
        -Headers $headers
    $remoteAsset = Get-VerifiedRemoteAsset `
        -RepositorySlug $identity.Slug `
        -Release $release `
        -AssetName $job.AssetName `
        -ExpectedBytes ([int64]$staging.Metadata.asset_bytes) `
        -ExpectedSha256 ([string]$staging.Metadata.asset_sha256) `
        -Headers $headers
    if ($null -eq $remoteAsset) {
        throw 'Verified remote asset disappeared before deletion gate'
    }
    $preDelete = Get-SourceInventory -Job $job -DeepHash $true
    Assert-InventoryEqual -Expected $inventory -Actual $preDelete -Label 'Pre-delete source'

    $journal = $null
    if ($DeleteSources) {
        Assert-ApprovedParentLayout `
            -ResolvedCaseParent $resolvedCaseParent `
            -ResolvedSnapshotParent $resolvedSnapshotParent
        Assert-CaseTargetLayout -Job $job
        $journal = Remove-SourceRootSafely `
            -Job $job `
            -Inventory $inventory `
            -ArchiveRoot $script:ArchiveRoot `
            -StagedAsset $staging.Paths.Asset `
            -Metadata $staging.Metadata `
            -RemoteCommit $manifestVerification.Commit `
            -DeletionAuthorized $true `
            -RemoteAssetVerified $true `
            -RemoteManifestVerified $true
        $script:StagedAssetGuard.Dispose()
        $script:StagedAssetGuard = $null
        Remove-CompletedStaging `
            -ArchiveRoot $script:ArchiveRoot `
            -Staging $staging `
            -DeletionReceipt $journal.ReceiptPath
        Assert-ApprovedParentLayout `
            -ResolvedCaseParent $resolvedCaseParent `
            -ResolvedSnapshotParent $resolvedSnapshotParent
    }

    [pscustomobject][ordered]@{
        status = if ($DeleteSources) {
            'ARCHIVED_VERIFIED_AND_DELETED'
        } else {
            'ARCHIVED_VERIFIED_RETAINED'
        }
        source_locator = $job.SourceLocator
        asset = $job.AssetName
        asset_bytes = [int64]$staging.Metadata.asset_bytes
        sha256 = [string]$staging.Metadata.asset_sha256
        remote_commit = $manifestVerification.Commit
        sources_deleted = [bool]$DeleteSources
        staged_asset = if ($DeleteSources) { $null } else { $staging.Paths.Asset }
    } | ConvertTo-Json
    exit 0
} catch {
    Write-Error "Legacy OpenFOAM archive failed before deletion or rolled back an authorized delete: $($_.Exception.Message) [$($_.InvocationInfo.ScriptLineNumber)]"
    exit 1
} finally {
    $script:GitHubToken = $null
    $script:GitHubGetOverride = $null
    $script:ReparseQueryOverride = $null
    if ($null -ne $script:StagedAssetGuard) {
        $script:StagedAssetGuard.Dispose()
        $script:StagedAssetGuard = $null
    }
    if ($null -ne $script:ArchiveRepoLock) {
        $script:ArchiveRepoLock.Dispose()
        $script:ArchiveRepoLock = $null
    }
}
