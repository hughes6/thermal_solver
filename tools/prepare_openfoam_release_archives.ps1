#requires -Version 7.0

<#+
.SYNOPSIS
Plans, builds, or verifies the 2026-08-26 OpenFOAM Release-asset set.

.DESCRIPTION
The source allowlist is intentionally fixed. The script reads sources only and
writes only beneath a separately cloned archive repository. It never initializes
Git, commits, pushes, uploads, or deletes an OpenFOAM source.

Plan validates the complete source layout and prints a JSON inventory without
hashing file contents or writing output. Build creates deterministic-name
.tar.zst assets, local receipts, and one machine-readable manifest. Verify
deep-hashes the sources and verifies every existing asset, receipt, and manifest.

The archive repository must be a checkout of the private repository
hughes6/thermal-sim-openfoam-archive on branch main. Its .gitignore must ignore
/.archive-staging/ before Build or Verify is allowed.
#>

[CmdletBinding()]
param(
    [ValidateSet('Plan', 'Build', 'Verify')]
    [string] $Mode = 'Plan',

    [string] $CaseParent =
        'C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_cases',

    [string] $SnapshotParent =
        'C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_benchmark_snapshots',

    [string] $ArchiveRepoPath,

    [string] $ReleaseTag = 'openfoam-checkpoints-2026-08-25'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ArchiveSetId = 'openfoam-archive-2026-08-26'
$ExpectedRepositorySlug = 'hughes6/thermal-sim-openfoam-archive'
$ActiveCaseName = 'new_model_updated_openfoam_export_test'
$ManifestRelativePath = 'manifests/openfoam-archive-manifest-2026-08-26.json'
$StagingRelativePath = '.archive-staging'
$InvariantCulture = [Globalization.CultureInfo]::InvariantCulture
$NumberStyle = [Globalization.NumberStyles]::Float
$Utf8NoBom = [Text.UTF8Encoding]::new($false)
$PathComparison = if ($IsWindows) {
    [StringComparison]::OrdinalIgnoreCase
} else {
    [StringComparison]::Ordinal
}
$PathComparer = if ($IsWindows) {
    [StringComparer]::OrdinalIgnoreCase
} else {
    [StringComparer]::Ordinal
}

$ExpectedSiblingCases = @(
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

$ExpectedSnapshots = @(
    'decomposition_auto_case',
    'dt_baseline_0p0005_case',
    'dt_variant_0p00075_case',
    'pimple_outer2_auto_case'
)

# These later timestep-sensitivity branches are validation evidence. They may
# coexist under the snapshot parent, but are never archive jobs or deletion
# targets in this 2026-08-26 archive set.
$PreservedSnapshots = @(
    'thermal_dt_screen_20_case',
    'thermal_dt_screen_25_case',
    'thermal_dt_screen_30_case',
    'thermal_dt_screen_40_case',
    'thermal_dt_screen_60_case'
)

$CheckpointDefinitions = @(
    [pscustomobject]@{ Exact = '0.40000000000000002'; Label = 't0p4' },
    [pscustomobject]@{ Exact = '0.5'; Label = 't0p5' },
    [pscustomobject]@{ Exact = '0.59999999999999998'; Label = 't0p6' },
    [pscustomobject]@{ Exact = '0.69999999999999996'; Label = 't0p7' },
    [pscustomobject]@{ Exact = '0.80000000000000004'; Label = 't0p8' },
    [pscustomobject]@{ Exact = '0.90000000000000002'; Label = 't0p9' },
    [pscustomobject]@{ Exact = '1'; Label = 't1p0' },
    [pscustomobject]@{ Exact = '1.1000000000000001'; Label = 't1p1' },
    [pscustomobject]@{ Exact = '1.2'; Label = 't1p2' },
    [pscustomobject]@{ Exact = '1.3'; Label = 't1p3' },
    [pscustomobject]@{ Exact = '1.3999999999999999'; Label = 't1p4' }
)

$PreservedActiveTimes = @('0', '1.5', '1.6000000000000001')
$ExpectedFilesPerCheckpointCopy = 74

# These are the only accepted Windows/WSL LX symlink reparse payloads. They are
# internal relative links to the retained 1.6000000000000001 snapshot fields.
$ExpectedUniformLinkHash =
    '50c51c5834e5285413589f9f586e6b0701ad6453573d381f21fefa8a2fc395d4'
$ExpectedFluidUniformLinkHash =
    '93562f1e8fcb41f3d2156619bce683d61e8e08ea890be5339d4e22f323bc3878'

function Get-Sha256Bytes {
    param([Parameter(Mandatory)] [byte[]] $Bytes)

    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        return [Convert]::ToHexString($algorithm.ComputeHash($Bytes)).ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
}

function Get-Sha256Text {
    param([Parameter(Mandatory)] [string] $Text)

    return Get-Sha256Bytes -Bytes $Utf8NoBom.GetBytes($Text)
}

function Get-Sha256File {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $stream = [IO.File]::Open(
        $LiteralPath,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        return [Convert]::ToHexString(
            $algorithm.ComputeHash($stream)).ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
        $stream.Dispose()
    }
}

function Resolve-ExistingDirectory {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Label
    )

    $resolved = @(Resolve-Path -LiteralPath $LiteralPath -ErrorAction Stop)
    if ($resolved.Count -ne 1) {
        throw "$Label must resolve to exactly one directory: $LiteralPath"
    }
    $fullPath = [IO.Path]::GetFullPath($resolved[0].Path).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
    $root = [IO.Path]::GetPathRoot($fullPath).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
    if ($fullPath.Equals($root, $PathComparison)) {
        throw "$Label may not be a filesystem root: $fullPath"
    }
    $item = Get-Item -LiteralPath $fullPath -Force
    if (-not $item.PSIsContainer) {
        throw "$Label is not a directory: $fullPath"
    }
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label may not itself be a reparse point: $fullPath"
    }
    return $fullPath
}

