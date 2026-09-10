param(
    [ValidateSet("Install","Check","Restore")]
    [string]$Mode = "Check",
    [string]$InstallDir = $PSScriptRoot,
    [switch]$Rescan
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Write-Info([string]$Text)  { Write-Host "[INFO]  $Text" -ForegroundColor Cyan }
function Write-Ok([string]$Text)    { Write-Host "[OK]    $Text" -ForegroundColor Green }
function Write-Fix([string]$Text)   { Write-Host "[FIXED] $Text" -ForegroundColor Yellow }
function Write-Warn2([string]$Text) { Write-Host "[WARN]  $Text" -ForegroundColor Yellow }
function Write-Fail([string]$Text)  { Write-Host "[FAIL]  $Text" -ForegroundColor Red }

function Get-FileHashSafe([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return "" }
    try { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant() }
    catch { return "" }
}

function Get-FileVersionSafe([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return "" }
    try {
        $v = (Get-Item -LiteralPath $Path).VersionInfo.FileVersion
        if ($null -eq $v) { return "" }
        return $v.Trim()
    } catch { return "" }
}

function Get-StringSha256([string]$Text) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text.ToLowerInvariant())
        $hash = $sha.ComputeHash($bytes)
        return ([BitConverter]::ToString($hash) -replace "-", "").Substring(0, 20)
    } finally {
        $sha.Dispose()
    }
}

function Normalize-FullPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return "" }
    $clean = $Path.Trim().Trim('"')
    return [System.IO.Path]::GetFullPath($clean).TrimEnd([char]'\')
}

$InstallDir = Normalize-FullPath $InstallDir
$OptiDir = Join-Path $InstallDir "OptiScaler"
$StreamlineDir = Join-Path $OptiDir "streamline"
$StateDir = Join-Path $OptiDir "RuntimeSync"
$BackupRoot = Join-Path $StateDir "backup"
$ManifestPath = Join-Path $StateDir "manifest.json"

if (-not (Test-Path -LiteralPath $OptiDir -PathType Container)) {
    Write-Fail "OptiScaler folder was not found: $OptiDir"
    exit 2
}

# Only files that ship in our validated runtime bundle are candidates.
# nvngx_dlssnr.dll is intentionally NOT touched.
$Sources = @{}
foreach ($name in @("nvngx_dlss.dll", "nvngx_dlssd.dll", "nvngx_dlssg.dll")) {
    $p = Join-Path $OptiDir $name
    if (Test-Path -LiteralPath $p -PathType Leaf) { $Sources[$name] = $p }
}
if (Test-Path -LiteralPath $StreamlineDir -PathType Container) {
    Get-ChildItem -LiteralPath $StreamlineDir -File -Filter "sl.*.dll" -ErrorAction SilentlyContinue |
        ForEach-Object { $Sources[$_.Name.ToLowerInvariant()] = $_.FullName }
}

if ($Sources.Count -eq 0) {
    Write-Fail "No bundled DLSS / Streamline runtime files were found under OptiScaler."
    exit 3
}

function Resolve-ScanRoot([string]$StartDir) {
    $cur = New-Object System.IO.DirectoryInfo($StartDir)
    for ($i = 0; $i -lt 6 -and $null -ne $cur; $i++) {
        $engine = Join-Path $cur.FullName "Engine"
        if (Test-Path -LiteralPath $engine -PathType Container) {
            return $cur.FullName
        }
        if ($null -eq $cur.Parent) { break }
        # Never deliberately climb into a Steam/common-style shared game container.
        if ($cur.Parent.Name -ieq "common") { break }
        $cur = $cur.Parent
    }
    return $StartDir
}

$ScanRoot = Normalize-FullPath (Resolve-ScanRoot $InstallDir)

function Is-SkippedDirectory([System.IO.DirectoryInfo]$Dir) {
    $n = $Dir.Name.ToLowerInvariant()
    if ($n -eq "optiscaler") { return $true }
    if ($n -like "_storage*") { return $true }
    if ($n.StartsWith(".")) { return $true }

    $skip = @(
        "content","paks","saved","logs","movies","sounds","music","videos",
        "localization","shadercache","derivedcache","cache","textures","maps",
        "levels","audio","data","assets","mods","screenshots","redist",
        "_commonredist","crashreport","crashreports"
    )
    return $skip -contains $n
}

