#requires -Version 7.0

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$helper = (Resolve-Path -LiteralPath (
    Join-Path $PSScriptRoot '..\tools\archive_openfoam_checkpoint.ps1')).Path
$pwsh = (Get-Command pwsh -CommandType Application -ErrorAction Stop).Source
$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar)
$testRoot = Join-Path $tempBase ("thermal sim archive helper [" + [guid]::NewGuid().ToString('N') + "]")

function Assert-True {
    param(
        [Parameter(Mandatory)] [bool] $Condition,
        [Parameter(Mandatory)] [string] $Message
    )
    if (-not $Condition) {
        throw "ASSERTION FAILED: $Message"
    }
}

$helperText = [IO.File]::ReadAllText($helper)
Assert-True `
    -Condition $helperText.Contains('${uploadBase}?name=') `
    -Message 'GitHub upload URL does not delimit uploadBase before the query string'
Assert-True `
    -Condition (-not [regex]::IsMatch($helperText, '\$[A-Za-z_][A-Za-z0-9_]*\?')) `
    -Message 'Archive helper contains an unsafe PowerShell variable/query-string interpolation'

function Invoke-Helper {
    param([Parameter(Mandatory)] [string[]] $Arguments)

    $output = @(& $pwsh -NoLogo -NoProfile -File $helper @Arguments 2>&1)
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Text = ($output -join [Environment]::NewLine)
    }
}

function New-Checkpoint {
    param(
        [Parameter(Mandatory)] [string] $CaseRoot,
        [Parameter(Mandatory)] [string] $Time,
        [int] $FilesPerCopy = 74,
        [switch] $OneEmptyFile
    )

    $parents = @($CaseRoot)
    0..3 | ForEach-Object {
        $parents += Join-Path $CaseRoot "processor$_"
    }
    foreach ($parent in $parents) {
        $directory = Join-Path $parent $Time
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        $fluid = Join-Path $directory 'fluid'
        New-Item -ItemType Directory -Path $fluid | Out-Null
        [IO.File]::WriteAllText((Join-Path $fluid 'T'), "$Time|T")
        [IO.File]::WriteAllText((Join-Path $fluid 'U'), "$Time|U")
        for ($index = 0; $index -lt ($FilesPerCopy - 2); ++$index) {
            $content = if ($OneEmptyFile -and $parent -ceq $CaseRoot -and $index -eq 0) {
                ''
            } else {
                "$Time|$([IO.Path]::GetFileName($parent))|$index"
            }
            [IO.File]::WriteAllText((Join-Path $directory ("field_{0:D2}" -f $index)), $content)
        }
    }
}