function Test-PathUnderRoot {
    param(
        [Parameter(Mandatory)] [string] $Candidate,
        [Parameter(Mandatory)] [string] $Root
    )

    $prefix = $Root.TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    return $Candidate.StartsWith($prefix, $PathComparison)
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
        $First.Equals($Second, $PathComparison) -or
        (Test-PathUnderRoot -Candidate $First -Root $Second) -or
        (Test-PathUnderRoot -Candidate $Second -Root $First)
    ) {
        throw "$Label must be separate and non-nested: '$First' and '$Second'"
    }
}

function Assert-SafeRelativePath {
    param(
        [Parameter(Mandatory)] [string] $RelativePath,
        [Parameter(Mandatory)] [string] $Label
    )

    if (
        [string]::IsNullOrWhiteSpace($RelativePath) -or
        [IO.Path]::IsPathRooted($RelativePath) -or
        $RelativePath.Contains("`r") -or
        $RelativePath.Contains("`n") -or
        $RelativePath -split '[/\\]' -contains '..'
    ) {
        throw "$Label is not a safe relative path: '$RelativePath'"
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
            throw "$Label contains a duplicate '$value'"
        }
    }
    foreach ($value in $Expected) {
        if (-not $expectedSet.Add([string]$value)) {
            throw "Internal expected set for $Label contains a duplicate '$value'"
        }
    }
    $missing = @($expectedSet | Where-Object { -not $actualSet.Contains($_) })
    $unexpected = @($actualSet | Where-Object { -not $expectedSet.Contains($_) })
    if ($missing.Count -ne 0 -or $unexpected.Count -ne 0) {
        throw "$Label mismatch; missing=[$($missing -join ', ')]; unexpected=[$($unexpected -join ', ')]"
    }
}

function Get-NumericDirectoryNames {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $result = @()
    foreach ($directory in Get-ChildItem -LiteralPath $LiteralPath -Directory -Force) {
        $number = 0.0
        if ([double]::TryParse(
            $directory.Name,
            $NumberStyle,
            $InvariantCulture,
            [ref] $number
        )) {
            if ([double]::IsNaN($number) -or [double]::IsInfinity($number)) {
                throw "Non-finite numeric directory under '$LiteralPath': $($directory.Name)"
            }
            $result += $directory.Name
        }
    }
    return $result
}

function Get-ReparsePayloadHash {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    if (-not $IsWindows) {
        throw 'Known benchmark reparse points require Windows fsutil validation'
    }
    $fsutil = @(Get-Command fsutil -CommandType Application -ErrorAction Stop)[0]
    $output = @(& $fsutil.Source reparsepoint query $LiteralPath 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "fsutil could not read reparse data for '$LiteralPath': $($output -join [Environment]::NewLine)"
    }
    $normalized = (($output -join "`n").Replace("`r", '').Trim() + "`n")
    if ($normalized -notmatch '(?i)0xa000001d') {
        throw "Unexpected reparse tag for '$LiteralPath'"
    }
    return Get-Sha256Text -Text $normalized
}