function Add-TargetIfMatch(
    [string]$FilePath,
    [System.Collections.Generic.HashSet[string]]$Found
) {
    if (-not (Test-Path -LiteralPath $FilePath -PathType Leaf)) { return }
    $name = [System.IO.Path]::GetFileName($FilePath).ToLowerInvariant()
    if (-not $Sources.ContainsKey($name)) { return }

    $full = Normalize-FullPath $FilePath
    # Never sync the package's own runtime copies.
    if ($full.StartsWith($OptiDir + "\", [System.StringComparison]::OrdinalIgnoreCase)) { return }
    [void]$Found.Add($full)
}

function Find-GameRuntimeTargets([string]$Root) {
    $found = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)

    # 1) Files beside the game executable / setup.
    foreach ($name in $Sources.Keys) {
        Add-TargetIfMatch (Join-Path $InstallDir $name) $found
    }

    # 2) Common Unreal Engine NVIDIA runtime locations: hit these before walking.
    $knownDirs = @(
        "Engine\Plugins\Runtime\Nvidia\DLSS\Binaries\ThirdParty\Win64",
        "Engine\Plugins\Runtime\Nvidia\StreamlineCore\Binaries\ThirdParty\Win64",
        "Engine\Plugins\Runtime\Nvidia\Streamline\Binaries\ThirdParty\Win64"
    )
    foreach ($rel in $knownDirs) {
        $d = Join-Path $Root $rel
        if (Test-Path -LiteralPath $d -PathType Container) {
            foreach ($name in $Sources.Keys) {
                Add-TargetIfMatch (Join-Path $d $name) $found
            }
        }
    }

    # 3) Bounded walk for project plugins, RE Engine layouts, CryEngine layouts, etc.
    $queue = New-Object 'System.Collections.Generic.Queue[object]'
    $queue.Enqueue([pscustomobject]@{ Path = $Root; Depth = 0 })
    $maxDepth = 9
    $maxDirs = 7000
    $seenDirs = 0
    $deadline = [DateTime]::UtcNow.AddSeconds(10)

    while ($queue.Count -gt 0) {
        if ($seenDirs -ge $maxDirs -or [DateTime]::UtcNow -gt $deadline) {
            Write-Warn2 "Runtime scan budget reached. Existing matches will still be used."
            break
        }

        $item = $queue.Dequeue()
        $dirPath = [string]$item.Path
        $depth = [int]$item.Depth
        $seenDirs++

        try {
            $items = Get-ChildItem -LiteralPath $dirPath -Force -ErrorAction Stop
        } catch {
            continue
        }

        foreach ($child in $items) {
            if ($child.PSIsContainer) {
                if ($depth -lt $maxDepth -and -not (Is-SkippedDirectory $child)) {
                    $queue.Enqueue([pscustomobject]@{ Path = $child.FullName; Depth = $depth + 1 })
                }
            } else {
                $lname = $child.Name.ToLowerInvariant()
                if ($Sources.ContainsKey($lname)) {
                    Add-TargetIfMatch $child.FullName $found
                }
            }
        }
    }

    return @($found)
}

function Load-Manifest {
    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) { return $null }
    try {
        return (Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json)
    } catch {
        Write-Warn2 "Manifest is unreadable; a new one will be created."
        return $null
    }
}

function Save-Manifest($Entries) {
    New-Item -ItemType Directory -Force -Path $StateDir | Out-Null
    $obj = [pscustomobject]@{
        SchemaVersion = 1
        InstallDir = $InstallDir
        ScanRoot = $ScanRoot
        UpdatedAt = (Get-Date).ToString("o")
        Entries = @($Entries)
    }
    $json = $obj | ConvertTo-Json -Depth 7
    [System.IO.File]::WriteAllText($ManifestPath, $json, (New-Object System.Text.UTF8Encoding($false)))
}

function Find-Entry($Entries, [string]$TargetPath) {
    return @($Entries | Where-Object { $_.TargetPath -ieq $TargetPath }) | Select-Object -First 1
}

function Save-TargetBackup([string]$TargetPath, [string]$ExpectedHash) {
    $id = Get-StringSha256 $TargetPath
    $backupDir = Join-Path $BackupRoot $id
    New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
    $backupPath = Join-Path $backupDir ([System.IO.Path]::GetFileName($TargetPath))
    $tempPath = $backupPath + ".tmp"

    try {
        Copy-Item -LiteralPath $TargetPath -Destination $tempPath -Force
        $backupHash = Get-FileHashSafe $tempPath
        if ([string]::IsNullOrWhiteSpace($backupHash) -or $backupHash -ne $ExpectedHash) {
            throw "Backup verification failed: $TargetPath"
        }
        Move-Item -LiteralPath $tempPath -Destination $backupPath -Force
    } finally {
        if (Test-Path -LiteralPath $tempPath -PathType Leaf) {
            Remove-Item -LiteralPath $tempPath -Force -ErrorAction SilentlyContinue
        }
    }

    return $backupPath
}

