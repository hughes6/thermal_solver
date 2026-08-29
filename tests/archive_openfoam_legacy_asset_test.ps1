#requires -Version 7.0

[CmdletBinding()]
param(
    [switch] $RunRealLinkProbe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$workspace = Split-Path -Parent $PSScriptRoot
$scriptPath = Join-Path $workspace 'tools/archive_openfoam_legacy_asset.ps1'
. $scriptPath -LoadFunctionsOnly

$script:Passed = 0

function Assert-True {
    param(
        [Parameter(Mandatory)] [bool] $Condition,
        [Parameter(Mandatory)] [string] $Message
    )
    if (-not $Condition) { throw "ASSERTION FAILED: $Message" }
    ++$script:Passed
}

function Assert-Equal {
    param(
        [Parameter(Mandatory)] $Expected,
        [Parameter(Mandatory)] $Actual,
        [Parameter(Mandatory)] [string] $Message
    )
    if ([string]$Expected -cne [string]$Actual) {
        throw "ASSERTION FAILED: $Message; expected='$Expected' actual='$Actual'"
    }
    ++$script:Passed
}

function Assert-Throws {
    param(
        [Parameter(Mandatory)] [scriptblock] $Action,
        [Parameter(Mandatory)] [string] $Message,
        [string] $Like = '*'
    )
    $threw = $false
    try {
        & $Action
    } catch {
        $threw = $true
        if ($_.Exception.Message -notlike $Like) {
            throw "ASSERTION FAILED: $Message; unexpected error '$($_.Exception.Message)'"
        }
    }
    if (-not $threw) { throw "ASSERTION FAILED: $Message; action did not throw" }
    ++$script:Passed
}

function Write-TestFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Text
    )
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($LiteralPath)) |
        Out-Null
    [IO.File]::WriteAllText($LiteralPath, $Text, [Text.UTF8Encoding]::new($false))
}

function New-TestAsset {
    param(
        [Parameter(Mandatory)] [int] $Id,
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [int64] $Bytes,
        [Parameter(Mandatory)] [string] $Sha
    )
    return [pscustomobject]@{
        id = $Id
        name = $Name
        size = $Bytes
        digest = "sha256:$Sha"
        state = 'uploaded'
        created_at = '2026-08-26T12:00:00Z'
    }
}