function Get-ArchiveEntries {
    param(
        [Parameter(Mandatory)] [string] $BaseRoot,
        [Parameter(Mandatory)] [string[]] $RelativeRoots,
        [Parameter(Mandatory)] [hashtable] $AllowedReparseHashes
    )

    $entries = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::Ordinal)
    $seenDirectories = [Collections.Generic.HashSet[string]]::new($PathComparer)

    foreach ($relativeRoot in $RelativeRoots) {
        Assert-SafeRelativePath -RelativePath $relativeRoot -Label 'Archive root'
        $rootPath = [IO.Path]::GetFullPath((Join-Path $BaseRoot $relativeRoot))
        Assert-PathUnderRoot -Candidate $rootPath -Root $BaseRoot -Label 'Archive root'
        if (-not (Test-Path -LiteralPath $rootPath -PathType Container)) {
            throw "Archive root is missing: $rootPath"
        }
        $rootItem = Get-Item -LiteralPath $rootPath -Force
        if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Archive root may not be a reparse point: $rootPath"
        }

        $stack = [Collections.Generic.Stack[string]]::new()
        $stack.Push($rootPath)
        while ($stack.Count -gt 0) {
            $directoryPath = $stack.Pop()
            if (-not $seenDirectories.Add($directoryPath)) {
                continue
            }
            $directoryRelative = [IO.Path]::GetRelativePath(
                $BaseRoot, $directoryPath).Replace('\', '/')
            Assert-SafeRelativePath -RelativePath $directoryRelative -Label 'Directory entry'
            if ($entries.ContainsKey($directoryRelative)) {
                throw "Archive entry appears more than once: $directoryRelative"
            }
            $entries.Add($directoryRelative, [pscustomobject]@{
                ArchivePath = $directoryRelative
                FullPath = $directoryPath
                Kind = 'directory'
                Length = [int64]0
                ReparseSha256 = $null
            })

            $directory = [IO.DirectoryInfo]::new($directoryPath)
            foreach ($child in $directory.EnumerateFileSystemInfos()) {
                $childPath = [IO.Path]::GetFullPath($child.FullName)
                Assert-PathUnderRoot -Candidate $childPath -Root $BaseRoot -Label 'Archive entry'
                $relative = [IO.Path]::GetRelativePath(
                    $BaseRoot, $childPath).Replace('\', '/')
                Assert-SafeRelativePath -RelativePath $relative -Label 'Archive entry'
                $isReparse =
                    ($child.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
                $isDirectory =
                    ($child.Attributes -band [IO.FileAttributes]::Directory) -ne 0

                if ($isReparse) {
                    if (-not $AllowedReparseHashes.ContainsKey($relative)) {
                        throw "Unexpected reparse point: $childPath"
                    }
                    $payloadHash = Get-ReparsePayloadHash -LiteralPath $childPath
                    $expectedHash = [string]$AllowedReparseHashes[$relative]
                    if ($payloadHash -cne $expectedHash) {
                        throw "Reparse payload changed for '$relative': expected $expectedHash, got $payloadHash"
                    }
                    if ($entries.ContainsKey($relative)) {
                        throw "Archive entry appears more than once: $relative"
                    }
                    $entries.Add($relative, [pscustomobject]@{
                        ArchivePath = $relative
                        FullPath = $childPath
                        Kind = 'link'
                        Length = [int64]0
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
                if ($entries.ContainsKey($relative)) {
                    throw "Archive entry appears more than once: $relative"
                }
                $entries.Add($relative, [pscustomobject]@{
                    ArchivePath = $relative
                    FullPath = $childPath
                    Kind = 'file'
                    Length = [int64]$child.Length
                    ReparseSha256 = $null
                })
            }
        }
    }

    $actualLinks = @(
        $entries.Values |
            Where-Object Kind -eq 'link' |
            ForEach-Object ArchivePath)
    Assert-ExactSet `
        -Actual $actualLinks `
        -Expected @($AllowedReparseHashes.Keys) `
        -Label 'Allowed reparse-point set'

    $keys = [string[]]@($entries.Keys)
    [Array]::Sort($keys, [StringComparer]::Ordinal)
    $ordered = [Collections.Generic.List[object]]::new()
    foreach ($key in $keys) {
        $ordered.Add($entries[$key])
    }
    return $ordered.ToArray()
}

function Get-SourceInventory {
    param(
        [Parameter(Mandatory)] $Job,
        [Parameter(Mandatory)] [bool] $DeepHash
    )

    $entries = @(Get-ArchiveEntries `
        -BaseRoot $Job.BaseRoot `
        -RelativeRoots $Job.RelativeRoots `
        -AllowedReparseHashes $Job.AllowedReparseHashes)
    $files = @($entries | Where-Object Kind -eq 'file')
    $directories = @($entries | Where-Object Kind -eq 'directory')
    $links = @($entries | Where-Object Kind -eq 'link')
    $sourceBytes = [int64](($files | Measure-Object Length -Sum).Sum)
    $listingText = (($entries.ArchivePath -join "`n") + "`n")
    $fingerprint = $null

    if ($DeepHash) {
        $records = [Text.StringBuilder]::new()
        foreach ($entry in $entries) {
            switch ($entry.Kind) {
                'directory' {
                    [void]$records.Append("D`t$($entry.ArchivePath)`n")
                }
                'link' {
                    [void]$records.Append(
                        "L`t$($entry.ArchivePath)`t$($entry.ReparseSha256)`n")
                }
                'file' {
                    $beforeLength = [int64](Get-Item -LiteralPath $entry.FullPath -Force).Length
                    if ($beforeLength -ne [int64]$entry.Length) {
                        throw "Source length changed during inventory: $($entry.FullPath)"
                    }
                    $fileHash = Get-Sha256File -LiteralPath $entry.FullPath
                    $afterLength = [int64](Get-Item -LiteralPath $entry.FullPath -Force).Length
                    if ($afterLength -ne $beforeLength) {
                        throw "Source length changed while hashing: $($entry.FullPath)"
                    }
                    [void]$records.Append(
                        "F`t$($entry.ArchivePath)`t$beforeLength`t$fileHash`n")
                }
                default {
                    throw "Internal unsupported entry kind: $($entry.Kind)"
                }
            }
        }
        $fingerprint = Get-Sha256Text -Text $records.ToString()
    }

    return [pscustomobject]@{
        Entries = $entries
        EntryCount = $entries.Count
        SourceFileCount = $files.Count
        SourceBytes = $sourceBytes
        DirectoryCount = $directories.Count
        LinkCount = $links.Count
        ListingSha256 = Get-Sha256Text -Text $listingText
        SourceFingerprintSha256 = $fingerprint
    }
}

function Get-CheckpointFileManifest {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $reparse = @(
        Get-ChildItem -LiteralPath $LiteralPath -Force -Recurse `
            -Attributes ReparsePoint -ErrorAction Stop)
    if ($reparse.Count -ne 0) {
        throw "Checkpoint copy contains a reparse point: $($reparse[0].FullName)"
    }
    $files = @(Get-ChildItem -LiteralPath $LiteralPath -File -Force -Recurse)
    if ($files.Count -ne $ExpectedFilesPerCheckpointCopy) {
        throw "Checkpoint copy '$LiteralPath' has $($files.Count) files; expected $ExpectedFilesPerCheckpointCopy"
    }
    $empty = @($files | Where-Object Length -le 0)
    if ($empty.Count -ne 0) {
        throw "Checkpoint copy contains an empty file: $($empty[0].FullName)"
    }
    $relative = [string[]]@(
        $files | ForEach-Object {
            [IO.Path]::GetRelativePath($LiteralPath, $_.FullName).Replace('\', '/')
        })
    [Array]::Sort($relative, [StringComparer]::Ordinal)
    foreach ($required in @('fluid/T', 'fluid/U')) {
        if ([Array]::IndexOf($relative, $required) -lt 0) {
            throw "Checkpoint copy '$LiteralPath' lacks required restart field '$required'"
        }
    }
    return $relative
}

function ConvertTo-AssetSlug {
    param([Parameter(Mandatory)] [string] $Value)

    $slug = $Value.ToLowerInvariant() -replace '[^a-z0-9]+', '-'
    $slug = $slug.Trim('-')
    if ([string]::IsNullOrWhiteSpace($slug)) {
        throw "Could not derive an asset slug from '$Value'"
    }
    return $slug
}

function New-ArchiveJob {
    param(
        [Parameter(Mandatory)] [string] $Category,
        [Parameter(Mandatory)] [string] $AssetName,
        [Parameter(Mandatory)] [string] $SourceLocator,
        [Parameter(Mandatory)] [string] $BaseRoot,
        [Parameter(Mandatory)] [string[]] $RelativeRoots,
        [Parameter(Mandatory)] [hashtable] $AllowedReparseHashes
    )

    if ($AssetName -notmatch '^[a-z0-9][a-z0-9-]*\.tar\.zst$') {
        throw "Unsafe deterministic asset name: $AssetName"
    }
    return [pscustomobject]@{
        Category = $Category
        AssetName = $AssetName
        SourceLocator = $SourceLocator
        BaseRoot = $BaseRoot
        RelativeRoots = $RelativeRoots
        AllowedReparseHashes = $AllowedReparseHashes
    }
}

function Get-AllowedSnapshotReparseHashes {
    param([Parameter(Mandatory)] [string] $SnapshotName)

    $allowed = @{}
    if ($SnapshotName -in @('decomposition_auto_case', 'pimple_outer2_auto_case')) {
        foreach ($rank in 0..3) {
            $prefix = "$SnapshotName/processor$rank/1.6000000000000001"
            $allowed["$prefix/uniform"] = $ExpectedUniformLinkHash
            $allowed["$prefix/fluid/uniform"] = $ExpectedFluidUniformLinkHash
        }
    }
    return $allowed
}

function Get-ArchiveJobs {
    param(
        [Parameter(Mandatory)] [string] $ResolvedCaseParent,
        [Parameter(Mandatory)] [string] $ResolvedSnapshotParent
    )

    $caseParentFiles = @(Get-ChildItem -LiteralPath $ResolvedCaseParent -File -Force)
    if ($caseParentFiles.Count -ne 0) {
        throw "Unexpected file directly under case parent: $($caseParentFiles[0].FullName)"
    }
    $snapshotParentFiles = @(Get-ChildItem -LiteralPath $ResolvedSnapshotParent -File -Force)
    if ($snapshotParentFiles.Count -ne 0) {
        throw "Unexpected file directly under snapshot parent: $($snapshotParentFiles[0].FullName)"
    }

    $actualCaseDirectories = @(
        Get-ChildItem -LiteralPath $ResolvedCaseParent -Directory -Force |
            ForEach-Object Name)
    Assert-ExactSet `
        -Actual $actualCaseDirectories `
        -Expected @($ExpectedSiblingCases + $ActiveCaseName) `
        -Label 'OpenFOAM case-parent directories'

    $actualSnapshotDirectories = @(
        Get-ChildItem -LiteralPath $ResolvedSnapshotParent -Directory -Force |
            ForEach-Object Name)
    Assert-ExactSet `
        -Actual $actualSnapshotDirectories `
        -Expected @($ExpectedSnapshots + $PreservedSnapshots) `
        -Label 'OpenFOAM benchmark-snapshot directories'

    $jobs = [Collections.Generic.List[object]]::new()

    foreach ($caseName in $ExpectedSiblingCases) {
        $casePath = Join-Path $ResolvedCaseParent $caseName
        Assert-ExactSet `
            -Actual @(Get-NumericDirectoryNames -LiteralPath $casePath) `
            -Expected @('0') `
            -Label "Numeric times for sibling case '$caseName'"
        $processors = @(
            Get-ChildItem -LiteralPath $casePath -Directory -Force |
                Where-Object Name -Match '^processor[0-9]+$')
        if ($processors.Count -ne 0) {
            throw "Sibling time-zero case unexpectedly contains processor output: $casePath"
        }
        if (Test-Path -LiteralPath (Join-Path $casePath 'postProcessing')) {
            throw "Sibling time-zero case unexpectedly contains postProcessing: $casePath"
        }
        $slug = ConvertTo-AssetSlug -Value $caseName
        $jobs.Add((New-ArchiveJob `
            -Category 'sibling_time0_case' `
            -AssetName "openfoam-case-$slug-time0-20260826.tar.zst" `
            -SourceLocator "openfoam_cases/$caseName" `
            -BaseRoot $ResolvedCaseParent `
            -RelativeRoots @($caseName) `
            -AllowedReparseHashes @{}))
    }

    foreach ($snapshotName in $ExpectedSnapshots) {
        $slug = ConvertTo-AssetSlug -Value $snapshotName
        $jobs.Add((New-ArchiveJob `
            -Category 'benchmark_snapshot' `
            -AssetName "openfoam-benchmark-$slug-20260826.tar.zst" `
            -SourceLocator "openfoam_benchmark_snapshots/$snapshotName" `
            -BaseRoot $ResolvedSnapshotParent `
            -RelativeRoots @($snapshotName) `
            -AllowedReparseHashes (
                Get-AllowedSnapshotReparseHashes -SnapshotName $snapshotName)))
    }

    $activeCase = Join-Path $ResolvedCaseParent $ActiveCaseName
    $processorNames = @(
        Get-ChildItem -LiteralPath $activeCase -Directory -Force |
            Where-Object Name -Match '^processor[0-9]+$' |
            ForEach-Object Name)
    $expectedProcessors = @(0..3 | ForEach-Object { "processor$_" })
    Assert-ExactSet `
        -Actual $processorNames `
        -Expected $expectedProcessors `
        -Label 'Active-case processor directories'

    $expectedActiveTimes = @(
        $PreservedActiveTimes +
        @($CheckpointDefinitions | ForEach-Object Exact))
    Assert-ExactSet `
        -Actual @(Get-NumericDirectoryNames -LiteralPath $activeCase) `
        -Expected $expectedActiveTimes `
        -Label 'Active reconstructed numeric times'
    foreach ($processor in $expectedProcessors) {
        Assert-ExactSet `
            -Actual @(Get-NumericDirectoryNames -LiteralPath (Join-Path $activeCase $processor)) `
            -Expected $expectedActiveTimes `
            -Label "Active $processor numeric times"
    }

    foreach ($checkpoint in $CheckpointDefinitions) {
        $relativeRoots = @("$ActiveCaseName/$($checkpoint.Exact)")
        $copyPaths = @((Join-Path $activeCase $checkpoint.Exact))
        foreach ($processor in $expectedProcessors) {
            $relativeRoots += "$ActiveCaseName/$processor/$($checkpoint.Exact)"
            $copyPaths += Join-Path (Join-Path $activeCase $processor) $checkpoint.Exact
        }
        $referenceManifest = $null
        foreach ($copyPath in $copyPaths) {
            if (-not (Test-Path -LiteralPath $copyPath -PathType Container)) {
                throw "Required checkpoint copy is missing: $copyPath"
            }
            $manifest = @(Get-CheckpointFileManifest -LiteralPath $copyPath)
            $manifestKey = $manifest -join "`n"
            if ($null -eq $referenceManifest) {
                $referenceManifest = $manifestKey
            } elseif ($manifestKey -cne $referenceManifest) {
                throw "Checkpoint copies do not have identical file manifests at t=$($checkpoint.Exact)"
            }
        }
        $jobs.Add((New-ArchiveJob `
            -Category 'active_checkpoint_group' `
            -AssetName "openfoam-active-checkpoint-$($checkpoint.Label)-20260826.tar.zst" `
            -SourceLocator "openfoam_cases/$ActiveCaseName/checkpoint/$($checkpoint.Exact)" `
            -BaseRoot $ResolvedCaseParent `
            -RelativeRoots $relativeRoots `
            -AllowedReparseHashes @{}))
    }

    $assetNames = @($jobs | ForEach-Object AssetName)
    Assert-ExactSet -Actual $assetNames -Expected $assetNames -Label 'Asset names'
    $ordered = [object[]]@($jobs)
    [Array]::Sort($ordered, [Comparison[object]]{
        param($left, $right)
        return [StringComparer]::Ordinal.Compare($left.AssetName, $right.AssetName)
    })
    return $ordered
}

function ConvertTo-GitHubSlug {
    param([Parameter(Mandatory)] [string] $Url)

    $trimmed = $Url.Trim()
    if ($trimmed -match '^(?i)https://github\.com/([^/]+/[^/]+?)(?:\.git)?$') {
        return $Matches[1]
    }
    if ($trimmed -match '^(?i)git@github\.com:([^/]+/[^/]+?)(?:\.git)?$') {
        return $Matches[1]
    }
    throw "Archive origin is not an accepted GitHub URL: $Url"
}

function Resolve-ArchiveRepository {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $ResolvedCaseParent,
        [Parameter(Mandatory)] [string] $ResolvedSnapshotParent
    )

    $root = Resolve-ExistingDirectory -LiteralPath $LiteralPath -Label 'ArchiveRepoPath'
    Assert-SeparateTrees -First $root -Second $ResolvedCaseParent -Label 'Archive repo and case parent'
    Assert-SeparateTrees -First $root -Second $ResolvedSnapshotParent -Label 'Archive repo and snapshot parent'
    $git = @(Get-Command git -CommandType Application -ErrorAction Stop)[0]
    $topOutput = @(& $git.Source -C $root rev-parse --show-toplevel 2>&1)
    if ($LASTEXITCODE -ne 0 -or $topOutput.Count -ne 1) {
        throw "ArchiveRepoPath is not a Git working tree: $root"
    }
    $top = [IO.Path]::GetFullPath($topOutput[0]).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
    if (-not $top.Equals($root, $PathComparison)) {
        throw "ArchiveRepoPath must be the working-tree root; Git returned '$top'"
    }
    $originOutput = @(& $git.Source -C $root remote get-url origin 2>&1)
    if ($LASTEXITCODE -ne 0 -or $originOutput.Count -ne 1) {
        throw 'Archive repository must have exactly one readable origin URL'
    }
    $slug = ConvertTo-GitHubSlug -Url $originOutput[0]
    if (-not $slug.Equals($ExpectedRepositorySlug, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Wrong archive repository: expected $ExpectedRepositorySlug, got $slug"
    }
    $branchOutput = @(& $git.Source -C $root branch --show-current 2>&1)
    if ($LASTEXITCODE -ne 0 -or $branchOutput.Count -ne 1 -or $branchOutput[0] -cne 'main') {
        throw "Archive repository must be checked out on main; got '$($branchOutput -join ' ')'."
    }
    & $git.Source -C $root check-ignore --quiet -- "$StagingRelativePath/probe"
    if ($LASTEXITCODE -ne 0) {
        throw "Archive repository must ignore /$StagingRelativePath/ before assets are built"
    }
    & $git.Source -C $root check-ignore --quiet -- $ManifestRelativePath
    if ($LASTEXITCODE -eq 0) {
        throw "Archive manifest must not be ignored: $ManifestRelativePath"
    }
    return $root
}

function Test-ZstdArchive {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $zstd = @(Get-Command zstd -CommandType Application -ErrorAction Stop)[0]
    $output = @(& $zstd.Source --quiet --test $LiteralPath 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "zstd integrity test failed for '$LiteralPath': $($output -join [Environment]::NewLine)"
    }
}

function Get-VerifiedTarListing {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [object[]] $ExpectedEntries
    )

    $tar = @(Get-Command tar -CommandType Application -ErrorAction Stop)[0]
    $output = @(& $tar.Source -tf $LiteralPath 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "tar listing failed for '$LiteralPath': $($output -join [Environment]::NewLine)"
    }
    $actual = [Collections.Generic.List[string]]::new()
    foreach ($line in $output) {
        $value = ([string]$line).Replace('\', '/').Trim()
        while ($value.StartsWith('./', [StringComparison]::Ordinal)) {
            $value = $value.Substring(2)
        }
        $value = $value.TrimEnd('/')
        Assert-SafeRelativePath -RelativePath $value -Label 'Tar member'
        $actual.Add($value)
    }
    $expected = @($ExpectedEntries | ForEach-Object ArchivePath)
    if ($actual.Count -ne $expected.Count) {
        throw "Tar member count mismatch for '$LiteralPath': expected $($expected.Count), got $($actual.Count)"
    }
    for ($index = 0; $index -lt $expected.Count; $index++) {
        if ($actual[$index] -cne $expected[$index]) {
            throw "Tar member mismatch at index $index for '$LiteralPath': expected '$($expected[$index])', got '$($actual[$index])'"
        }
    }
    return [pscustomobject]@{
        MemberCount = $actual.Count
        ListingSha256 = Get-Sha256Text -Text (($actual -join "`n") + "`n")
    }
}

function Assert-InventoryEqual {
    param(
        [Parameter(Mandatory)] $Before,
        [Parameter(Mandatory)] $After,
        [Parameter(Mandatory)] [string] $Label
    )

    foreach ($property in @(
        'EntryCount',
        'SourceFileCount',
        'SourceBytes',
        'DirectoryCount',
        'LinkCount',
        'ListingSha256',
        'SourceFingerprintSha256'
    )) {
        if ([string]$Before.$property -cne [string]$After.$property) {
            throw "$Label changed during archive preparation ($property)"
        }
    }
}

function Write-AtomicUtf8File {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Content,
        [Parameter(Mandatory)] [string] $Root
    )

    $fullPath = [IO.Path]::GetFullPath($LiteralPath)
    Assert-PathUnderRoot -Candidate $fullPath -Root $Root -Label 'Output file'
    $directory = [IO.Path]::GetDirectoryName($fullPath)
    [IO.Directory]::CreateDirectory($directory) | Out-Null
    $temporary = "$fullPath.part"
    Assert-PathUnderRoot -Candidate $temporary -Root $Root -Label 'Temporary output file'
    if ([IO.File]::Exists($temporary)) {
        [IO.File]::Delete($temporary)
    }
    [IO.File]::WriteAllText($temporary, $Content, $Utf8NoBom)
    if ([IO.File]::Exists($fullPath)) {
        $existing = [IO.File]::ReadAllText($fullPath, $Utf8NoBom)
        if ($existing -ceq $Content) {
            [IO.File]::Delete($temporary)
            return
        }
        [IO.File]::Delete($temporary)
        throw "Refusing to overwrite conflicting output file: $fullPath"
    }
    [IO.File]::Move($temporary, $fullPath)
}

function Get-ReceiptObject {
    param(
        [Parameter(Mandatory)] $Job,
        [Parameter(Mandatory)] $Inventory,
        [Parameter(Mandatory)] [string] $AssetPath,
        [Parameter(Mandatory)] $TarListing
    )

    $assetItem = Get-Item -LiteralPath $AssetPath -Force
    return [ordered]@{
        schema_version = 1
        archive_set = $ArchiveSetId
        release_tag = $ReleaseTag
        repository = $ExpectedRepositorySlug
        category = $Job.Category
        source_locator = $Job.SourceLocator
        relative_roots = @($Job.RelativeRoots)
        asset_name = $Job.AssetName
        source_file_count = [int64]$Inventory.SourceFileCount
        source_bytes = [int64]$Inventory.SourceBytes
        source_entry_count = [int64]$Inventory.EntryCount
        source_directory_count = [int64]$Inventory.DirectoryCount
        source_link_count = [int64]$Inventory.LinkCount
        source_listing_sha256 = $Inventory.ListingSha256
        source_fingerprint_sha256 = $Inventory.SourceFingerprintSha256
        asset_bytes = [int64]$assetItem.Length
        asset_sha256 = Get-Sha256File -LiteralPath $AssetPath
        tar_member_count = [int64]$TarListing.MemberCount
        tar_listing_sha256 = $TarListing.ListingSha256
        zstd_integrity = 'passed'
        tar_integrity = 'passed'
        status = 'prepared_not_uploaded'
    }
}

function Assert-ReceiptMatches {
    param(
        [Parameter(Mandatory)] $Receipt,
        [Parameter(Mandatory)] $ExpectedReceipt,
        [Parameter(Mandatory)] [string] $Label
    )

    $actualJson = ($Receipt | ConvertTo-Json -Depth 8 -Compress)
    $expectedJson = ($ExpectedReceipt | ConvertTo-Json -Depth 8 -Compress)
    if ($actualJson -cne $expectedJson) {
        throw "$Label does not match the current source and asset"
    }
}

function Invoke-BuildOrVerifyJob {
    param(
        [Parameter(Mandatory)] $Job,
        [Parameter(Mandatory)] [string] $ArchiveRoot,
        [Parameter(Mandatory)] [bool] $Build
    )

    $stagingRoot = [IO.Path]::GetFullPath((Join-Path $ArchiveRoot $StagingRelativePath))
    Assert-PathUnderRoot -Candidate $stagingRoot -Root $ArchiveRoot -Label 'Staging root'
    $assetDirectory = Join-Path $stagingRoot 'assets'
    $receiptDirectory = Join-Path $stagingRoot 'receipts'
    $listDirectory = Join-Path $stagingRoot 'lists'
    if ($Build) {
        [IO.Directory]::CreateDirectory($assetDirectory) | Out-Null
        [IO.Directory]::CreateDirectory($receiptDirectory) | Out-Null
        [IO.Directory]::CreateDirectory($listDirectory) | Out-Null
    }
    $assetPath = [IO.Path]::GetFullPath((Join-Path $assetDirectory $Job.AssetName))
    $partialPath = "$assetPath.part"
    $receiptPath = [IO.Path]::GetFullPath((Join-Path $receiptDirectory "$($Job.AssetName).json"))
    $listPath = [IO.Path]::GetFullPath((Join-Path $listDirectory "$($Job.AssetName).txt"))
    foreach ($path in @($assetPath, $partialPath, $receiptPath, $listPath)) {
        Assert-PathUnderRoot -Candidate $path -Root $ArchiveRoot -Label 'Archive output'
    }

    $inventory = Get-SourceInventory -Job $Job -DeepHash $true
    $assetExists = Test-Path -LiteralPath $assetPath -PathType Leaf
    $receiptExists = Test-Path -LiteralPath $receiptPath -PathType Leaf
    if ($assetExists -xor $receiptExists) {
        throw "Asset and receipt must either both exist or both be absent: $($Job.AssetName)"
    }

    if (-not $assetExists) {
        if (-not $Build) {
            throw "Prepared asset is missing: $assetPath"
        }
        if (Test-Path -LiteralPath $partialPath) {
            [IO.File]::Delete($partialPath)
        }
        $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($assetPath))
        $requiredFree = [int64]$inventory.SourceBytes + 1GB
        if ($drive.AvailableFreeSpace -lt $requiredFree) {
            throw "Insufficient staging space for '$($Job.AssetName)': need at least $requiredFree bytes free, found $($drive.AvailableFreeSpace)"
        }

        [IO.File]::WriteAllLines(
            $listPath,
            [string[]]@($inventory.Entries | ForEach-Object ArchivePath),
            $Utf8NoBom)
        $tar = @(Get-Command tar -CommandType Application -ErrorAction Stop)[0]
        $tarArguments = @(
            '-c',
            '--zstd',
            '--format', 'pax',
            '--no-recursion',
            '--no-acls',
            '--no-xattrs',
            '--no-fflags',
            '--uid', '0',
            '--gid', '0',
            '--uname', 'root',
            '--gname', 'root',
            '--mtime', '1970-01-01 00:00:00Z',
            '-f', $partialPath,
            '-C', $Job.BaseRoot,
            '-T', $listPath)
        $tarOutput = @(& $tar.Source @tarArguments 2>&1)
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $partialPath -PathType Leaf)) {
            throw "tar creation failed for '$($Job.AssetName)': $($tarOutput -join [Environment]::NewLine)"
        }

        $after = Get-SourceInventory -Job $Job -DeepHash $true
        Assert-InventoryEqual `
            -Before $inventory `
            -After $after `
            -Label $Job.SourceLocator
        Test-ZstdArchive -LiteralPath $partialPath
        $tarListing = Get-VerifiedTarListing `
            -LiteralPath $partialPath `
            -ExpectedEntries $inventory.Entries
        [IO.File]::Move($partialPath, $assetPath)
        $receipt = Get-ReceiptObject `
            -Job $Job `
            -Inventory $inventory `
            -AssetPath $assetPath `
            -TarListing $tarListing
        Write-AtomicUtf8File `
            -LiteralPath $receiptPath `
            -Content (($receipt | ConvertTo-Json -Depth 8) + "`n") `
            -Root $ArchiveRoot
        if (Test-Path -LiteralPath $listPath) {
            [IO.File]::Delete($listPath)
        }
        return [pscustomobject]$receipt
    }

    Test-ZstdArchive -LiteralPath $assetPath
    $listing = Get-VerifiedTarListing `
        -LiteralPath $assetPath `
        -ExpectedEntries $inventory.Entries
    $expectedReceipt = Get-ReceiptObject `
        -Job $Job `
        -Inventory $inventory `
        -AssetPath $assetPath `
        -TarListing $listing
    $actualReceipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    Assert-ReceiptMatches `
        -Receipt $actualReceipt `
        -ExpectedReceipt $expectedReceipt `
        -Label "Receipt for $($Job.AssetName)"
    return [pscustomobject]$expectedReceipt
}

function Get-ManifestObject {
    param([Parameter(Mandatory)] [object[]] $Receipts)

    $assets = @(
        $Receipts | Sort-Object asset_name | ForEach-Object {
            [pscustomobject][ordered]@{
                category = $_.category
                source_locator = $_.source_locator
                relative_roots = @($_.relative_roots)
                asset_name = $_.asset_name
                source_file_count = [int64]$_.source_file_count
                source_bytes = [int64]$_.source_bytes
                source_entry_count = [int64]$_.source_entry_count
                source_directory_count = [int64]$_.source_directory_count
                source_link_count = [int64]$_.source_link_count
                source_listing_sha256 = $_.source_listing_sha256
                source_fingerprint_sha256 = $_.source_fingerprint_sha256
                asset_bytes = [int64]$_.asset_bytes
                asset_sha256 = $_.asset_sha256
                tar_member_count = [int64]$_.tar_member_count
                tar_listing_sha256 = $_.tar_listing_sha256
                zstd_integrity = $_.zstd_integrity
                tar_integrity = $_.tar_integrity
                status = $_.status
            }
        })
    return [ordered]@{
        schema_version = 1
        archive_set = $ArchiveSetId
        repository = $ExpectedRepositorySlug
        repository_visibility_required = 'private'
        release_tag = $ReleaseTag
        asset_format = 'tar.zst'
        asset_count = $assets.Count
        source_file_count = [int64](($assets | Measure-Object source_file_count -Sum).Sum)
        source_bytes = [int64](($assets | Measure-Object source_bytes -Sum).Sum)
        exclusions = [ordered]@{
            active_case = $ActiveCaseName
            retained_numeric_times = @('0', '1.5', '1.6000000000000001')
            retained_paths = @('constant', 'system', 'postProcessing', 'root logs and run summaries')
            validation_evidence = 'All project validation evidence remains outside this archive source set.'
        }
        assets = $assets
    }
}

if ($ReleaseTag -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') {
    throw "Unsafe release tag: $ReleaseTag"
}

$resolvedCaseParent = Resolve-ExistingDirectory `
    -LiteralPath $CaseParent `
    -Label 'CaseParent'
$resolvedSnapshotParent = Resolve-ExistingDirectory `
    -LiteralPath $SnapshotParent `
    -Label 'SnapshotParent'
Assert-SeparateTrees `
    -First $resolvedCaseParent `
    -Second $resolvedSnapshotParent `
    -Label 'Case and snapshot parents'