try {
    $caseRoot = Join-Path $testRoot 'synthetic case [literal]'
    $archiveRoot = Join-Path $testRoot 'archive manifest [literal]'
    New-Item -ItemType Directory -Path $caseRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $archiveRoot -Force | Out-Null
    foreach ($directory in @('system', 'constant', 'processor0', 'processor1', 'processor2', 'processor3')) {
        New-Item -ItemType Directory -Path (Join-Path $caseRoot $directory) -Force | Out-Null
    }

    New-Checkpoint -CaseRoot $caseRoot -Time '0.9'
    New-Checkpoint -CaseRoot $caseRoot -Time '1.0'
    New-Checkpoint -CaseRoot $caseRoot -Time '1.1'
    New-Checkpoint -CaseRoot $caseRoot -Time '0.8' -FilesPerCopy 73
    New-Checkpoint -CaseRoot $caseRoot -Time '0.7' -OneEmptyFile

    $manifestPath = Join-Path $archiveRoot 'checkpoints.csv'
    [IO.File]::WriteAllText(
        $manifestPath,
        'case,time_directory,asset,source_files,source_bytes,asset_bytes,sha256,github_digest,uploaded_at_utc,status' + [Environment]::NewLine)
    [IO.File]::WriteAllText((Join-Path $archiveRoot '.gitignore'), "assets/" + [Environment]::NewLine)
    [IO.File]::WriteAllText((Join-Path $archiveRoot 'README.md'), "# synthetic archive" + [Environment]::NewLine)

    & git -C $archiveRoot init -b main | Out-Null
    & git -C $archiveRoot add -- .gitignore README.md checkpoints.csv
    & git -C $archiveRoot -c user.name='Synthetic Test' -c user.email='synthetic@example.invalid' commit -m 'Initialize synthetic archive' | Out-Null
    & git -C $archiveRoot remote add origin 'https://github.com/example/synthetic-openfoam-archive.git'
    Assert-True -Condition ($LASTEXITCODE -eq 0) -Message 'synthetic archive Git setup failed'

    $common = @(
        '-CasePath', $caseRoot,
        '-ArchiveRepoPath', $archiveRoot,
        '-AssetName', 'synthetic_t0p9.tar',
        '-ReleaseTag', 'synthetic-checkpoints'
    )
    $manifestBefore = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
    $validSourceBefore = @(
        Get-ChildItem -LiteralPath (Join-Path $caseRoot '0.9') -Recurse -File
        0..3 | ForEach-Object {
            Get-ChildItem -LiteralPath (Join-Path (Join-Path $caseRoot "processor$_") '0.9') -Recurse -File
        }
    ).Count

    $plan = Invoke-Helper -Arguments (@('-Mode', 'Plan', '-TimeDirectory', '0.9') + $common)
    Assert-True -Condition ($plan.ExitCode -eq 0) -Message "valid plan failed: $($plan.Text)"
    $planObject = $plan.Text | ConvertFrom-Json
    Assert-True -Condition ($planObject.SourceFiles -eq 370) -Message 'plan did not report 370 files'
    Assert-True -Condition (-not $planObject.TargetIsLatestTwo) -Message '0.9 was incorrectly classified as latest-two'
    Assert-True -Condition ($planObject.Targets.Count -eq 5) -Message 'plan did not prove five exact targets'

    $validate = Invoke-Helper -Arguments (@('-Mode', 'Validate', '-TimeDirectory', '0.9') + $common)
    Assert-True -Condition ($validate.ExitCode -eq 0) -Message "valid validation failed: $($validate.Text)"

    $latestRefusal = Invoke-Helper -Arguments (
        @('-Mode', 'Plan', '-TimeDirectory', '1.1', '-DeleteSources') + $common)
    Assert-True -Condition ($latestRefusal.ExitCode -ne 0) -Message 'latest checkpoint deletion was not refused'
    Assert-True -Condition ($latestRefusal.Text -match 'latest two') -Message 'latest-two refusal was not explicit'

    $latestOverride = Invoke-Helper -Arguments (
        @('-Mode', 'Plan', '-TimeDirectory', '1.1', '-DeleteSources', '-AllowDeleteLatestTwo') + $common)
    Assert-True -Condition ($latestOverride.ExitCode -eq 0) -Message "explicit latest-two override did not plan: $($latestOverride.Text)"

    $traversal = Invoke-Helper -Arguments (
        @('-Mode', 'Plan', '-TimeDirectory', '..\escape') + $common)
    Assert-True -Condition ($traversal.ExitCode -ne 0) -Message 'path traversal time was accepted'

    $shortCopy = Invoke-Helper -Arguments (
        @('-Mode', 'Validate', '-TimeDirectory', '0.8') + $common)
    Assert-True -Condition ($shortCopy.ExitCode -ne 0) -Message '73-file checkpoint was accepted'
    Assert-True -Condition ($shortCopy.Text -match 'expected 74') -Message 'file-count failure was not explicit'

    $emptyFile = Invoke-Helper -Arguments (
        @('-Mode', 'Validate', '-TimeDirectory', '0.7') + $common)
    Assert-True -Condition ($emptyFile.ExitCode -ne 0) -Message 'checkpoint with an empty file was accepted'
    Assert-True -Condition ($emptyFile.Text -match 'empty file') -Message 'empty-file failure was not explicit'

    $shortRoot = Join-Path $caseRoot '0.8'
    Remove-Item -LiteralPath (Join-Path $shortRoot 'fluid\U')
    [IO.File]::WriteAllText((Join-Path $shortRoot 'diagnostic_a'), 'not a restart field')
    [IO.File]::WriteAllText((Join-Path $shortRoot 'diagnostic_b'), 'not a restart field')
    $missingRestart = Invoke-Helper -Arguments (
        @('-Mode', 'Validate', '-TimeDirectory', '0.8') + $common)
    Assert-True -Condition ($missingRestart.ExitCode -ne 0) -Message 'checkpoint missing fluid/U was accepted'
    Assert-True -Condition ($missingRestart.Text -match 'not restartable') -Message 'restart-field failure was not explicit'

    [IO.File]::WriteAllText((Join-Path $caseRoot '0.7\field_00'), 'repaired synthetic field')
    Move-Item -LiteralPath (Join-Path $caseRoot 'processor3\0.7\field_00') -Destination (
        Join-Path $caseRoot 'processor3\0.7\different_field')
    $manifestMismatch = Invoke-Helper -Arguments (
        @('-Mode', 'Validate', '-TimeDirectory', '0.7') + $common)
    Assert-True -Condition ($manifestMismatch.ExitCode -ne 0) -Message 'rank-mismatched field manifest was accepted'
    Assert-True -Condition ($manifestMismatch.Text -match 'identical relative-file manifests') -Message 'rank-manifest failure was not explicit'

    $aliasPath = Join-Path $caseRoot 'processor2\1'
    New-Item -ItemType Directory -Path $aliasPath | Out-Null
    $numericAlias = Invoke-Helper -Arguments (
        @('-Mode', 'Plan', '-TimeDirectory', '0.9') + $common)
    Assert-True -Condition ($numericAlias.ExitCode -ne 0) -Message 'duplicate numeric time spelling was accepted'
    Assert-True -Condition ($numericAlias.Text -match '(?s)Ambiguous.*time spellings') -Message "numeric-alias failure was not explicit: $($numericAlias.Text)"
    Remove-Item -LiteralPath $aliasPath

    $manifestAfter = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
    $validSourceAfter = @(
        Get-ChildItem -LiteralPath (Join-Path $caseRoot '0.9') -Recurse -File
        0..3 | ForEach-Object {
            Get-ChildItem -LiteralPath (Join-Path (Join-Path $caseRoot "processor$_") '0.9') -Recurse -File
        }
    ).Count
    Assert-True -Condition ($manifestAfter -ceq $manifestBefore) -Message 'read-only modes changed checkpoints.csv'
    Assert-True -Condition ($validSourceAfter -eq $validSourceBefore) -Message 'read-only modes changed source files'
    Assert-True -Condition (-not (Test-Path -LiteralPath (Join-Path $archiveRoot 'assets'))) -Message 'read-only modes created staging'
    Assert-True -Condition (-not (Test-Path -LiteralPath (Join-Path $caseRoot '.thermal_solver_run.lock'))) -Message 'read-only modes created the solver lock file'
    $gitStatus = @(& git -C $archiveRoot status --porcelain)
    Assert-True -Condition ($gitStatus.Count -eq 0) -Message 'read-only modes dirtied the archive repository'

    New-Checkpoint -CaseRoot $caseRoot -Time '0.6'
    $loadArguments = @{
        Mode = 'Plan'
        CasePath = 'unused'
        ArchiveRepoPath = 'unused'
        TimeDirectory = '1'
        AssetName = 'unused.tar'
        ReleaseTag = 'unused'
        LoadFunctionsOnly = $true
    }
    $syntheticArchiveRepo = $archiveRoot
    . $helper @loadArguments

    $wsl = @(Get-Command wsl.exe -CommandType Application -ErrorAction SilentlyContinue) | Select-Object -First 1
    $wslUsable = $false
    if ($null -ne $wsl) {
        try {
            $null = & $wsl.Source --exec sh -c 'exit 0' 2>$null
            $wslUsable = $LASTEXITCODE -eq 0
        } catch {
            $wslUsable = $false
        }
    }
    if ($wslUsable) {
        $realLock = $null
        $secondLock = $null
        try {
            $lockPath = Join-Path $caseRoot '.thermal_solver_run.lock'
            $realLock = Start-CaseArchiveLock -CaseRoot $caseRoot -LockFile $lockPath
            Assert-CaseArchiveLockHeld -Process $realLock
            $collisionRefused = $false
            try {
                $secondLock = Start-CaseArchiveLock -CaseRoot $caseRoot -LockFile $lockPath
            } catch {
                $collisionRefused = $_.Exception.Message -match 'already held'
            }
            Assert-True -Condition $collisionRefused -Message 'a second archive/solver lock was not refused'
        } finally {
            Stop-CaseArchiveLock -Process $secondLock
            Stop-CaseArchiveLock -Process $realLock
        }
    } else {
        Write-Output 'SKIP: real WSL flock lifetime test (WSL service unavailable)'
    }

    Set-ArchiveRootForTesting -LiteralPath $syntheticArchiveRepo
    $originalManifestText = [IO.File]::ReadAllText($manifestPath)
    [IO.File]::AppendAllText($manifestPath, '# unstaged status probe')
    $statusProbe = @(Get-ArchiveGitStatusRecords)
    Assert-True -Condition ($statusProbe.Count -eq 1) -Message 'raw porcelain status did not return one record'
    Assert-True -Condition ($statusProbe[0].Status -ceq ' M') -Message 'raw porcelain status lost the leading unstaged marker'
    Assert-True -Condition ($statusProbe[0].Path -ceq 'checkpoints.csv') -Message 'raw porcelain status parsed the wrong path'
    Set-TextFileIfUnchangedDurably -LiteralPath $manifestPath -ExpectedCurrentText ([IO.File]::ReadAllText($manifestPath)) -NewText $originalManifestText
    $conditionalWriteRefused = $false
    try {
        Set-TextFileIfUnchangedDurably -LiteralPath $manifestPath -ExpectedCurrentText 'not the current bytes' -NewText 'must not be written'
    } catch {
        $conditionalWriteRefused = $_.Exception.Message -match 'changed concurrently'
    }
    Assert-True -Condition $conditionalWriteRefused -Message 'conditional manifest write accepted stale bytes'
    Assert-True -Condition ([IO.File]::ReadAllText($manifestPath) -ceq $originalManifestText) -Message 'conditional manifest rejection changed the file'

    $lock = [pscustomobject]@{ HasExited = $false }
    $checkpoint = Get-ValidatedCheckpoint -CaseRoot $caseRoot -ExactTime '0.6'
    $sourceFingerprint = Get-CheckpointFingerprint -Checkpoint $checkpoint

    if ($IsWindows) {
        $junctions = @()
        try {
            foreach ($target in $checkpoint.Targets) {
                $parent = [IO.Path]::GetDirectoryName($target.FullPath)
                $junction = Join-Path $parent '9.0'
                New-Item -ItemType Junction -Path $junction -Target $target.FullPath | Out-Null
                $junctions += $junction
            }
            $latestWithReparse = @(Get-LatestCompleteCheckpointTimes -CaseRoot $caseRoot)
            Assert-True -Condition ($latestWithReparse.Name -cnotcontains '9.0') -Message 'reparse-backed time spoofed latest-two retention'
        } finally {
            foreach ($junction in $junctions) {
                Remove-Item -LiteralPath $junction -Force
            }
        }
    }

    $stageRoot = Join-Path $testRoot 'local primitive stage [literal]'
    New-Item -ItemType Directory -Path $stageRoot | Out-Null
    $stagedAsset = Join-Path $stageRoot 'synthetic_delete_t0p6.tar'
    $partialAsset = "$stagedAsset.part"
    $stagedMetadata = "$stagedAsset.checkpoint.json"
    $assetItem = New-CheckpointTar -CaseRoot $caseRoot -Checkpoint $checkpoint -FinalPath $stagedAsset -PartialPath $partialAsset -MetadataPath $stagedMetadata -SourceFingerprint $sourceFingerprint -LockProcess $lock
    Assert-True -Condition ($assetItem.Length -gt 0) -Message 'synthetic tar was not staged'
    Assert-True -Condition (Test-Path -LiteralPath $stagedMetadata -PathType Leaf) -Message 'staged tar metadata was not created'
    $assetSha = (Get-FileHash -LiteralPath $stagedAsset -Algorithm SHA256).Hash.ToLowerInvariant()
    $reusedAsset = New-CheckpointTar -CaseRoot $caseRoot -Checkpoint $checkpoint -FinalPath $stagedAsset -PartialPath $partialAsset -MetadataPath $stagedMetadata -SourceFingerprint $sourceFingerprint -LockProcess $lock
    Assert-True -Condition ((Get-FileHash -LiteralPath $reusedAsset.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $assetSha) -Message 'resumable tar reuse changed archive bytes'

    $specialTar = Join-Path $stageRoot 'forbidden_link.tar'
    $specialStream = [IO.File]::Create($specialTar)
    $specialWriter = [System.Formats.Tar.TarWriter]::new($specialStream, $false)
    try {
        $linkEntry = [System.Formats.Tar.PaxTarEntry]::new(
            [System.Formats.Tar.TarEntryType]::SymbolicLink,
            '0.6/forbidden_link')
        $linkEntry.LinkName = '../../escape'
        $specialWriter.WriteEntry($linkEntry)
    } finally {
        $specialWriter.Dispose()
    }
    $specialEntryRefused = $false
    try {
        $null = Get-StructuredTarEntries -TarPath $specialTar
    } catch {
        $specialEntryRefused = $_.Exception.Message -match 'forbidden type'
    }
    Assert-True -Condition $specialEntryRefused -Message 'structured tar validation accepted a symbolic link'
    Remove-Item -LiteralPath $specialTar -Force

    $matchingAsset = [pscustomobject]@{
        size = [int64]$assetItem.Length
        digest = "sha256:$assetSha"
        state = 'uploaded'
    }
    Assert-RemoteAssetMatches -Asset $matchingAsset -ExpectedBytes $assetItem.Length -ExpectedSha256 $assetSha
    $badDigestRefused = $false
    try {
        $badAsset = [pscustomobject]@{
            size = [int64]$assetItem.Length
            digest = 'sha256:' + ('0' * 64)
            state = 'uploaded'
        }
        Assert-RemoteAssetMatches -Asset $badAsset -ExpectedBytes $assetItem.Length -ExpectedSha256 $assetSha
    } catch {
        $badDigestRefused = $_.Exception.Message -match 'digest mismatch'
    }
    Assert-True -Condition $badDigestRefused -Message 'remote digest mismatch was not refused'

    $originalTarBytes = [IO.File]::ReadAllBytes($stagedAsset)
    $tamperedTarBytes = [byte[]]$originalTarBytes.Clone()
    $tamperedTarBytes[$tamperedTarBytes.Length - 1] = $tamperedTarBytes[$tamperedTarBytes.Length - 1] -bxor 1
    [IO.File]::WriteAllBytes($stagedAsset, $tamperedTarBytes)
    $tamperedDeleteRefused = $false
    try {
        Remove-CheckpointGroupSafely -CaseRoot $caseRoot -Checkpoint $checkpoint -Asset 'synthetic_delete_t0p6.tar' -AssetSha256 $assetSha -ExpectedSourceFingerprint $sourceFingerprint -StagedAsset $stagedAsset -RemoteCommit ('a' * 40) -LockProcess $lock
    } catch {
        $tamperedDeleteRefused = $_.Exception.Message -match 'changed after remote verification'
    } finally {
        [IO.File]::WriteAllBytes($stagedAsset, $originalTarBytes)
    }
    Assert-True -Condition $tamperedDeleteRefused -Message 'same-name staged tar mutation did not block deletion'
    $checkpoint = Get-ValidatedCheckpoint -CaseRoot $caseRoot -ExactTime '0.6'
    Assert-True -Condition ((Get-CheckpointFingerprint -Checkpoint $checkpoint) -ceq $sourceFingerprint) -Message 'tar mutation rejection changed sources'

    $sourceMutationTarget = Join-Path $caseRoot '0.6\fluid\T'
    $originalSourceBytes = [IO.File]::ReadAllBytes($sourceMutationTarget)
    $mutatedSourceBytes = [byte[]]$originalSourceBytes.Clone()
    $mutatedSourceBytes[0] = $mutatedSourceBytes[0] -bxor 1
    Assert-True -Condition ($mutatedSourceBytes.Length -eq $originalSourceBytes.Length) -Message 'source-race probe did not preserve file size'
    $preMoveSourceProbe = {
        param($ignoredCheckpoint)
        [IO.File]::WriteAllBytes($sourceMutationTarget, $mutatedSourceBytes)
    }.GetNewClosure()
    $preMoveQuarantine = Get-QuarantinePaths -CaseRoot $caseRoot -Asset 'synthetic_delete_t0p6.tar'
    $preMoveMutationRefused = $false
    try {
        Remove-CheckpointGroupSafely -CaseRoot $caseRoot -Checkpoint $checkpoint -Asset 'synthetic_delete_t0p6.tar' -AssetSha256 $assetSha -ExpectedSourceFingerprint $sourceFingerprint -StagedAsset $stagedAsset -RemoteCommit ('a' * 40) -LockProcess $lock -PreMoveSourceProbe $preMoveSourceProbe
    } catch {
        $preMoveMutationRefused = $_.Exception.Message -match 'source fingerprint changed immediately before quarantine'
    } finally {
        [IO.File]::WriteAllBytes($sourceMutationTarget, $originalSourceBytes)
    }
    Assert-True -Condition $preMoveMutationRefused -Message 'same-size source mutation immediately before quarantine did not block deletion'
    foreach ($target in $checkpoint.Targets) {
        Assert-True -Condition (Test-Path -LiteralPath $target.FullPath -PathType Container) -Message "pre-move source mutation moved $($target.FullPath)"
    }
    Assert-True -Condition (-not (Test-Path -LiteralPath $preMoveQuarantine.Group)) -Message 'pre-move source mutation retained a quarantine group'
    Assert-True -Condition (-not (Test-Path -LiteralPath $preMoveQuarantine.Receipt)) -Message 'pre-move source mutation retained a quarantine receipt'
    $checkpoint = Get-ValidatedCheckpoint -CaseRoot $caseRoot -ExactTime '0.6'
    Assert-True -Condition ((Get-CheckpointFingerprint -Checkpoint $checkpoint) -ceq $sourceFingerprint) -Message 'pre-move source mutation test did not restore original source bytes'

    $script:AssetGuardDeniedWrite = $false
    $assetGuardProbe = {
        param($path)
        $writer = $null
        try {
            $writer = [IO.File]::Open(
                $path,
                [IO.FileMode]::Open,
                [IO.FileAccess]::Write,
                [IO.FileShare]::ReadWrite)
        } catch [IO.IOException] {
            $script:AssetGuardDeniedWrite = $true
        } finally {
            if ($null -ne $writer) {
                $writer.Dispose()
            }
        }
    }
    $moveFailureRestored = $false
    try {
        Remove-CheckpointGroupSafely -CaseRoot $caseRoot -Checkpoint $checkpoint -Asset 'synthetic_delete_t0p6.tar' -AssetSha256 $assetSha -ExpectedSourceFingerprint $sourceFingerprint -StagedAsset $stagedAsset -RemoteCommit ('a' * 40) -LockProcess $lock -InjectMoveFailureAfter 2 -AssetGuardProbe $assetGuardProbe
    } catch {
        $moveFailureRestored = $_.Exception.Message -match 'all sources were restored'
    }
    Assert-True -Condition $moveFailureRestored -Message 'injected quarantine failure did not report restoration'
    Assert-True -Condition $script:AssetGuardDeniedWrite -Message 'deletion gate did not deny a concurrent staged-tar writer'
    $checkpoint = Get-ValidatedCheckpoint -CaseRoot $caseRoot -ExactTime '0.6'
    Assert-True -Condition ((Get-CheckpointFingerprint -Checkpoint $checkpoint) -ceq $sourceFingerprint) -Message 'move failure did not restore exact source bytes'

    $purgeFailureRestored = $false
    try {
        Remove-CheckpointGroupSafely -CaseRoot $caseRoot -Checkpoint $checkpoint -Asset 'synthetic_delete_t0p6.tar' -AssetSha256 $assetSha -ExpectedSourceFingerprint $sourceFingerprint -StagedAsset $stagedAsset -RemoteCommit ('a' * 40) -LockProcess $lock -InjectPurgeFailureAfter 2
    } catch {
        $purgeFailureRestored = $_.Exception.Message -match 'all source directories were restored'
    }
    Assert-True -Condition $purgeFailureRestored -Message 'injected purge failure did not report restoration'
    $checkpoint = Get-ValidatedCheckpoint -CaseRoot $caseRoot -ExactTime '0.6'
    Assert-True -Condition ((Get-CheckpointFingerprint -Checkpoint $checkpoint) -ceq $sourceFingerprint) -Message 'purge failure did not restore exact source bytes'

    $quarantine = Get-QuarantinePaths -CaseRoot $caseRoot -Asset 'synthetic_delete_t0p6.tar'
    New-Item -ItemType Directory -Path $quarantine.Group -Force | Out-Null
    $resumeTargets = @(
        foreach ($target in $checkpoint.Targets) {
            $copy = Test-CheckpointCopy -LiteralPath $target.FullPath
            [pscustomobject]@{
                source = $target.FullPath
                destination = Join-Path $quarantine.Group $target.Relative
                relative = $target.Relative
                bytes = [int64]$copy.Bytes
            }
        }
    )
    $firstTarget = $checkpoint.Targets[0]
    Move-Item -LiteralPath $firstTarget.FullPath -Destination $resumeTargets[0].destination
    $resumeReceipt = [pscustomobject][ordered]@{
        case_path = $caseRoot
        time_directory = '0.6'
        asset = 'synthetic_delete_t0p6.tar'
        sha256 = $assetSha
        source_fingerprint = $sourceFingerprint
        staged_asset = $stagedAsset
        remote_commit = ('a' * 40)
        phase = 'moving'
        moved_count = 1
        purged_count = 0
        targets = $resumeTargets
    }
    Write-QuarantineReceipt -ReceiptPath $quarantine.Receipt -Receipt $resumeReceipt
    $resumed = Restore-PriorCheckpointQuarantine -CaseRoot $caseRoot -ExactTime '0.6' -Asset 'synthetic_delete_t0p6.tar' -ExpectedStagedAsset $stagedAsset -LockProcess $lock
    Assert-True -Condition ($resumed.State -ceq 'SourcesRestored') -Message 'interrupted quarantine was not resumed by restoring sources'
    $checkpoint = Get-ValidatedCheckpoint -CaseRoot $caseRoot -ExactTime '0.6'
    Assert-True -Condition ((Get-CheckpointFingerprint -Checkpoint $checkpoint) -ceq $sourceFingerprint) -Message 'resumed quarantine did not restore exact source bytes'

    $deletionJournal = Remove-CheckpointGroupSafely -CaseRoot $caseRoot -Checkpoint $checkpoint -Asset 'synthetic_delete_t0p6.tar' -AssetSha256 $assetSha -ExpectedSourceFingerprint $sourceFingerprint -StagedAsset $stagedAsset -RemoteCommit ('a' * 40) -LockProcess $lock
    foreach ($target in $checkpoint.Targets) {
        Assert-True -Condition (-not (Test-Path -LiteralPath $target.FullPath)) -Message "successful synthetic deletion retained $($target.FullPath)"
    }
    $deletionJournal.AssetGuard.Dispose()
    Remove-Item -LiteralPath $stagedAsset -Force
    Remove-Item -LiteralPath $stagedMetadata -Force
    $deletionJournal.Receipt.phase = 'completed'
    $deletionJournal.Receipt.staged_asset_removed = $true
    Write-QuarantineReceipt -ReceiptPath $deletionJournal.ReceiptPath -Receipt $deletionJournal.Receipt
    $completionReceipt = Get-Content -LiteralPath $deletionJournal.ReceiptPath -Raw | ConvertFrom-Json
    Assert-True -Condition ([string]$completionReceipt.phase -ceq 'completed') -Message 'durable deletion tombstone was not completed'
    $completedResume = Restore-PriorCheckpointQuarantine -CaseRoot $caseRoot -ExactTime '0.6' -Asset 'synthetic_delete_t0p6.tar' -ExpectedStagedAsset $stagedAsset -LockProcess $lock
    Assert-True -Condition ($completedResume.State -ceq 'DeletionCompleted') -Message 'completed deletion tombstone was not resumable'

    Write-Output 'PASS: archive helper synthetic read-only, lock, tar, verification, rollback, resume, and delete tests'
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        $resolvedTestRoot = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $testRoot).Path)
        $tempPrefix = $tempBase + [IO.Path]::DirectorySeparatorChar
        if (
            -not $resolvedTestRoot.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase) -or
            -not ([IO.Path]::GetFileName($resolvedTestRoot)).StartsWith(
                'thermal sim archive helper [',
                [StringComparison]::Ordinal)
        ) {
            throw "Refusing unsafe synthetic cleanup target: $resolvedTestRoot"
        }
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