function Sync-One($Entries, [string]$TargetPath, [bool]$AllowBackup) {
    $name = [System.IO.Path]::GetFileName($TargetPath).ToLowerInvariant()
    if (-not $Sources.ContainsKey($name)) {
        Write-Warn2 "No bundled source for $name"
        return [pscustomobject]@{ Entries=$Entries; Status="Skip" }
    }

    $source = $Sources[$name]
    $sourceHash = Get-FileHashSafe $source
    $sourceVer = Get-FileVersionSafe $source

    if (-not (Test-Path -LiteralPath $TargetPath -PathType Leaf)) {
        Write-Warn2 "Missing target, left untouched: $TargetPath"
        return [pscustomobject]@{ Entries=$Entries; Status="Missing" }
    }

    $currentHash = Get-FileHashSafe $TargetPath
    $currentVer = Get-FileVersionSafe $TargetPath
    $entry = Find-Entry $Entries $TargetPath

    if ($null -eq $entry) {
        $backupPath = ""
        if ($AllowBackup -and $currentHash -ne $sourceHash) {
            try {
                $backupPath = Save-TargetBackup $TargetPath $currentHash
            } catch {
                Write-Fail "Could not create a verified backup; target left untouched: $TargetPath"
                return [pscustomobject]@{ Entries=$Entries; Status="Fail" }
            }
        }

        $entry = [pscustomobject]@{
            TargetPath = $TargetPath
            SourceName = $name
            BackupPath = $backupPath
            OriginalHash = $currentHash
            OriginalVersion = $currentVer
            DeployedHash = $sourceHash
            DeployedVersion = $sourceVer
        }
        $Entries = @($Entries) + $entry
    } else {
        $previousDeployedHash = [string]$entry.DeployedHash
        $currentIsPreviousAurora = (-not [string]::IsNullOrWhiteSpace($previousDeployedHash)) -and ($currentHash -eq $previousDeployedHash)

        # If the launcher/game changed a managed file after Aurora was installed,
        # preserve that newer game-owned state before repairing it again.
        if ($AllowBackup -and $currentHash -ne $sourceHash -and -not $currentIsPreviousAurora) {
            try {
                $entry.BackupPath = Save-TargetBackup $TargetPath $currentHash
                $entry.OriginalHash = $currentHash
                $entry.OriginalVersion = $currentVer
                Write-Info "Refreshed restore backup for $name"
            } catch {
                Write-Fail "Could not refresh the restore backup; target left untouched: $TargetPath"
                return [pscustomobject]@{ Entries=$Entries; Status="Fail" }
            }
        }

        # If the original game file happened to equal Aurora's previous runtime, there
        # was no backup to create at first install. Preserve it before a future Aurora
        # runtime upgrade changes the deployed hash.
        if ($AllowBackup -and $currentHash -ne $sourceHash -and $currentIsPreviousAurora -and
            [string]::IsNullOrWhiteSpace([string]$entry.BackupPath) -and
            [string]$entry.OriginalHash -eq $previousDeployedHash) {
            try {
                $entry.BackupPath = Save-TargetBackup $TargetPath $currentHash
            } catch {
                Write-Fail "Could not preserve the previous runtime before upgrade; target left untouched: $TargetPath"
                return [pscustomobject]@{ Entries=$Entries; Status="Fail" }
            }
        }

        $entry.DeployedHash = $sourceHash
        $entry.DeployedVersion = $sourceVer
        $entry.SourceName = $name
    }

    if ($currentHash -eq $sourceHash) {
        $label = $name
        if ($sourceVer) { $label += " [$sourceVer]" }
        Write-Ok $label
        return [pscustomobject]@{ Entries=$Entries; Status="OK" }
    }

    try {
        Copy-Item -LiteralPath $source -Destination $TargetPath -Force
        $afterHash = Get-FileHashSafe $TargetPath
        if ($afterHash -ne $sourceHash) {
            Write-Fail "Verification failed after replacing: $TargetPath"
            return [pscustomobject]@{ Entries=$Entries; Status="Fail" }
        }

        $from = if ($currentVer) { $currentVer } else { "different build" }
        $to = if ($sourceVer) { $sourceVer } else { "bundled build" }
        Write-Fix "$name : $from -> $to"
        return [pscustomobject]@{ Entries=$Entries; Status="Fixed" }
    } catch {
        Write-Fail "$name could not be replaced. Close the game first. $($_.Exception.Message)"
        return [pscustomobject]@{ Entries=$Entries; Status="Fail" }
    }
}