$jobs = @(Get-ArchiveJobs `
    -ResolvedCaseParent $resolvedCaseParent `
    -ResolvedSnapshotParent $resolvedSnapshotParent)
if ($jobs.Count -ne 28) {
    throw "Internal archive job count mismatch: expected 28, got $($jobs.Count)"
}

if ($Mode -eq 'Plan') {
    $planAssets = @()
    foreach ($job in $jobs) {
        $inventory = Get-SourceInventory -Job $job -DeepHash $false
        $planAssets += [pscustomobject][ordered]@{
            category = $job.Category
            source_locator = $job.SourceLocator
            relative_roots = @($job.RelativeRoots)
            asset_name = $job.AssetName
            source_file_count = [int64]$inventory.SourceFileCount
            source_bytes = [int64]$inventory.SourceBytes
            source_entry_count = [int64]$inventory.EntryCount
            source_link_count = [int64]$inventory.LinkCount
            status = 'planned_not_built'
        }
    }
    [ordered]@{
        schema_version = 1
        mode = 'Plan'
        archive_set = $ArchiveSetId
        repository = $ExpectedRepositorySlug
        release_tag = $ReleaseTag
        asset_count = $planAssets.Count
        source_file_count = [int64](($planAssets | Measure-Object source_file_count -Sum).Sum)
        source_bytes = [int64](($planAssets | Measure-Object source_bytes -Sum).Sum)
        assets = $planAssets
    } | ConvertTo-Json -Depth 8
    exit 0
}