function New-TestDeletionJournal {
    param(
        [Parameter(Mandatory)] $Job,
        [Parameter(Mandatory)] [string] $ArchiveRoot,
        [Parameter(Mandatory)] $Staging,
        [Parameter(Mandatory)] [string] $SourceFingerprint,
        [ValidateSet('prepared', 'quarantined', 'sources_deleted')]
        [string] $Phase = 'quarantined'
    )

    $paths = Get-DeletionPaths -ArchiveRoot $ArchiveRoot -Job $Job
    [IO.Directory]::CreateDirectory($paths.Root) | Out-Null
    $source = [IO.Path]::GetFullPath((Join-Path $Job.ParentRoot $Job.RelativeRoot))
    $receipt = [pscustomobject][ordered]@{
        schema_version = 1
        source_locator = $Job.SourceLocator
        source_path = $source
        asset = $Job.AssetName
        asset_path = $Staging.Paths.Asset
        asset_sha256 = [string]$Staging.Metadata.asset_sha256
        source_fingerprint_sha256 = $SourceFingerprint
        remote_commit = 'e' * 40
        phase = $Phase
    }
    Write-DurableUtf8File `
        -LiteralPath $paths.Receipt `
        -Content (($receipt | ConvertTo-Json -Depth 5) + "`n") `
        -ApprovedRoot $ArchiveRoot
    return $paths
}

$temporaryRoot = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) (
    'openfoam-legacy-archive-test-' + [guid]::NewGuid().ToString('N'))))
[IO.Directory]::CreateDirectory($temporaryRoot) | Out-Null

try {
    Assert-Equal 13 $script:ApprovedCases.Count 'case allowlist has exactly 13 roots'
    Assert-Equal 4 $script:ApprovedSnapshots.Count 'snapshot allowlist has exactly four roots'
    Assert-Equal 5 $script:PreservedSnapshots.Count 'preserved snapshot allowlist has five roots'
    Assert-True ($script:ApprovedCases -cnotcontains $script:ActiveCaseName) `
        'active case is never an approved legacy deletion root'
    Assert-True (
        @($script:ApprovedSnapshots | Where-Object {
            $script:PreservedSnapshots -ccontains $_
        }).Count -eq 0
    ) 'archive and preservation snapshot allowlists do not overlap'

    $sourceText = [IO.File]::ReadAllText($scriptPath)
    Assert-True ($sourceText.Contains('${uploadBase}?name=')) `
        'upload URL braces the variable before a query string'
    Assert-True ($sourceText.Contains('${assetsBase}?per_page=100&page=')) `
        'paginated assets URL braces the variable before a query string'
    Assert-True ($sourceText.Contains('${contentsBase}?ref=')) `
        'manifest readback URL braces the variable before a query string'
    Assert-True (-not [regex]::IsMatch(
        $sourceText,
        '"\$[A-Za-z_][A-Za-z0-9_]*\?'
    )) 'no unsafe PowerShell variable/query interpolation remains'
    Assert-True (-not [regex]::IsMatch(
        $sourceText,
        '(?i)-Method\s+(Delete|Patch)'
    )) 'publisher contains no remote delete or replacement verb'
    Assert-True ($sourceText.Contains("[switch] `$DeleteSources")) `
        'source deletion is exposed only through an explicit switch'
    Assert-True ($sourceText.Contains("[ValidateSet('Plan', 'Validate', 'Archive', 'Recover')]")) `
        'recovery is an explicit mode separate from archive/deletion'
    Assert-True (-not $sourceText.Contains('Move-Item')) `
        'directory quarantine and restoration avoid provider move semantics'
    $recoverBranchIndex = $sourceText.IndexOf("if (`$Mode -ceq 'Recover')")
    $recoverGuardIndex = $sourceText.IndexOf(
        '$script:StagedAssetGuard = [IO.File]::Open(', $recoverBranchIndex)
    $recoverProofIndex = $sourceText.IndexOf(
        '$remoteProof = Confirm-PriorDeletionRemoteProof', $recoverBranchIndex)
    $recoverRestoreIndex = $sourceText.IndexOf(
        '$recovery = Restore-PriorDeletionJournal', $recoverBranchIndex)
    Assert-True (
        $recoverBranchIndex -ge 0 -and
        $recoverGuardIndex -gt $recoverBranchIndex -and
        $recoverProofIndex -gt $recoverGuardIndex -and
        $recoverRestoreIndex -gt $recoverProofIndex
    ) 'Recover mode holds the staged-asset guard before remote proof and extraction'

    Assert-Throws {
        Get-ApprovedJob `
            -Key '../lab_screening_export' `
            -ResolvedCaseParent $temporaryRoot `
            -ResolvedSnapshotParent $temporaryRoot
    } 'path-shaped asset keys are rejected' '*safe leaf*'
    Assert-Throws {
        Get-ApprovedJob `
            -Key 'not_approved' `
            -ResolvedCaseParent $temporaryRoot `
            -ResolvedSnapshotParent $temporaryRoot
    } 'unknown roots are rejected' '*17 approved*'

    $priorRetryDelay = $script:ReparseQueryDelayMilliseconds
    $script:ReparseQueryDelayMilliseconds = 0
    try {
        $script:TransientReparseCalls = 0
        $script:ReparseQueryOverride = {
            param($LiteralPath, $Attempt)
            ++$script:TransientReparseCalls
            if ($LiteralPath -ceq 'synthetic-link-8' -and $Attempt -eq 1) {
                return [pscustomobject]@{
                    ExitCode = 1
                    Output = @('The system cannot find the file specified.')
                }
            }
            return [pscustomobject]@{
                ExitCode = 0
                Output = @('Reparse Tag Value : 0xa000001d', "Synthetic path: $LiteralPath")
            }
        }
        $reparseHashes = @(1..8 | ForEach-Object {
            Get-ReparsePayloadHash -LiteralPath "synthetic-link-$_"
        })
        Assert-Equal 8 $reparseHashes.Count `
            'all eight LX-link payloads survive an eighth-query transient failure'
        Assert-Equal 9 $script:TransientReparseCalls `
            'the eighth LX-link query is retried once and only once'

        $script:PersistentReparseCalls = 0
        $script:ReparseQueryOverride = {
            param($LiteralPath, $Attempt)
            ++$script:PersistentReparseCalls
            return [pscustomobject]@{
                ExitCode = 2
                Output = @("persistent synthetic failure $Attempt")
            }
        }
        $retryErrorPattern = "*after $($script:ReparseQueryMaxAttempts) attempts*"
        Assert-Throws {
            Get-ReparsePayloadHash -LiteralPath 'synthetic-persistent-link'
        } 'persistent reparse lookup fails closed after bounded retries' $retryErrorPattern
        Assert-Equal $script:ReparseQueryMaxAttempts $script:PersistentReparseCalls `
            'persistent reparse lookup never exceeds its bounded attempt count'

        $script:WrongTagReparseCalls = 0
        $script:ReparseQueryOverride = {
            param($LiteralPath, $Attempt)
            ++$script:WrongTagReparseCalls
            return [pscustomobject]@{
                ExitCode = 0
                Output = @('Reparse Tag Value : 0xa000000c')
            }
        }
        Assert-Throws {
            Get-ReparsePayloadHash -LiteralPath 'synthetic-junction'
        } 'a successful fsutil query with a non-LX tag fails closed' '*Unexpected reparse tag*'
        Assert-Equal 1 $script:WrongTagReparseCalls `
            'a wrong reparse tag is a security failure, not a transient retry'
    } finally {
        $script:ReparseQueryOverride = $null
        $script:ReparseQueryDelayMilliseconds = $priorRetryDelay
    }

    $caseParent = Join-Path $temporaryRoot 'cases'
    $snapshotParent = Join-Path $temporaryRoot 'snapshots'
    $archiveRoot = Join-Path $temporaryRoot 'archive'
    foreach ($directory in @($caseParent, $snapshotParent, $archiveRoot)) {
        [IO.Directory]::CreateDirectory($directory) | Out-Null
    }

    $atomicParent = Join-Path $temporaryRoot 'atomic-move'
    $atomicSource = Join-Path $atomicParent 'source'
    $atomicDestination = Join-Path $atomicParent 'destination'
    [IO.Directory]::CreateDirectory($atomicSource) | Out-Null
    Write-TestFile -LiteralPath (Join-Path $atomicSource 'marker.txt') -Text 'atomic'
    Move-DirectoryAtomically `
        -Source $atomicSource `
        -Destination $atomicDestination `
        -Label 'Synthetic atomic move'
    Assert-True (-not (Test-Path -LiteralPath $atomicSource)) `
        'atomic directory rename removes the exact source path'
    Assert-True (Test-Path -LiteralPath (Join-Path $atomicDestination 'marker.txt') -PathType Leaf) `
        'atomic directory rename preserves the exact destination tree'
    [IO.Directory]::CreateDirectory($atomicSource) | Out-Null
    Assert-Throws {
        Move-DirectoryAtomically `
            -Source $atomicSource `
            -Destination $atomicDestination `
            -Label 'Synthetic collision move'
    } 'atomic directory rename refuses a pre-existing destination' '*destination must not exist*'

    $cleanGitRoot = Join-Path $temporaryRoot 'clean-git'
    [IO.Directory]::CreateDirectory($cleanGitRoot) | Out-Null
    & git -C $cleanGitRoot init --quiet --initial-branch=main
    & git -C $cleanGitRoot config user.name 'Archive Test'
    & git -C $cleanGitRoot config user.email 'archive-test@example.invalid'
    & git -C $cleanGitRoot config core.autocrlf false
    Write-TestFile -LiteralPath (Join-Path $cleanGitRoot 'tracked.txt') -Text 'tracked'
    & git -C $cleanGitRoot add -- tracked.txt
    & git -C $cleanGitRoot commit --quiet -m 'initial'
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not initialize clean Git regression repository'
    }
    $priorArchiveRoot = $script:ArchiveRoot
    try {
        $script:ArchiveRoot = [IO.Path]::GetFullPath($cleanGitRoot)
        $cleanStatusBytes = Invoke-ArchiveGitBytes -Arguments @(
            'status', '--porcelain=v1', '-z', '--untracked-files=all')
        Assert-True ($cleanStatusBytes -is [byte[]]) `
            'zero-length Git stdout remains a byte[] rather than collapsing to null'
        Assert-Equal 0 $cleanStatusBytes.Length `
            'clean Git porcelain output is an explicit zero-length byte array'
        $cleanRecords = @(Get-ArchiveGitStatusRecords)
        Assert-Equal 0 $cleanRecords.Count `
            'clean archive repository status returns an empty record set without error'
    } finally {
        $script:ArchiveRoot = $priorArchiveRoot
    }
    $script:GitHubGetOverride = {
        param($Uri, $Headers)
        return [pscustomobject]@{ content = '' }
    }
    try {
        $emptyRemoteBytes = Get-RemoteManifestBytes `
            -RepositorySlug $script:ExpectedRepositorySlug `
            -BranchName 'main' `
            -Headers @{}
        Assert-True ($emptyRemoteBytes -is [byte[]]) `
            'zero-length remote content remains a byte[] rather than collapsing to null'
        Assert-Equal 0 $emptyRemoteBytes.Length `
            'empty remote content has an explicit zero-length binary value'
    } finally {
        $script:GitHubGetOverride = $null
    }

    foreach ($name in @($script:ApprovedCases + $script:ActiveCaseName)) {
        [IO.Directory]::CreateDirectory((Join-Path $caseParent $name)) | Out-Null
    }
    foreach ($name in @($script:ApprovedSnapshots + $script:PreservedSnapshots)) {
        [IO.Directory]::CreateDirectory((Join-Path $snapshotParent $name)) | Out-Null
    }
    Assert-ApprovedParentLayout `
        -ResolvedCaseParent $caseParent `
        -ResolvedSnapshotParent $snapshotParent

    $targetName = 'lab_screening_export'
    $target = Join-Path $caseParent $targetName
    foreach ($relative in @('0/fluid', 'constant/polyMesh', 'system', 'provenance')) {
        [IO.Directory]::CreateDirectory((Join-Path $target $relative)) | Out-Null
    }
    Write-TestFile -LiteralPath (Join-Path $target '0/fluid/T') -Text 'temperature-field'
    Write-TestFile -LiteralPath (Join-Path $target '0/fluid/U') -Text 'velocity-field'
    Write-TestFile -LiteralPath (Join-Path $target 'constant/polyMesh/points') -Text 'mesh-points'
    Write-TestFile -LiteralPath (Join-Path $target 'system/controlDict') -Text 'control'
    Write-TestFile -LiteralPath (Join-Path $target 'provenance/source.json') -Text '{"test":true}'

    $job = Get-ApprovedJob `
        -Key $targetName `
        -ResolvedCaseParent $caseParent `
        -ResolvedSnapshotParent $snapshotParent
    Assert-Equal 'sibling_time0_case' $job.Category 'approved case category is fixed'
    Assert-Equal 'openfoam-case-lab-screening-export-time0-20260826.tar.zst' `
        $job.AssetName 'release filename is deterministic and not caller-controlled'
    Assert-CaseTargetLayout -Job $job
    $inventory = Get-SourceInventory -Job $job -DeepHash $true
    Assert-Equal 5 $inventory.SourceFileCount 'deep inventory counts all source files'
    Assert-True ($inventory.SourceBytes -gt 0) 'deep inventory counts source bytes'
    Assert-True ($inventory.SourceFingerprintSha256 -match '^[0-9a-f]{64}$') `
        'deep inventory produces a SHA-256 fingerprint'

    $gatePaths = Get-DeletionPaths -ArchiveRoot $archiveRoot -Job $job
    [IO.Directory]::CreateDirectory($gatePaths.Root) | Out-Null
    $foreignJournal = Join-Path $gatePaths.Root 'foreign-asset.delete.json'
    [IO.File]::WriteAllText($foreignJournal, '{}')
    Assert-Throws {
        Assert-NoForeignDeletionState -ArchiveRoot $archiveRoot -Job $job
    } 'a foreign incomplete journal blocks a fresh asset archive' '*Another recovery/deletion artifact*'
    [IO.File]::Delete($foreignJournal)
    Assert-NoForeignDeletionState -ArchiveRoot $archiveRoot -Job $job
    Assert-True $true `
        'fresh archive gate accepts an empty deletion-state root'

    [IO.Directory]::CreateDirectory((Join-Path $caseParent 'unexpected_case')) | Out-Null
    Assert-Throws {
        Assert-ApprovedParentLayout `
            -ResolvedCaseParent $caseParent `
            -ResolvedSnapshotParent $snapshotParent
    } 'unexpected sibling roots fail closed' '*Unexpected OpenFOAM case*'
    Remove-Item -LiteralPath (Join-Path $caseParent 'unexpected_case') -Recurse -Force

    $staging = New-OrResumeStagedArchive `
        -Job $job `
        -Inventory $inventory `
        -ArchiveRoot $archiveRoot
    Assert-True (Test-Path -LiteralPath $staging.Paths.Asset -PathType Leaf) `
        'one deterministic staged archive is created'
    Assert-True (Test-Path -LiteralPath $staging.Paths.Metadata -PathType Leaf) `
        'durable staged metadata is created'
    Assert-True (-not $staging.Resumed) 'first staging pass is not marked resumed'
    $secondStaging = New-OrResumeStagedArchive `
        -Job $job `
        -Inventory $inventory `
        -ArchiveRoot $archiveRoot
    Assert-True $secondStaging.Resumed 'exact staged archive resumes idempotently'
    Assert-Equal $staging.Metadata.asset_sha256 $secondStaging.Metadata.asset_sha256 `
        'resumed archive retains exact SHA-256'
    Assert-Equal $inventory.EntryCount $staging.Metadata.tar_members `
        'verified tar members exactly match the source inventory'

    # Regression: libarchive stores these 101-byte paths as a USTAR name plus
    # prefix inside a PAX archive. .NET TarReader drops that prefix unless the
    # verifier reconciles it against the independently verified native listing.
    $longRootName = 'new_model_airflow_mapping_40mm'
    $longRoot = Join-Path $caseParent $longRootName
    foreach ($relative in @('0', 'constant', 'system')) {
        [IO.Directory]::CreateDirectory((Join-Path $longRoot $relative)) | Out-Null
    }
    Write-TestFile `
        -LiteralPath (Join-Path $longRoot '0/seed') `
        -Text 'seed'
    $longLeaf = 'topoSetDict_external_Eaton_SU3000RTXLCD2UTAA_UPS_Front_intake_1'
    $longRelative = "$longRootName/system/$longLeaf"
    Assert-Equal 101 $longRelative.Length `
        'PAX/USTAR-prefix regression path retains the real failing length'
    Write-TestFile `
        -LiteralPath (Join-Path $longRoot "system/$longLeaf") `
        -Text 'long-path-regression'
    $longJob = Get-ApprovedJob `
        -Key $longRootName `
        -ResolvedCaseParent $caseParent `
        -ResolvedSnapshotParent $snapshotParent
    $longInventory = Get-SourceInventory -Job $longJob -DeepHash $true
    $longStalePaths = Get-StagingPaths -ArchiveRoot $archiveRoot -Job $longJob
    [IO.Directory]::CreateDirectory($longStalePaths.Root) | Out-Null
    [IO.File]::WriteAllText($longStalePaths.Partial, 'stale-pilot-partial')
    [IO.File]::WriteAllText($longStalePaths.List, 'stale-pilot-list')
    $longStaging = New-OrResumeStagedArchive `
        -Job $longJob `
        -Inventory $longInventory `
        -ArchiveRoot $archiveRoot
    Assert-True (Test-Path -LiteralPath $longStaging.Paths.Asset -PathType Leaf) `
        'real 101-byte PAX/USTAR-prefix filename stages and verifies'
    Assert-Equal $longInventory.EntryCount $longStaging.Metadata.tar_members `
        'PAX/USTAR-prefix regression archive retains every exact member'
    Assert-True (-not (Test-Path -LiteralPath $longStalePaths.List)) `
        'pilot retry removes only its stale derived list after a verified rebuild'
    Assert-True ((Get-Item -LiteralPath $longStaging.Paths.Asset).Length -gt 20) `
        'pilot retry replaces its stale partial with a real verified archive'

    $zeroSnapshotName = 'dt_baseline_0p0005_case'
    $zeroSnapshotRoot = Join-Path $snapshotParent $zeroSnapshotName
    Assert-Equal $script:EmptySha256 `
        (Get-TarRegularFileSha256 `
            -Entry ([pscustomobject]@{ Length = [int64]0; DataStream = $null }) `
            -Name 'empty.foam') `
        'null tar payload stream is accepted only for a declared empty file'
    Assert-Throws {
        Get-TarRegularFileSha256 `
            -Entry ([pscustomobject]@{ Length = [int64]1; DataStream = $null }) `
            -Name 'nonempty.foam'
    } 'null tar payload stream remains fatal for a nonempty file' '*Nonempty tar regular file lacks a data stream*'
    [IO.Directory]::CreateDirectory((Join-Path $zeroSnapshotRoot 'system')) | Out-Null
    $zeroMarker = Join-Path $zeroSnapshotRoot "$zeroSnapshotName.foam"
    [IO.File]::WriteAllBytes($zeroMarker, [byte[]]::new(0))
    Write-TestFile `
        -LiteralPath (Join-Path $zeroSnapshotRoot 'system/controlDict') `
        -Text 'zero-marker-control'
    $zeroJob = Get-ApprovedJob `
        -Key $zeroSnapshotName `
        -ResolvedCaseParent $caseParent `
        -ResolvedSnapshotParent $snapshotParent
    $zeroInventory = Get-SourceInventory -Job $zeroJob -DeepHash $true
    $zeroEntry = $zeroInventory.Entries | Where-Object {
        $_.ArchivePath -ceq "$zeroSnapshotName/$zeroSnapshotName.foam"
    } | Select-Object -First 1
    Assert-Equal 0 $zeroEntry.Length `
        'legitimate OpenFOAM marker is inventoried as a zero-byte regular file'
    Assert-Equal $script:EmptySha256 $zeroEntry.Sha256 `
        'zero-byte source marker has the canonical empty SHA-256'
    $zeroStalePaths = Get-StagingPaths -ArchiveRoot $archiveRoot -Job $zeroJob
    [IO.Directory]::CreateDirectory($zeroStalePaths.Root) | Out-Null
    [IO.File]::WriteAllText($zeroStalePaths.Partial, 'stale-zero-marker-partial')
    [IO.File]::WriteAllText($zeroStalePaths.List, 'stale-zero-marker-list')
    $zeroStaging = New-OrResumeStagedArchive `
        -Job $zeroJob `
        -Inventory $zeroInventory `
        -ArchiveRoot $archiveRoot
    Assert-True (-not (Test-Path -LiteralPath $zeroStalePaths.List)) `
        'zero-marker pilot retry removes its stale derived member list'
    $zeroMetadataEntry = $zeroStaging.Metadata.entries | Where-Object {
        $_.path -ceq "$zeroSnapshotName/$zeroSnapshotName.foam"
    } | Select-Object -First 1
    Assert-Equal $script:EmptySha256 $zeroMetadataEntry.sha256 `
        'verified archive records canonical empty SHA-256 for the marker'
    Assert-Throws {
        Remove-SourceRootSafely `
            -Job $zeroJob `
            -Inventory $zeroInventory `
            -ArchiveRoot $archiveRoot `
            -StagedAsset $zeroStaging.Paths.Asset `
            -Metadata $zeroStaging.Metadata `
            -RemoteCommit ('d' * 40) `
            -DeletionAuthorized $true `
            -RemoteAssetVerified $true `
            -RemoteManifestVerified $true `
            -InjectPurgeFailureAfterDamage
    } 'zero-marker quarantine damage triggers verified archive rollback' '*restored from the verified archive*'
    $zeroRestored = Get-SourceInventory -Job $zeroJob -DeepHash $true
    Assert-Equal $zeroInventory.SourceFingerprintSha256 `
        $zeroRestored.SourceFingerprintSha256 `
        'zero-marker rollback restores the complete source fingerprint'
    Assert-Equal 0 (Get-Item -LiteralPath $zeroMarker -Force).Length `
        'zero-marker rollback restores the legitimate empty file'
    $retainedJournalPaths = New-TestDeletionJournal `
        -Job $zeroJob `
        -ArchiveRoot $archiveRoot `
        -Staging $zeroStaging `
        -SourceFingerprint $zeroInventory.SourceFingerprintSha256 `
        -Phase prepared
    $retainedInspection = Restore-PriorDeletionJournal `
        -Job $zeroJob `
        -ArchiveRoot $archiveRoot `
        -InspectOnly
    Assert-Equal 'SourceRetained' $retainedInspection.State `
        'non-mutating journal inspection recognizes an already restored source'
    Assert-True (Test-Path -LiteralPath $retainedJournalPaths.Receipt -PathType Leaf) `
        'source-retained inspection leaves its durable journal untouched'
    $retainedClear = Restore-PriorDeletionJournal `
        -Job $zeroJob `
        -ArchiveRoot $archiveRoot
    Assert-Equal 'JournalCleared' $retainedClear.State `
        'verified retained-source journal can be finalized locally'
    Assert-True (Test-Path -LiteralPath $zeroSnapshotRoot -PathType Container) `
        'retained-source journal finalization never continues into deletion'
    Assert-True (Test-Path -LiteralPath $zeroStaging.Paths.Asset -PathType Leaf) `
        'retained-source journal finalization preserves staged archive evidence'

    $persistentName = 'thermal_lab_corrected_curves'
    $persistentRoot = Join-Path $caseParent $persistentName
    foreach ($relative in @('0', 'constant', 'system', 'provenance')) {
        [IO.Directory]::CreateDirectory((Join-Path $persistentRoot $relative)) | Out-Null
    }
    Write-TestFile `
        -LiteralPath (Join-Path $persistentRoot '0/T') `
        -Text 'persistent-temperature'
    Write-TestFile `
        -LiteralPath (Join-Path $persistentRoot 'system/controlDict') `
        -Text 'persistent-control'
    Write-TestFile `
        -LiteralPath (Join-Path $persistentRoot 'provenance/source.json') `
        -Text '{"recovery":"persistent"}'
    $persistentJob = Get-ApprovedJob `
        -Key $persistentName `
        -ResolvedCaseParent $caseParent `
        -ResolvedSnapshotParent $snapshotParent
    $persistentInventory = Get-SourceInventory -Job $persistentJob -DeepHash $true
    $persistentStaging = New-OrResumeStagedArchive `
        -Job $persistentJob `
        -Inventory $persistentInventory `
        -ArchiveRoot $archiveRoot
    $persistentPaths = Get-DeletionPaths -ArchiveRoot $archiveRoot -Job $persistentJob
    [IO.Directory]::CreateDirectory($persistentPaths.Container) | Out-Null
    Move-DirectoryAtomically `
        -Source $persistentRoot `
        -Destination $persistentPaths.Group `
        -Label 'Synthetic persistent-failure quarantine'
    Write-TestFile `
        -LiteralPath (Join-Path $persistentPaths.Group 'system/controlDict') `
        -Text 'damaged-quarantine-copy'
    $persistentPaths = New-TestDeletionJournal `
        -Job $persistentJob `
        -ArchiveRoot $archiveRoot `
        -Staging $persistentStaging `
        -SourceFingerprint $persistentInventory.SourceFingerprintSha256
    Assert-Throws {
        Restore-PriorDeletionJournal `
            -Job $persistentJob `
            -ArchiveRoot $archiveRoot `
            -RecoveryAuthorized `
            -RemoteAssetVerified $true `
            -RemoteManifestVerified $true `
            -AllowInvalidQuarantineArchiveFallback `
            -RestoreCandidateProbe {
                param($CandidateRoot, $ExtractionRoot)
                [IO.File]::WriteAllText(
                    (Join-Path $CandidateRoot 'system/controlDict'),
                    'damaged-extracted-candidate',
                    [Text.UTF8Encoding]::new($false))
            }
    } 'persistent extraction validation failure remains fail-closed' '*preserve journal, quarantine, and candidate*'
    $failedCandidates = @(
        Get-ChildItem -LiteralPath $persistentPaths.Container -Directory -Force |
            Where-Object Name -Match '^\.restore\.[0-9a-f]{32}$')
    Assert-True (-not (Test-Path -LiteralPath $persistentRoot)) `
        'failed recovery does not expose an unverified source candidate'
    Assert-True (Test-Path -LiteralPath $persistentPaths.Group -PathType Container) `
        'failed recovery preserves the damaged quarantine copy'
    Assert-Equal 1 $failedCandidates.Count `
        'failed recovery preserves its extracted candidate tree'
    Assert-True (Test-Path -LiteralPath $persistentPaths.Receipt -PathType Leaf) `
        'failed recovery preserves the durable deletion journal'
    Assert-True (Test-Path -LiteralPath $persistentStaging.Paths.Asset -PathType Leaf) `
        'failed recovery preserves the verified staged archive'
    Assert-True (Test-Path -LiteralPath $persistentStaging.Paths.Metadata -PathType Leaf) `
        'failed recovery preserves durable staged metadata'
    $persistentInspection = Restore-PriorDeletionJournal `
        -Job $persistentJob `
        -ArchiveRoot $archiveRoot `
        -InspectOnly
    Assert-Equal 'NeedsRecovery' $persistentInspection.State `
        'failed recovery remains explicitly resumable without mutation'
    Assert-True $persistentInspection.QuarantineGroupPresent `
        'resumption inspection reports the preserved quarantine copy'
    Assert-Throws {
        Restore-PriorDeletionJournal `
            -Job $persistentJob `
            -ArchiveRoot $archiveRoot `
            -RecoveryAuthorized `
            -RemoteAssetVerified $true `
            -RemoteManifestVerified $true `
            -AllowInvalidQuarantineArchiveFallback
    } 'an invalid persisted candidate is preserved without another extraction' '*failed deep validation*'
    $stillFailedCandidates = @(
        Get-ChildItem -LiteralPath $persistentPaths.Container -Directory -Force |
            Where-Object Name -CMatch '^\.restore\.[0-9a-f]{32}$')
    Assert-Equal 1 $stillFailedCandidates.Count `
        'invalid-candidate retry does not accumulate another extraction tree'
    Assert-True (Test-Path -LiteralPath $persistentPaths.Receipt -PathType Leaf) `
        'invalid-candidate retry retains its journal for delayed visibility'
    Write-TestFile `
        -LiteralPath (Join-Path $failedCandidates[0].FullName "$persistentName/system/controlDict") `
        -Text 'persistent-control'
    $persistentResume = Restore-PriorDeletionJournal `
        -Job $persistentJob `
        -ArchiveRoot $archiveRoot `
        -RecoveryAuthorized `
        -RemoteAssetVerified $true `
        -RemoteManifestVerified $true `
        -AllowInvalidQuarantineArchiveFallback
    Assert-Equal 1 @($persistentResume).Count `
        'resumed recovery returns one state object without accidental helper output'
    Assert-Equal 'SourcesRestored' $persistentResume.State `
        'a later verified recovery resumes from the retained journal'
    Assert-Equal 'preserved_candidate' $persistentResume.RestoreMethod `
        'resumed recovery atomically adopts the now-valid persisted candidate'
    $persistentRestored = Get-SourceInventory -Job $persistentJob -DeepHash $true
    Assert-Equal $persistentInventory.SourceFingerprintSha256 `
        $persistentRestored.SourceFingerprintSha256 `
        'resumed recovery restores the exact archived source fingerprint'
    Assert-True (-not (Test-Path -LiteralPath $persistentPaths.Receipt)) `
        'successful resumed recovery clears its completed local journal'
    Assert-True (-not (Test-Path -LiteralPath $persistentPaths.Container)) `
        'recovery artifacts are removed only after final source verification'
    Assert-True (Test-Path -LiteralPath $persistentStaging.Paths.Asset -PathType Leaf) `
        'recovery retains the verified staged archive and cannot continue deletion'

    $donorName = 'thermal_lab_fan_axial'
    $donorRoot = Join-Path $caseParent $donorName
    foreach ($relative in @('0', 'constant', 'system', 'provenance')) {
        [IO.Directory]::CreateDirectory((Join-Path $donorRoot $relative)) | Out-Null
    }
    Write-TestFile -LiteralPath (Join-Path $donorRoot '0/T') -Text 'donor-temperature'
    Write-TestFile `
        -LiteralPath (Join-Path $donorRoot 'system/controlDict') `
        -Text 'donor-control'
    Write-TestFile `
        -LiteralPath (Join-Path $donorRoot 'provenance/source.json') `
        -Text '{"recovery":"manual-probe"}'
    $donorJob = Get-ApprovedJob `
        -Key $donorName `
        -ResolvedCaseParent $caseParent `
        -ResolvedSnapshotParent $snapshotParent
    $donorInventory = Get-SourceInventory -Job $donorJob -DeepHash $true
    $donorStaging = New-OrResumeStagedArchive `
        -Job $donorJob `
        -Inventory $donorInventory `
        -ArchiveRoot $archiveRoot
    $donorPaths = Get-DeletionPaths -ArchiveRoot $archiveRoot -Job $donorJob
    [IO.Directory]::CreateDirectory($donorPaths.Container) | Out-Null
    $badDonorCandidate = Join-Path $donorPaths.Container ('.restore.' + ('b' * 32))
    [IO.Directory]::CreateDirectory($badDonorCandidate) | Out-Null
    Copy-Item -LiteralPath $donorRoot -Destination $badDonorCandidate -Recurse -Force
    Write-TestFile `
        -LiteralPath (Join-Path $badDonorCandidate "$donorName/system/controlDict") `
        -Text 'ghost-candidate-control'
    $manualProbeRoot = Join-Path $donorPaths.Root (
        '.manual-probe-synthetic-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($manualProbeRoot) | Out-Null
    $manualProbeGroup = Join-Path $manualProbeRoot $donorName
    Move-DirectoryAtomically `
        -Source $donorRoot `
        -Destination $manualProbeGroup `
        -Label 'Synthetic verified manual probe'
    $donorPaths = New-TestDeletionJournal `
        -Job $donorJob `
        -ArchiveRoot $archiveRoot `
        -Staging $donorStaging `
        -SourceFingerprint $donorInventory.SourceFingerprintSha256
    Assert-Throws {
        Restore-PriorDeletionJournal `
            -Job $donorJob `
            -ArchiveRoot $archiveRoot `
            -RecoveryAuthorized `
            -RemoteAssetVerified $true `
            -RemoteManifestVerified $true
    } 'bad exact-container candidate is rejected while foreign manual probe is retained' '*failed deep validation*'
    Assert-True (Test-Path -LiteralPath $manualProbeGroup -PathType Container) `
        'foreign verified manual probe is ignored and preserved until explicitly staged'
    Assert-True (Test-Path -LiteralPath $badDonorCandidate -PathType Container) `
        'bad exact-container candidate remains available for audit'
    Move-DirectoryAtomically `
        -Source $manualProbeGroup `
        -Destination $donorPaths.Group `
        -Label 'Synthetic manual probe staging'
    $donorRecovery = Restore-PriorDeletionJournal `
        -Job $donorJob `
        -ArchiveRoot $archiveRoot `
        -RecoveryAuthorized `
        -RemoteAssetVerified $true `
        -RemoteManifestVerified $true `
        -CandidateScanProbe {
            throw 'candidate scan must not run when exact quarantine group is valid'
        }
    Assert-Equal 'SourcesRestored' $donorRecovery.State `
        'staged verified manual probe restores the absent source'
    Assert-Equal 'quarantine' $donorRecovery.RestoreMethod `
        'verified manual probe is adopted through the exact quarantine group'
    $donorRestored = Get-SourceInventory -Job $donorJob -DeepHash $true
    Assert-Equal $donorInventory.SourceFingerprintSha256 `
        $donorRestored.SourceFingerprintSha256 `
        'manual-probe recovery passes the final source fingerprint'
    Assert-True (-not (Test-Path -LiteralPath $donorPaths.Container)) `
        'bad candidate is cleaned only after the replacement source fully verifies'
    Assert-True (Test-Path -LiteralPath $manualProbeRoot -PathType Container) `
        'manual-probe sibling itself remains outside exact per-asset cleanup'
    Assert-True (Test-Path -LiteralPath $donorStaging.Paths.Asset -PathType Leaf) `
        'manual-probe recovery retains staged archive evidence'
    [IO.Directory]::Delete($manualProbeRoot)

    $hostileRoot = Join-Path $archiveRoot 'hostile-preserved-candidate'
    [IO.Directory]::CreateDirectory($hostileRoot) | Out-Null
    $hostileFile = Join-Path $hostileRoot ('.restore.' + ('c' * 32))
    [IO.File]::WriteAllText($hostileFile, 'not-a-directory')
    Assert-Throws {
        Get-PreservedRecoveryCandidateState `
            -Job $persistentJob `
            -Metadata $persistentStaging.Metadata `
            -QuarantineRoot $hostileRoot `
            -QuarantineGroup (Join-Path $hostileRoot $persistentName)
    } 'file masquerading as an exact recovery candidate fails closed' '*not an ordinary directory*'
    Assert-True (Test-Path -LiteralPath $hostileFile -PathType Leaf) `
        'hostile candidate file is preserved without mutation'
    Remove-Item -LiteralPath $hostileRoot -Recurse -Force

    $hostileRoot = Join-Path $archiveRoot 'hostile-preserved-candidate-root'
    $hostileWrapper = Join-Path $hostileRoot ('.restore.' + ('d' * 32))
    [IO.Directory]::CreateDirectory((Join-Path $hostileWrapper 'unexpected-root')) | Out-Null
    Assert-Throws {
        Get-PreservedRecoveryCandidateState `
            -Job $persistentJob `
            -Metadata $persistentStaging.Metadata `
            -QuarantineRoot $hostileRoot `
            -QuarantineGroup (Join-Path $hostileRoot $persistentName)
    } 'candidate wrapper with a foreign root fails closed' '*unexpected root*'
    Assert-True (Test-Path -LiteralPath $hostileWrapper -PathType Container) `
        'hostile candidate wrapper is preserved for audit'
    Remove-Item -LiteralPath $hostileRoot -Recurse -Force

    $selectionParent = Join-Path $temporaryRoot 'preserved-selection-parent'
    $selectionRoot = Join-Path $archiveRoot 'preserved-selection'
    [IO.Directory]::CreateDirectory($selectionParent) | Out-Null
    [IO.Directory]::CreateDirectory($selectionRoot) | Out-Null
    foreach ($suffix in @('e', 'f')) {
        $wrapper = Join-Path $selectionRoot ('.restore.' + ($suffix * 32))
        [IO.Directory]::CreateDirectory($wrapper) | Out-Null
        Copy-Item -LiteralPath $persistentRoot -Destination $wrapper -Recurse -Force
    }
    Write-TestFile `
        -LiteralPath (Join-Path $selectionRoot (
            '.restore.' + ('f' * 32) + "/$persistentName/system/controlDict")) `
        -Text 'invalid-selection-candidate'
    $selectionJob = [pscustomobject]@{
        ParentRoot = $selectionParent
        RelativeRoot = $persistentName
        AllowedReparseHashes = @{}
    }
    $selectedRecovery = Restore-SourceRootFromArchive `
        -Job $selectionJob `
        -StagedAsset $persistentStaging.Paths.Asset `
        -Metadata $persistentStaging.Metadata `
        -QuarantineRoot $selectionRoot `
        -QuarantineGroup (Join-Path $selectionRoot $persistentName)
    Assert-Equal 'preserved_candidate' $selectedRecovery.Method `
        'exactly one matching candidate is selected despite one invalid sibling'
    $selectedInventory = Get-SourceInventory -Job $selectionJob -DeepHash $true
    Assert-Equal $persistentInventory.SourceFingerprintSha256 `
        $selectedInventory.SourceFingerprintSha256 `
        'selected persisted candidate passes the final source fingerprint'
    Assert-True (-not (Test-Path -LiteralPath $selectionRoot)) `
        'invalid sibling is cleaned only after selected source verification'

    $invalidGroupParent = Join-Path $temporaryRoot 'invalid-group-priority-parent'
    $invalidGroupRoot = Join-Path $archiveRoot 'invalid-group-priority'
    [IO.Directory]::CreateDirectory($invalidGroupParent) | Out-Null
    [IO.Directory]::CreateDirectory($invalidGroupRoot) | Out-Null
    Copy-Item -LiteralPath $persistentRoot -Destination $invalidGroupRoot -Recurse -Force
    Write-TestFile `
        -LiteralPath (Join-Path $invalidGroupRoot "$persistentName/system/controlDict") `
        -Text 'invalid-exact-group'
    $validFallbackWrapper = Join-Path $invalidGroupRoot ('.restore.' + ('9' * 32))
    [IO.Directory]::CreateDirectory($validFallbackWrapper) | Out-Null
    Copy-Item -LiteralPath $persistentRoot -Destination $validFallbackWrapper -Recurse -Force
    $invalidGroupJob = [pscustomobject]@{
        ParentRoot = $invalidGroupParent
        RelativeRoot = $persistentName
        AllowedReparseHashes = @{}
    }
    Assert-Throws {
        Restore-SourceRootFromArchive `
            -Job $invalidGroupJob `
            -StagedAsset $persistentStaging.Paths.Asset `
            -Metadata $persistentStaging.Metadata `
            -QuarantineRoot $invalidGroupRoot `
            -QuarantineGroup (Join-Path $invalidGroupRoot $persistentName)
    } 'invalid exact quarantine group blocks otherwise valid candidate fallback' '*candidate fallback is forbidden*'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $invalidGroupParent $persistentName))) `
        'invalid exact group never allows candidate promotion'
    Assert-True (Test-Path -LiteralPath $validFallbackWrapper -PathType Container) `
        'candidate remains preserved when exact quarantine group is invalid'

    $conflictParent = Join-Path $temporaryRoot 'preserved-conflict-parent'
    $conflictRoot = Join-Path $archiveRoot 'preserved-conflict'
    [IO.Directory]::CreateDirectory($conflictParent) | Out-Null
    [IO.Directory]::CreateDirectory($conflictRoot) | Out-Null
    foreach ($suffix in @('1', '2')) {
        $wrapper = Join-Path $conflictRoot ('.restore.' + ($suffix * 32))
        [IO.Directory]::CreateDirectory($wrapper) | Out-Null
        Copy-Item -LiteralPath $persistentRoot -Destination $wrapper -Recurse -Force
    }
    $conflictJob = [pscustomobject]@{
        ParentRoot = $conflictParent
        RelativeRoot = $persistentName
        AllowedReparseHashes = @{}
    }
    Assert-Throws {
        Restore-SourceRootFromArchive `
            -Job $conflictJob `
            -StagedAsset $persistentStaging.Paths.Asset `
            -Metadata $persistentStaging.Metadata `
            -QuarantineRoot $conflictRoot `
            -QuarantineGroup (Join-Path $conflictRoot $persistentName)
    } 'multiple valid preserved candidates fail closed without selection' '*Multiple valid preserved recovery candidates*'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $conflictParent $persistentName))) `
        'multiple-candidate conflict never exposes a source root'
    Assert-Equal 2 @(
        Get-ChildItem -LiteralPath $conflictRoot -Directory -Force
    ).Count 'multiple valid candidates remain preserved after conflict'

    $staleName = 'thermal_lab_fan_axial_intersection'
    $staleRoot = Join-Path $caseParent $staleName
    foreach ($relative in @('0', 'constant', 'system', 'provenance')) {
        [IO.Directory]::CreateDirectory((Join-Path $staleRoot $relative)) | Out-Null
    }
    Write-TestFile -LiteralPath (Join-Path $staleRoot '0/T') -Text 'stale-temperature'
    Write-TestFile `
        -LiteralPath (Join-Path $staleRoot 'system/controlDict') `
        -Text 'stale-control'
    $staleJob = Get-ApprovedJob `
        -Key $staleName `
        -ResolvedCaseParent $caseParent `
        -ResolvedSnapshotParent $snapshotParent
    $staleInventory = Get-SourceInventory -Job $staleJob -DeepHash $true
    $staleStaging = New-OrResumeStagedArchive `
        -Job $staleJob `
        -Inventory $staleInventory `
        -ArchiveRoot $archiveRoot
    $stalePaths = Get-DeletionPaths -ArchiveRoot $archiveRoot -Job $staleJob
    [IO.Directory]::CreateDirectory($stalePaths.Container) | Out-Null
    Copy-Item -LiteralPath $staleRoot -Destination $stalePaths.Container -Recurse -Force
    Write-TestFile `
        -LiteralPath (Join-Path $stalePaths.Group 'system/controlDict') `
        -Text 'stale-group-copy'
    $staleCandidate = Join-Path $stalePaths.Container ('.restore.' + ('a' * 32))
    [IO.Directory]::CreateDirectory($staleCandidate) | Out-Null
    Copy-Item -LiteralPath $staleRoot -Destination $staleCandidate -Recurse -Force
    $externalTarget = Join-Path $temporaryRoot 'hostile-junction-target'
    [IO.Directory]::CreateDirectory($externalTarget) | Out-Null
    $externalSentinel = Join-Path $externalTarget 'must-survive.txt'
    Write-TestFile -LiteralPath $externalSentinel -Text 'outside-candidate'
    $hostileJunction = Join-Path $staleCandidate "$staleName/system/hostile-junction"
    $null = New-Item `
        -ItemType Junction `
        -Path $hostileJunction `
        -Target $externalTarget `
        -Force
    $stalePaths = New-TestDeletionJournal `
        -Job $staleJob `
        -ArchiveRoot $archiveRoot `
        -Staging $staleStaging `
        -SourceFingerprint $staleInventory.SourceFingerprintSha256
    $staleInspection = Restore-PriorDeletionJournal `
        -Job $staleJob `
        -ArchiveRoot $archiveRoot `
        -InspectOnly
    Assert-Equal 'SourceRetained' $staleInspection.State `
        'source-plus-stale-group crash state is recognized after source fingerprint verification'
    Assert-True $staleInspection.QuarantineGroupPresent `
        'source-retained inspection reports its stale quarantine group'
    Assert-Throws {
        Restore-PriorDeletionJournal -Job $staleJob -ArchiveRoot $archiveRoot
    } 'unexpected nested reparse blocks no-follow stale-artifact cleanup' '*Unexpected nested reparse point*'
    Assert-True (Test-Path -LiteralPath $staleRoot -PathType Container) `
        'hostile cleanup refusal preserves the verified source'
    Assert-True (Test-Path -LiteralPath $stalePaths.Receipt -PathType Leaf) `
        'hostile cleanup refusal preserves the durable journal'
    Assert-True (Test-Path -LiteralPath $externalSentinel -PathType Leaf) `
        'no-follow cleanup never traverses or deletes the junction target'
    [IO.Directory]::Delete($hostileJunction)
    $staleCleanupResume = Restore-PriorDeletionJournal `
        -Job $staleJob `
        -ArchiveRoot $archiveRoot
    Assert-Equal 'JournalCleared' $staleCleanupResume.State `
        'stale-artifact cleanup resumes after the hostile link is removed'
    Assert-True (-not (Test-Path -LiteralPath $stalePaths.Container)) `
        'resumed stale-artifact cleanup removes its exact container'
    Assert-True (Test-Path -LiteralPath $externalSentinel -PathType Leaf) `
        'external junction target remains intact after cleanup resume'

    $emptyRecoveryName = 'dt_variant_0p00075_case'
    $emptyRecoveryRoot = Join-Path $snapshotParent $emptyRecoveryName
    [IO.Directory]::CreateDirectory((Join-Path $emptyRecoveryRoot 'system')) | Out-Null
    Write-TestFile `
        -LiteralPath (Join-Path $emptyRecoveryRoot 'system/controlDict') `
        -Text 'empty-quarantine-control'
    [IO.File]::WriteAllBytes(
        (Join-Path $emptyRecoveryRoot "$emptyRecoveryName.foam"),
        [byte[]]::new(0))
    $emptyRecoveryJob = Get-ApprovedJob `
        -Key $emptyRecoveryName `
        -ResolvedCaseParent $caseParent `
        -ResolvedSnapshotParent $snapshotParent
    $emptyRecoveryInventory = Get-SourceInventory -Job $emptyRecoveryJob -DeepHash $true
    $emptyRecoveryStaging = New-OrResumeStagedArchive `
        -Job $emptyRecoveryJob `
        -Inventory $emptyRecoveryInventory `
        -ArchiveRoot $archiveRoot
    $emptyRecoveryPaths = Get-DeletionPaths `
        -ArchiveRoot $archiveRoot `
        -Job $emptyRecoveryJob
    [IO.Directory]::CreateDirectory($emptyRecoveryPaths.Container) | Out-Null
    Remove-Item -LiteralPath $emptyRecoveryRoot -Recurse -Force
    $emptyRecoveryPaths = New-TestDeletionJournal `
        -Job $emptyRecoveryJob `
        -ArchiveRoot $archiveRoot `
        -Staging $emptyRecoveryStaging `
        -SourceFingerprint $emptyRecoveryInventory.SourceFingerprintSha256
    $emptyInspection = Restore-PriorDeletionJournal `
        -Job $emptyRecoveryJob `
        -ArchiveRoot $archiveRoot `
        -InspectOnly
    Assert-Equal 'NeedsRecovery' $emptyInspection.State `
        'source-absent empty quarantine is detected without mutation'
    Assert-True (-not $emptyInspection.QuarantineGroupPresent) `
        'empty-quarantine inspection records that no source copy remains there'
    Assert-Throws {
        Restore-PriorDeletionJournal `
            -Job $emptyRecoveryJob `
            -ArchiveRoot $archiveRoot
    } 'empty-quarantine restoration requires explicit verified authorization' '*explicit Recover mode*'
    Assert-True (Test-Path -LiteralPath $emptyRecoveryPaths.Receipt -PathType Leaf) `
        'unauthorized recovery attempt preserves its journal'
    Assert-True (-not (Test-Path -LiteralPath $emptyRecoveryRoot)) `
        'unauthorized recovery attempt never publishes a candidate as source'
    $emptyRecovery = Restore-PriorDeletionJournal `
        -Job $emptyRecoveryJob `
        -ArchiveRoot $archiveRoot `
        -RecoveryAuthorized `
        -RemoteAssetVerified $true `
        -RemoteManifestVerified $true
    Assert-Equal 'SourcesRestored' $emptyRecovery.State `
        'verified archive restores a source-absent empty-quarantine journal'
    $emptyRecoveredInventory = Get-SourceInventory -Job $emptyRecoveryJob -DeepHash $true
    Assert-Equal $emptyRecoveryInventory.SourceFingerprintSha256 `
        $emptyRecoveredInventory.SourceFingerprintSha256 `
        'empty-quarantine recovery publishes only a fully verified source'
    Assert-True (-not (Test-Path -LiteralPath $emptyRecoveryPaths.Receipt)) `
        'successful empty-quarantine recovery clears its local journal'
    Assert-True (Test-Path -LiteralPath $emptyRecoveryStaging.Paths.Asset -PathType Leaf) `
        'empty-quarantine recovery retains staged archive evidence'

    $validAsset = New-TestAsset `
        -Id 1 `
        -Name $job.AssetName `
        -Bytes ([int64]$staging.Metadata.asset_bytes) `
        -Sha ([string]$staging.Metadata.asset_sha256)
    Assert-RemoteAssetMatches `
        -Asset $validAsset `
        -ExpectedBytes ([int64]$staging.Metadata.asset_bytes) `
        -ExpectedSha256 ([string]$staging.Metadata.asset_sha256)
    Assert-True $true 'remote asset accepts exact bytes, digest, and uploaded state'
    $wrongBytes = $validAsset.PSObject.Copy()
    $wrongBytes.size = [int64]$wrongBytes.size + 1
    Assert-Throws {
        Assert-RemoteAssetMatches `
            -Asset $wrongBytes `
            -ExpectedBytes ([int64]$staging.Metadata.asset_bytes) `
            -ExpectedSha256 ([string]$staging.Metadata.asset_sha256)
    } 'remote byte mismatch fails closed' '*byte mismatch*'
    $wrongDigest = $validAsset.PSObject.Copy()
    $wrongDigest.digest = 'sha256:' + ('0' * 64)
    Assert-Throws {
        Assert-RemoteAssetMatches `
            -Asset $wrongDigest `
            -ExpectedBytes ([int64]$staging.Metadata.asset_bytes) `
            -ExpectedSha256 ([string]$staging.Metadata.asset_sha256)
    } 'remote digest mismatch fails closed' '*digest mismatch*'
    $wrongState = $validAsset.PSObject.Copy()
    $wrongState.state = 'new'
    Assert-Throws {
        Assert-RemoteAssetMatches `
            -Asset $wrongState `
            -ExpectedBytes ([int64]$staging.Metadata.asset_bytes) `
            -ExpectedSha256 ([string]$staging.Metadata.asset_sha256)
    } 'remote state mismatch fails closed' '*not uploaded*'
    Assert-Throws {
        Get-SingleReleaseAsset -Assets @($validAsset, $validAsset) -Name $job.AssetName
    } 'same-name remote collisions fail closed' '*duplicate*'

    $pageUris = [Collections.Generic.List[string]]::new()
    $script:GitHubGetOverride = {
        param($Uri, $Headers)
        $pageUris.Add($Uri)
        if ($Uri -like '*page=1') {
            return @(1..100 | ForEach-Object {
                New-TestAsset -Id $_ -Name "asset-$_.tar.zst" -Bytes 1 -Sha ('a' * 64)
            })
        }
        if ($Uri -like '*page=2') {
            return @(New-TestAsset -Id 101 -Name 'last.tar.zst' -Bytes 1 -Sha ('b' * 64))
        }
        throw "Unexpected pagination URI: $Uri"
    }
    $paged = @(Get-AllReleaseAssets `
        -RepositorySlug $script:ExpectedRepositorySlug `
        -ReleaseId '1234' `
        -Headers @{})
    $script:GitHubGetOverride = $null
    Assert-Equal 101 $paged.Count 'release assets are fetched across all pages'
    Assert-Equal 2 $pageUris.Count 'pagination stops after a short page'
    Assert-True ($pageUris[0] -like '*?per_page=100&page=1') `
        'first paginated URL contains the exact safe query'
    Assert-True ($pageUris[1] -like '*?per_page=100&page=2') `
        'second paginated URL contains the exact safe query'

    $row = Get-ExpectedManifestRow `
        -Job $job `
        -Metadata $staging.Metadata `
        -RemoteAsset $validAsset
    $rowCsv = @($row | ConvertTo-Csv -NoTypeInformation -UseQuotes AsNeeded)
    Assert-Equal $script:ManifestHeader $rowCsv[0] 'manifest schema is stable'
    $manifestText = $rowCsv[0] + "`n" + $rowCsv[1] + "`n"
    Assert-ManifestRowState `
        -CsvText $manifestText `
        -Expected $row `
        -Label 'synthetic manifest' | Out-Null
    Assert-True $true 'exact verified manifest row is accepted'

    $proofManifestPath = Join-Path $cleanGitRoot $script:ManifestRelativePath
    Write-TestFile -LiteralPath $proofManifestPath -Text $manifestText
    & git -C $cleanGitRoot add -- $script:ManifestRelativePath
    & git -C $cleanGitRoot commit --quiet -m 'verified synthetic manifest'
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not commit the synthetic recovery manifest'
    }
    $proofCommit = (& git -C $cleanGitRoot rev-parse HEAD).Trim()
    $proofManifestBytes = [IO.File]::ReadAllBytes($proofManifestPath)
    $proofPrior = [pscustomobject]@{
        Receipt = [pscustomobject]@{ remote_commit = $proofCommit }
        Staging = [pscustomobject]@{ Metadata = $staging.Metadata }
    }
    $script:ProofApiCalls = [Collections.Generic.List[string]]::new()
    $script:ProofAsset = $validAsset
    $script:ProofCommit = $proofCommit
    $script:ProofManifestBase64 = [Convert]::ToBase64String($proofManifestBytes)
    $priorArchiveRoot = $script:ArchiveRoot
    $script:ArchiveRoot = [IO.Path]::GetFullPath($cleanGitRoot)
    $script:GitHubGetOverride = {
        param($Uri, $Headers)
        [void]$script:ProofApiCalls.Add($Uri)
        if ($Uri -ceq "https://api.github.com/repos/$($script:ExpectedRepositorySlug)") {
            return [pscustomobject]@{
                full_name = $script:ExpectedRepositorySlug
                private = $true
                permissions = [pscustomobject]@{ push = $true }
            }
        }
        if ($Uri -like '*/releases/tags/openfoam-checkpoints-2026-08-25') {
            return [pscustomobject]@{
                tag_name = 'openfoam-checkpoints-2026-08-25'
                id = 777
            }
        }
        if ($Uri -like '*/releases/777/assets?per_page=100&page=1') {
            return @($script:ProofAsset)
        }
        if ($Uri -like '*/branches/main') {
            return [pscustomobject]@{
                commit = [pscustomobject]@{ sha = $script:ProofCommit }
            }
        }
        if ($Uri -like '*/contents/manifests/openfoam-legacy-assets-2026-08-26.csv?ref=main') {
            return [pscustomobject]@{ content = $script:ProofManifestBase64 }
        }
        throw "Unexpected synthetic recovery-proof URI: $Uri"
    }
    try {
        $proof = Confirm-PriorDeletionRemoteProof `
            -Job $job `
            -PriorDeletion $proofPrior `
            -RepositorySlug $script:ExpectedRepositorySlug `
            -ReleaseTag $script:ExpectedReleaseTag `
            -BranchName main `
            -Headers @{}
        Assert-Equal $proofCommit $proof.Commit `
            'recovery proof pins remote main to the journal commit'
        Assert-Equal $validAsset.id $proof.RemoteAsset.id `
            'recovery proof re-verifies the same exact remote asset twice'
        Assert-Equal 2 @($script:ProofApiCalls | Where-Object {
            $_ -like '*/releases/777/assets?per_page=100&page=1'
        }).Count 'recovery proof performs the final remote-asset recheck'
    } finally {
        $script:GitHubGetOverride = $null
        $script:ArchiveRoot = $priorArchiveRoot
        $script:ProofApiCalls = $null
        $script:ProofAsset = $null
        $script:ProofCommit = $null
        $script:ProofManifestBase64 = $null
    }

    $stagedGuard = [IO.File]::Open(
        $staging.Paths.Asset,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    try {
        Assert-Throws {
            $blockedWriter = [IO.File]::Open(
                $staging.Paths.Asset,
                [IO.FileMode]::Open,
                [IO.FileAccess]::ReadWrite,
                [IO.FileShare]::None)
            $blockedWriter.Dispose()
        } 'held recovery guard blocks staged-asset replacement or writes'
    } finally {
        $stagedGuard.Dispose()
    }

    $conflict = $row.PSObject.Copy()
    $conflict.sha256 = 'f' * 64
    Assert-Throws {
        Assert-ManifestRowState `
            -CsvText $manifestText `
            -Expected $conflict `
            -Label 'conflicting manifest'
    } 'conflicting manifest row fails closed' '*conflicting*'
    $bytesA = [Text.Encoding]::UTF8.GetBytes("manifest`n")
    $bytesB = [Text.Encoding]::UTF8.GetBytes("manifest`r`n")
    Assert-Throws {
        Assert-ByteArraysEqual -Expected $bytesA -Actual $bytesB -Label 'remote manifest'
    } 'manifest verification is byte-for-byte, not newline-normalized' '*byte-for-byte*'

    $commit = 'c' * 40
    Assert-Throws {
        Remove-SourceRootSafely `
            -Job $job `
            -Inventory $inventory `
            -ArchiveRoot $archiveRoot `
            -StagedAsset $staging.Paths.Asset `
            -Metadata $staging.Metadata `
            -RemoteCommit $commit `
            -DeletionAuthorized $false `
            -RemoteAssetVerified $true `
            -RemoteManifestVerified $true
    } 'deletion refuses absent explicit authorization' '*explicit -DeleteSources*'
    Assert-True (Test-Path -LiteralPath $target -PathType Container) `
        'authorization failure leaves source root intact'
    Assert-Throws {
        Remove-SourceRootSafely `
            -Job $job `
            -Inventory $inventory `
            -ArchiveRoot $archiveRoot `
            -StagedAsset $staging.Paths.Asset `
            -Metadata $staging.Metadata `
            -RemoteCommit $commit `
            -DeletionAuthorized $true `
            -RemoteAssetVerified $false `
            -RemoteManifestVerified $true
    } 'deletion refuses absent remote-asset gate' '*remote asset and remote manifest*'
    Assert-True (Test-Path -LiteralPath $target -PathType Container) `
        'remote gate failure leaves source root intact'

    Assert-Throws {
        Remove-SourceRootSafely `
            -Job $job `
            -Inventory $inventory `
            -ArchiveRoot $archiveRoot `
            -StagedAsset $staging.Paths.Asset `
            -Metadata $staging.Metadata `
            -RemoteCommit $commit `
            -DeletionAuthorized $true `
            -RemoteAssetVerified $true `
            -RemoteManifestVerified $true `
            -InjectMoveFailure
    } 'injected quarantine move failure fails safely' '*source is retained*'
    Assert-True (Test-Path -LiteralPath $target -PathType Container) `
        'move failure leaves the source root intact'

    Assert-Throws {
        Remove-SourceRootSafely `
            -Job $job `
            -Inventory $inventory `
            -ArchiveRoot $archiveRoot `
            -StagedAsset $staging.Paths.Asset `
            -Metadata $staging.Metadata `
            -RemoteCommit $commit `
            -DeletionAuthorized $true `
            -RemoteAssetVerified $true `
            -RemoteManifestVerified $true `
            -PostMoveProbe {
                param($QuarantinedRoot)
                [IO.File]::WriteAllText(
                    (Join-Path $QuarantinedRoot '0/fluid/T'),
                    'changed-after-quarantine',
                    [Text.UTF8Encoding]::new($false))
            }
    } 'post-move source mutation fails the final quarantine fingerprint' '*restored from the verified archive*'
    $postMoveRollback = Get-SourceInventory -Job $job -DeepHash $true
    Assert-Equal $inventory.SourceFingerprintSha256 `
        $postMoveRollback.SourceFingerprintSha256 `
        'post-move mutation is rolled back to the verified archive fingerprint'

    Assert-Throws {
        Remove-SourceRootSafely `
            -Job $job `
            -Inventory $inventory `
            -ArchiveRoot $archiveRoot `
            -StagedAsset $staging.Paths.Asset `
            -Metadata $staging.Metadata `
            -RemoteCommit $commit `
            -DeletionAuthorized $true `
            -RemoteAssetVerified $true `
            -RemoteManifestVerified $true `
            -InjectPurgeFailureAfterDamage
    } 'partial purge failure triggers automatic archive rollback' '*restored from the verified archive*'
    Assert-True (Test-Path -LiteralPath $target -PathType Container) `
        'partial purge failure restores the exact source root'
    $restoredInventory = Get-SourceInventory -Job $job -DeepHash $true
    Assert-Equal $inventory.SourceFingerprintSha256 `
        $restoredInventory.SourceFingerprintSha256 `
        'rollback restores the byte-identical source fingerprint'

    $journal = Remove-SourceRootSafely `
        -Job $job `
        -Inventory $inventory `
        -ArchiveRoot $archiveRoot `
        -StagedAsset $staging.Paths.Asset `
        -Metadata $staging.Metadata `
        -RemoteCommit $commit `
        -DeletionAuthorized $true `
        -RemoteAssetVerified $true `
        -RemoteManifestVerified $true
    Assert-True (-not (Test-Path -LiteralPath $target)) `
        'successful deletion removes only the one exact approved source root'
    Assert-True (Test-Path -LiteralPath $journal.ReceiptPath -PathType Leaf) `
        'successful deletion leaves a durable completion journal until staging cleanup'
    Assert-True (Test-Path -LiteralPath (Join-Path $caseParent $script:ActiveCaseName) -PathType Container) `
        'active case remains present after exact-root deletion'
    foreach ($preserved in $script:PreservedSnapshots) {
        Assert-True (Test-Path -LiteralPath (Join-Path $snapshotParent $preserved) -PathType Container) `
            "preserved validation snapshot remains: $preserved"
    }
    $completionPaths = Get-DeletionPaths -ArchiveRoot $archiveRoot -Job $job
    [IO.Directory]::CreateDirectory($completionPaths.Container) | Out-Null
    $completedInspection = Restore-PriorDeletionJournal `
        -Job $job `
        -ArchiveRoot $archiveRoot `
        -InspectOnly
    Assert-Equal 'DeletionCompleted' $completedInspection.State `
        'completed deletion journal is inspectable without recreating source'
    Assert-True (Test-Path -LiteralPath $completionPaths.Container -PathType Container) `
        'non-mutating completion inspection leaves its empty container untouched'
    $completed = Restore-PriorDeletionJournal -Job $job -ArchiveRoot $archiveRoot
    Assert-Equal 'DeletionCompleted' $completed.State `
        'completed deletion journal remains resumable during final cleanup'
    Assert-True (-not (Test-Path -LiteralPath $completionPaths.Container)) `
        'completion cleanup removes its exact empty per-asset container'
    Remove-CompletedStaging `
        -ArchiveRoot $archiveRoot `
        -Staging $completed.Staging `
        -DeletionReceipt $completed.ReceiptPath
    Assert-True (-not (Test-Path -LiteralPath $completed.ReceiptPath)) `
        'completion finalization removes the completed receipt after verification'
    Assert-NoForeignDeletionState -ArchiveRoot $archiveRoot -Job $longJob
    Assert-True $true `
        'completed finalization leaves no artifact that blocks the next asset'

    if ($RunRealLinkProbe) {
        $realParent =
            'C:\Users\hconn\.codex\visualizations\2026\08\04\019fcccd-4536-7b51-a70c-8023779a1618\openfoam_benchmark_snapshots'
        $realRelative =
            'decomposition_auto_case/processor0/1.6000000000000001/uniform'
        $realSource = Join-Path $realParent $realRelative.Replace('/', '\')
        $linkProbeRoot = Join-Path $temporaryRoot 'real-link-probe'
        $linkRestoreRoot = Join-Path $linkProbeRoot (
            'restore-' + ('x' * 110))
        [IO.Directory]::CreateDirectory($linkRestoreRoot) | Out-Null
        $linkList = Join-Path $linkProbeRoot 'members.txt'
        [IO.File]::WriteAllText(
            $linkList,
            $realRelative + "`n",
            [Text.UTF8Encoding]::new($false))
        $linkPaths = [pscustomobject]@{
            Root = $linkProbeRoot
            Partial = Join-Path $linkProbeRoot 'link.tar.zst'
            List = $linkList
        }
        New-DeterministicArchiveFile `
            -Job ([pscustomobject]@{
                ParentRoot = $realParent
                RelativeRoot = 'decomposition_auto_case'
            }) `
            -Inventory ([pscustomobject]@{ LinkCount = 1 }) `
            -Paths $linkPaths
        $verifiedLinkTar = Get-VerifiedZstdTarInventory `
            -LiteralPath $linkPaths.Partial `
            -ExpectedEntries @([pscustomobject]@{
                ArchivePath = $realRelative
                Kind = 'link'
                Length = [int64]0
                Sha256 = $null
            }) `
            -TopRoot 'decomposition_auto_case'
        Assert-Equal 1 $verifiedLinkTar.MemberCount `
            'real approved LX-link archive has exactly the intended member'
        Expand-ArchiveForRollback `
            -StagedAsset $linkPaths.Partial `
            -RestoreRoot $linkRestoreRoot `
            -QuarantineRoot $linkProbeRoot `
            -LinkCount 1
        $restoredLink = Join-Path $linkRestoreRoot $realRelative.Replace('/', '\')
        Assert-True ($restoredLink.Length -ge 260) `
            'real restored LX-link probe crosses the fsutil MAX_PATH boundary'
        $restoredLinkItem = Get-Item -LiteralPath $restoredLink -Force
        Assert-True (
            ($restoredLinkItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
        ) 'WSL rollback extraction recreates an LX reparse point'
        Assert-Equal `
            (Get-ReparsePayloadHash -LiteralPath $realSource) `
            (Get-ReparsePayloadHash -LiteralPath $restoredLink) `
            'rollback recreates the exact approved LX-link reparse payload hash'
        $realAllowedLinks = [Collections.Generic.HashSet[string]]::new(
            [StringComparer]::Ordinal)
        [void]$realAllowedLinks.Add($realRelative)
        $realRestoredRoot = Join-Path $linkRestoreRoot 'decomposition_auto_case'
        Remove-RecoveryTreeNoFollow `
            -LiteralPath $realRestoredRoot `
            -RelativeBase $linkRestoreRoot `
            -ApprovedRoot $linkProbeRoot `
            -AllowedReparsePaths $realAllowedLinks
        Assert-True (-not (Test-Path -LiteralPath $realRestoredRoot)) `
            'no-follow cleanup removes the restored LX link object and test tree'
        Assert-True (Test-Path -LiteralPath $realSource) `
            'no-follow cleanup never traverses into the real approved link source'
        Assert-Equal $script:ExpectedUniformLinkHash `
            (Get-ReparsePayloadHash -LiteralPath $realSource) `
            'real approved LX-link payload remains unchanged after no-follow cleanup'
    }

    Write-Output "PASS: $script:Passed assertions; staging, pagination, collision, manifest, gate, rollback, resume, and exact-deletion tests succeeded."
} finally {
    $script:GitHubGetOverride = $null
    $script:ReparseQueryOverride = $null
    $resolvedTemporary = [IO.Path]::GetFullPath($temporaryRoot)
    $systemTemporary = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
    if (
        (Test-PathUnderRoot -Candidate $resolvedTemporary -Root $systemTemporary) -and
        [IO.Path]::GetFileName($resolvedTemporary).StartsWith(
            'openfoam-legacy-archive-test-', [StringComparison]::Ordinal)
    ) {
        Remove-Item -LiteralPath $resolvedTemporary -Recurse -Force -ErrorAction SilentlyContinue
    }
}