function Restore-All($Manifest) {
    if ($null -eq $Manifest -or @($Manifest.Entries).Count -eq 0) {
        Write-Info "No managed game runtime files were recorded."
        return 0
    }

    $restored = 0
    $kept = 0
    $failed = 0
    foreach ($entry in @($Manifest.Entries)) {
        $target = [string]$entry.TargetPath
        $backup = [string]$entry.BackupPath
        if ([string]::IsNullOrWhiteSpace($backup) -or -not (Test-Path -LiteralPath $backup -PathType Leaf)) {
            $kept++
            continue
        }

        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
            Write-Warn2 "Target is gone; not restoring an old game file: $target"
            $kept++
            continue
        }

        $backupHash = Get-FileHashSafe $backup
        $expectedBackupHash = [string]$entry.OriginalHash
        if ([string]::IsNullOrWhiteSpace($backupHash) -or
            (-not [string]::IsNullOrWhiteSpace($expectedBackupHash) -and $backupHash -ne $expectedBackupHash)) {
            Write-Fail "Backup verification failed; target left untouched: $target"
            $failed++
            continue
        }

        $currentHash = Get-FileHashSafe $target
        if ($currentHash -ne [string]$entry.DeployedHash) {
            Write-Warn2 "Game/launcher changed this file after install; leaving it untouched: $target"
            $kept++
            continue
        }

        try {
            Copy-Item -LiteralPath $backup -Destination $target -Force
            Write-Ok "Restored $([System.IO.Path]::GetFileName($target))"
            $restored++
        } catch {
            Write-Fail "Could not restore: $target"
            $failed++
        }
    }

    Write-Host ""
    Write-Info "Restore complete. Restored: $restored  Left untouched: $kept  Failed: $failed"
    return $failed
}

Write-Host ""
Write-Host "============================================================" -ForegroundColor DarkGray
Write-Host " OptiScaler Aurora DLSS / Streamline Runtime Sync v1.2 / 运行库同步 v1.2" -ForegroundColor White
Write-Host "============================================================" -ForegroundColor DarkGray
Write-Info "Install folder: $InstallDir"
Write-Info "Scan root:      $ScanRoot"
Write-Host ""

$manifest = Load-Manifest

if ($Mode -eq "Restore") {
    $restoreFailed = Restore-All $manifest
    if ([int]$restoreFailed -gt 0) { exit 5 }
    exit 0
}

$entries = @()
if ($null -ne $manifest) { $entries = @($manifest.Entries) }

$targets = @()
$useFastCheck = ($Mode -eq "Check" -and -not $Rescan -and $entries.Count -gt 0)

if ($useFastCheck) {
    Write-Info "Using saved manifest for a fast self-check."
    $targets = @($entries | ForEach-Object { [string]$_.TargetPath } | Sort-Object -Unique)
} else {
    Write-Info "Scanning this game for its own DLSS / Streamline runtime files..."
    $targets = @(Find-GameRuntimeTargets $ScanRoot | Sort-Object -Unique)
}

if ($targets.Count -eq 0) {
    Write-Warn2 "No game-owned matching runtime files were found."
    Write-Info "Nothing was changed."
    exit 0
}

Write-Info "Targets: $($targets.Count)"
Write-Host ""

$ok = 0
$fixed = 0
$failed = 0
$missing = 0

foreach ($target in $targets) {
    $r = Sync-One $entries $target $true
    $entries = @($r.Entries)
    switch ($r.Status) {
        "OK"      { $ok++ }
        "Fixed"   { $fixed++ }
        "Fail"    { $failed++ }
        "Missing" { $missing++ }
    }
}

Save-Manifest $entries

Write-Host ""
Write-Host "------------------------------------------------------------" -ForegroundColor DarkGray
Write-Info "Checked: $($targets.Count)  Already correct: $ok  Repaired: $fixed  Failed: $failed  Missing: $missing"
if ($failed -eq 0) {
    Write-Ok "Runtime set is ready."
    Write-Host "        If this game uses a launcher, you can now click Start Game." -ForegroundColor Green
} else {
    Write-Warn2 "Some files could not be repaired. Close the game and retry."
}
Write-Host ""

if ($failed -gt 0) { exit 4 }
exit 0