if ([string]::IsNullOrWhiteSpace($ArchiveRepoPath)) {
    throw 'ArchiveRepoPath is required for Build and Verify'
}
$archiveRoot = Resolve-ArchiveRepository `
    -LiteralPath $ArchiveRepoPath `
    -ResolvedCaseParent $resolvedCaseParent `
    -ResolvedSnapshotParent $resolvedSnapshotParent
$receipts = @()
foreach ($job in $jobs) {
    $receipts += Invoke-BuildOrVerifyJob `
        -Job $job `
        -ArchiveRoot $archiveRoot `
        -Build ($Mode -eq 'Build')
}
$manifest = Get-ManifestObject -Receipts $receipts
$manifestJson = (($manifest | ConvertTo-Json -Depth 10) + "`n")
$manifestPath = [IO.Path]::GetFullPath((Join-Path $archiveRoot $ManifestRelativePath))
Assert-PathUnderRoot -Candidate $manifestPath -Root $archiveRoot -Label 'Manifest path'
if ($Mode -eq 'Build') {
    Write-AtomicUtf8File `
        -LiteralPath $manifestPath `
        -Content $manifestJson `
        -Root $archiveRoot
} else {
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Archive manifest is missing: $manifestPath"
    }
    $actualManifest = [IO.File]::ReadAllText($manifestPath, $Utf8NoBom)
    if ($actualManifest -cne $manifestJson) {
        throw 'Archive manifest does not exactly match verified receipts and current sources'
    }
}

[ordered]@{
    status = if ($Mode -eq 'Build') { 'PREPARED_NOT_UPLOADED' } else { 'VERIFIED_LOCAL_ONLY' }
    archive_set = $ArchiveSetId
    asset_count = $receipts.Count
    manifest = $manifestPath
    source_bytes = [int64]$manifest.source_bytes
    note = 'No Git commit, push, upload, or source deletion was performed.'
} | ConvertTo-Json -Depth 4
