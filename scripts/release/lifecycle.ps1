# Windows PowerShell 5.1. No game discovery, registry access or game execution.
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][ValidateSet('Install','Commit','Recover','Restore','Uninstall','Inspect','CacheInfo')][string]$Action,
  [Parameter(Mandatory=$true)][string]$Destination,
  [string]$Payload,
  [string]$SummaryPath,
  [ValidateSet('portable','inno')][string]$Owner = 'portable',
  [switch]$TransferLegacy, [switch]$DeferCommit, [switch]$RemoveSettings,
  [switch]$RemoveShaderCache,
  # Failure injection is inert unless explicitly passed; used only by disposable fixtures.
  [int]$FailAfter = -1, [switch]$SimulateInterruption
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
try { (Get-Process -Id $PID).PriorityClass = 'BelowNormal' } catch { }
if (Get-Process -Name bg3,bg3_dx11,BG3RTX -ErrorAction SilentlyContinue) {
  throw 'Close the game and BG3RTX settings, then retry. No process was stopped.'
}
function Safe([string]$p) {
  $p = [IO.Path]::GetFullPath($p)
  if ($p -match '(?i)[\\/]Larian Studios([\\/]|$)') { throw 'Refusing protected profile path.' }
  for ($q = $p; $q; $q = Split-Path $q) {
    if (Test-Path -LiteralPath $q) {
      if ((Get-Item -LiteralPath $q -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Refusing reparse point: $q" }
      foreach ($exe in @('bin\bg3.exe','bin\bg3_dx11.exe','bg3.exe','bg3_dx11.exe')) {
        if (Test-Path -LiteralPath (Join-Path $q $exe)) { throw "Refusing game directory: $q" }
      }
    }
  }
  return $p
}
function Under([string]$root, [string]$rel) {
  if (-not $rel -or $rel -match '(^[\\/]|[:<>"|*?\[\]\x00-\x1f]|[. ]([\\/]|$)|(^|[\\/])\.{1,2}([\\/]|$))') { throw "Unsafe relative path: $rel" }
  foreach ($segment in ($rel -split '[\\/]')) {
    if ($segment -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') { throw "Reserved device path: $rel" }
  }
  $p = Safe (Join-Path $root $rel)
  if (-not $p.StartsWith($root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Path escape: $rel" }
  if ((Test-Path -LiteralPath $p) -and -not (Test-Path -LiteralPath $p -PathType Leaf)) { throw "Expected file: $p" }
  return $p
}
function PayloadPath([string]$root, [string]$rel) {
  if ($rel -match '^(?i:\.bg3rtx[^\\/]*|install_manifest\.txt|setup-support|unins\d{3}\.(?:exe|dat|msg)|bg3rtx_user\.ini|bg3rtx_launcher\.ini)([\\/]|$)') { throw "Reserved payload path: $rel" }
  return Under $root $rel
}
function ReadJson([string]$p) { $null = Safe $p; return ([IO.File]::ReadAllText($p) | ConvertFrom-Json) }
function WriteJson([string]$p, $value) {
  $null = Safe $p
  $tmp = $p + '.new'
  $null = Under (Split-Path $p) ([IO.Path]::GetFileName($tmp))
  [IO.File]::WriteAllText($tmp, ($value | ConvertTo-Json -Depth 12), (New-Object Text.UTF8Encoding($false)))
  if (Test-Path -LiteralPath $p) {
    $prior = Under (Split-Path $p) ([IO.Path]::GetFileName($p) + '.old')
    [IO.File]::Replace($tmp, $p, $prior, $true)
    [IO.File]::Delete($prior)
  } else { [IO.File]::Move($tmp, $p) }
}
function CopyFile([string]$from, [string]$to) {
  Tick 'Copying/staging'
  $null = Safe $from; $null = Safe $to
  New-Item -ItemType Directory -Force -Path (Split-Path $to) | Out-Null
  [IO.File]::Copy($from, $to, $true)
}
function DeleteFile([string]$p) { Tick 'Removing recorded files'; $null = Safe $p; if (Test-Path -LiteralPath $p -PathType Leaf) { [IO.File]::Delete($p) } }
function Hash([string]$p) { Tick 'Verifying SHA256'; return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash }
$script:operations = 0L
$script:timer = [Diagnostics.Stopwatch]::StartNew()
function Tick([string]$phase) {
  $script:operations++
  if ($script:timer.Elapsed.TotalSeconds -ge 30) {
    Write-Host "$phase : $script:operations file operations so far; transaction is still running."
    $script:timer.Restart()
  }
}
function Writable([string]$root) {
  $parent = $root
  while (-not (Test-Path -LiteralPath $parent)) { $parent = Split-Path $parent }
  $probe = Under $parent ('bg3rtx-write-' + [Guid]::NewGuid().ToString('N') + '.tmp')
  $stream = [IO.FileStream]::new($probe, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None, 1, [IO.FileOptions]::DeleteOnClose)
  $stream.Dispose()
}
function CacheLeaves([string]$dir) {
  $null = Safe $dir
  if (Test-Path -LiteralPath $dir) {
    foreach ($item in Get-ChildItem -LiteralPath $dir -Force) {
      $null = Safe $item.FullName
      if ($item.PSIsContainer) { CacheLeaves $item.FullName } else { $item.FullName }
    }
  }
}
function ValidateEntries($entries, [string]$root, [switch]$Verify) {
  $seen = @{}
  foreach ($e in @($entries)) {
    $p = PayloadPath $root $e.path
    $key = $p.ToLowerInvariant()
    if ($seen.ContainsKey($key)) { throw "Duplicate file: $($e.path)" }; $seen[$key] = $true
    if ($e.sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw "Invalid SHA256: $($e.path)" }
    if ($Verify -and ((-not (Test-Path -LiteralPath $p -PathType Leaf)) -or (Hash $p) -ine $e.sha256)) { throw "Integrity check failed: $($e.path)" }
  }
}
$dst = (Safe $Destination).TrimEnd('\')
if ($dst -eq [IO.Path]::GetPathRoot($dst).TrimEnd('\')) { throw 'Refusing a volume root.' }
$digest = [Security.Cryptography.SHA256]::Create()
try { $id = -join ($digest.ComputeHash([Text.Encoding]::UTF8.GetBytes($dst.ToLowerInvariant())) | ForEach-Object { $_.ToString('x2') }) } finally { $digest.Dispose() }
$store = Safe (Join-Path $env:LOCALAPPDATA "BG3RTX\backups\$id")
if ($dst -ieq $store -or $dst.StartsWith($store + '\',[StringComparison]::OrdinalIgnoreCase) -or $store.StartsWith($dst + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Install and backup paths must be separate.' }
$statePath = Under $dst '.bg3rtx-state.json'
$journalPath = Under $store 'pending.json'
$previousPath = Under $store 'previous.json'
# Serialize the entire lifecycle, including recovery. A second invocation never races a writer.
New-Item -ItemType Directory -Force -Path $store | Out-Null
$lock = [IO.File]::Open((Under $store 'lifecycle.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
function Snapshot($rels, [string]$folder) {
  $records = @()
  foreach ($rel in @($rels | Sort-Object -Unique)) {
    $from = Under $dst $rel
    $exists = Test-Path -LiteralPath $from -PathType Leaf
    $sha = $null
    if ($exists) { $sha = Hash $from; CopyFile $from (Under $folder $rel) }
    # Get-Content attaches provider metadata to strings. Strip that wrapper before JSON
    # serialization: otherwise PowerShell 5.1 walks the provider/drive object graph.
    $records += [pscustomobject]@{path=[string]::Copy($rel); exists=[bool]$exists; sha256=$sha}
  }
  return ,$records
}
function VerifySnapshot($snap) {
  if ($snap.destination -ine $dst) { throw 'Snapshot destination mismatch.' }
  if ($snap.folder -notmatch '^txn-[a-f0-9]{32}\\snapshot$') { throw 'Invalid snapshot location.' }
  $folder = Safe (Join-Path $store $snap.folder)
  $seen = @{}
  foreach ($r in @($snap.records)) {
    $null = Under $dst $r.path
    $source = Under $folder $r.path
    if ($seen.ContainsKey($r.path.ToLowerInvariant())) { throw 'Duplicate snapshot path.' }; $seen[$r.path.ToLowerInvariant()] = $true
    if ($r.exists -isnot [bool]) { throw 'Invalid snapshot presence flag.' }
    if ($r.exists -and ((Hash $source) -ine $r.sha256)) { throw "Corrupt recovery file: $($r.path)" }
  }
}
function ApplySnapshot($snap) {
  VerifySnapshot $snap
  foreach ($r in @($snap.records)) {
    $to = Under $dst $r.path
    if ($r.exists) { CopyFile (Under (Join-Path $store $snap.folder) $r.path) $to } else { DeleteFile $to }
  }
}
function CommitPending {
  $snap = ReadJson $journalPath
  VerifySnapshot $snap
  if ($snap.phase -ne 'applied') { throw 'Incomplete transaction; recover it first.' }
  $retired = $null
  if (Test-Path -LiteralPath $previousPath) { $retired = ReadJson $previousPath; VerifySnapshot $retired }
  WriteJson $previousPath $snap
  DeleteFile $journalPath
  # Retain one previous snapshot. Remove only recorded files, never recurse a directory.
  try {
    if ($retired -and $retired.folder -ne $snap.folder) {
      foreach ($r in $retired.records) { if ($r.exists) { DeleteFile (Under (Join-Path $store $retired.folder) $r.path) } }
    }
    $area = Split-Path (Join-Path $store $snap.folder)
    $stageList = Under $area 'stage-files.json'
    if (Test-Path -LiteralPath $stageList) {
      foreach ($e in (ReadJson $stageList).files) { DeleteFile (PayloadPath (Join-Path $area 'stage') $e.path) }
      DeleteFile $stageList
    }
  } catch { Write-Warning "Committed successfully; some staging/retired backup files were retained: $_" }
}
function PreviousAvailable {
  if (-not (Test-Path -LiteralPath $previousPath)) { return $false }
  $prior = ReadJson $previousPath
  VerifySnapshot $prior
  return [bool]@($prior.records | Where-Object { $_.path -eq '.bg3rtx-state.json' -and $_.exists }).Count
}
try {
  if ($Action -eq 'CacheInfo') {
    $summary = Safe $SummaryPath
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if (-not $summary.StartsWith($tempRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Cache summary must be inside the temporary directory.' }
    $files = @(CacheLeaves (Join-Path $env:LOCALAPPDATA 'BG3RTX\shader_cache'))
    $bytes = 0L; foreach ($p in $files) { $bytes += (Get-Item -LiteralPath $p).Length }
    [IO.File]::WriteAllText($summary, "$bytes bytes in $($files.Count) files")
    return
  }
  if ($Action -eq 'Recover') {
    if (Test-Path -LiteralPath $journalPath) { ApplySnapshot (ReadJson $journalPath); DeleteFile $journalPath; Write-Host 'Previous complete state recovered.' }
    return
  }
  if ($Action -eq 'Commit') { CommitPending; return }
  if (Test-Path -LiteralPath $journalPath) { throw 'Interrupted transaction found. Run Recover before retrying or launching.' }
  $state = $null
  if (Test-Path -LiteralPath $statePath) {
    $state = ReadJson $statePath
    if ($state.schema -ne 1 -or $state.destination -ine $dst -or $state.owner -notin @('portable','inno')) { throw 'Invalid installation ownership.' }
    ValidateEntries $state.files $dst
  }
  if ($Action -eq 'Inspect') {
    [pscustomobject]@{destination=$dst; owner=$(if ($state) {$state.owner} else {'legacy-or-none'}); restoreAvailable=(PreviousAvailable); backupPath=$store} | ConvertTo-Json
    return
  }
  if ($state -and $state.owner -eq 'inno' -and $Owner -ne 'inno') { throw "Windows Setup owns this installation. Use its registered Apps uninstaller: $dst\setup-support\unins000.exe" }
  if ($Owner -eq 'inno') {
    $support = Safe (Join-Path $dst 'setup-support')
    foreach ($name in @('lifecycle.ps1','unins000.exe','unins000.dat','unins000.msg')) { $null = Under $support $name }
    if ((Test-Path -LiteralPath $support) -and (-not $state -or $state.owner -ne 'inno')) { throw 'Unowned setup-support directory; choose another destination.' }
  }
  $settings = @('bg3rtx_user.ini','bg3rtx_launcher.ini')
  $old = @(); $next = @(); $legacy = $false
  if ($state) { $old = @($state.files) }
  elseif (Test-Path -LiteralPath (Under $dst '.bg3rtx_install')) {
    if (-not $TransferLegacy -or $Action -ne 'Install') { throw 'Legacy installation requires explicit TransferLegacy.' }
    foreach ($rel in @(Get-Content -LiteralPath (Under $dst 'install_manifest.txt') | Where-Object { $_ })) {
      $p = PayloadPath $dst $rel
      # Retired legacy entries may already be absent. Their original collisions are retained.
      $sha = ('0' * 64); if (Test-Path -LiteralPath $p) { $sha = Hash $p }
      $old += [pscustomobject]@{path=[string]::Copy($rel); sha256=$sha}
      $null = Under (Join-Path $dst '.bg3rtx_backup') $rel
    }
    ValidateEntries $old $dst
    $legacy = $true
  } elseif ((Test-Path -LiteralPath (Under $dst 'install_manifest.txt')) -or (Test-Path -LiteralPath (Join-Path $dst '.bg3rtx_backup'))) {
    throw 'Orphaned legacy metadata. Preserve the folder for recovery; do not adopt it.'
  }
  if ($Action -eq 'Install') {
    if (-not $Payload) { throw 'Payload is required.' }
    $src = (Safe $Payload).TrimEnd('\')
    if ($src -ieq $dst -or $src.StartsWith($dst+'\',[StringComparison]::OrdinalIgnoreCase) -or $dst.StartsWith($src+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Payload and destination must be separate.' }
    $manifest = ReadJson (Under $src 'payload.sha256.json')
    if ($manifest.schema -ne 1 -or -not @($manifest.files).Count) { throw 'Invalid or empty payload.' }
    $next = @($manifest.files)
    ValidateEntries $next $src -Verify
    ValidateEntries $next $dst
    if (-not (@($next.path) -icontains 'bin\BG3RTX.exe') -and -not (@($next.path) -icontains 'bin/BG3RTX.exe')) { throw 'Launcher missing from payload.' }
    foreach ($e in $next) {
      if ((Test-Path -LiteralPath (PayloadPath $dst $e.path)) -and $e.path -notin @($old | ForEach-Object { $_.path })) { throw "Unowned file collision: $($e.path)" }
    }
    $rels = @($old | ForEach-Object { $_.path }) + @($next.path) + $settings + @('.bg3rtx-state.json','.bg3rtx_install','install_manifest.txt')
  } elseif ($Action -eq 'Restore') {
    if (-not $state) { throw 'No installation to restore.' }
    if (-not (PreviousAvailable)) { throw 'No previous complete lifecycle installation exists.' }
    $restore = ReadJson $previousPath; VerifySnapshot $restore
    $rels = @($restore.records.path)
  } elseif ($Action -eq 'Uninstall') {
    if (-not $state) { throw 'No lifecycle-owned installation.' }
    $rels = @($old | ForEach-Object { $_.path }) + @($state.legacyFiles) + @('.bg3rtx-state.json','.bg3rtx_install','install_manifest.txt')
    if ($RemoveSettings) { $rels += $settings }
  }
  # All touched paths and all legacy collision backups are checked before staging/journalling.
  foreach ($rel in $rels) { $null = Under $dst $rel }
  Writable $dst
  if ($state -and $state.legacyFiles) {
    foreach ($rel in $state.legacyFiles) { $null = Under (Join-Path $dst '.bg3rtx_backup') $rel }
  }
  $cacheFiles = @()
  if ($RemoveShaderCache) {
    if ($Action -ne 'Uninstall') { throw 'Cache removal is only available during uninstall.' }
    $cache = Safe (Join-Path $env:LOCALAPPDATA 'BG3RTX\shader_cache')
    $cacheFiles = @(CacheLeaves $cache)
    Write-Host "Explicit cache removal: $($cacheFiles.Count) files, $(($cacheFiles | ForEach-Object { (Get-Item -LiteralPath $_).Length } | Measure-Object -Sum).Sum) bytes."
  }
  $txn = 'txn-' + [Guid]::NewGuid().ToString('N')
  $area = Safe (Join-Path $store $txn)
  $needed = 0L
  foreach ($rel in $rels | Sort-Object -Unique) { $p = Under $dst $rel; if (Test-Path -LiteralPath $p) { $needed += (Get-Item -LiteralPath $p).Length } }
  if ($Action -eq 'Install') { foreach ($e in $next) { $needed += 2 * (Get-Item -LiteralPath (PayloadPath $src $e.path)).Length } }
  foreach ($volume in @([IO.Path]::GetPathRoot($dst),[IO.Path]::GetPathRoot($store)) | Sort-Object -Unique) {
    if ((New-Object IO.DriveInfo($volume)).AvailableFreeSpace -lt $needed) { throw 'Insufficient disk space for staging and rollback.' }
  }
  if ($Action -eq 'Install') {
    $stage = Join-Path $area 'stage'
    New-Item -ItemType Directory -Force -Path $area | Out-Null
    WriteJson (Under $area 'stage-files.json') ([pscustomobject]@{files=$next})
    $i = 0
    foreach ($e in $next) { CopyFile (PayloadPath $src $e.path) (PayloadPath $stage $e.path); $i++; Write-Progress -Activity 'Staging verified payload' -Status "$i / $($next.Count)" -PercentComplete (100*$i/$next.Count) }
    ValidateEntries $next $stage -Verify
  }
  $records = Snapshot $rels (Join-Path $area 'snapshot')
  $snap = [pscustomobject]@{schema=1; destination=$dst; folder="$txn\snapshot"; records=@($records); phase='prepared'}
  WriteJson $journalPath $snap
  try {
    if ($Action -eq 'Restore') { ApplySnapshot $restore }
    else {
      $i = 0
      foreach ($e in $old) {
        if ($Action -eq 'Uninstall' -or $e.path -notin @($next.path)) { DeleteFile (PayloadPath $dst $e.path) }
      }
      if ($Action -eq 'Install') {
        foreach ($e in $next) {
          CopyFile (PayloadPath $stage $e.path) (PayloadPath $dst $e.path); $i++
          if ($FailAfter -ge 0 -and $i -ge $FailAfter) { throw 'Injected replacement failure.' }
        }
        $legacyFiles = @(); if ($state) { $legacyFiles = @($state.legacyFiles) }; if ($legacy) { $legacyFiles = @($old.path) }
        WriteJson $statePath ([pscustomobject]@{schema=1; destination=$dst; owner=$Owner; files=$next; legacyFiles=$legacyFiles})
        DeleteFile (Under $dst '.bg3rtx_install'); DeleteFile (Under $dst 'install_manifest.txt')
      } else {
        foreach ($rel in $state.legacyFiles) {
          $saved = Under (Join-Path $dst '.bg3rtx_backup') $rel
          if (Test-Path -LiteralPath $saved) { CopyFile $saved (PayloadPath $dst $rel) }
        }
        if ($RemoveSettings) { foreach ($rel in $settings) { DeleteFile (Under $dst $rel) } }
        DeleteFile $statePath
      }
    }
    $snap.phase = 'applied'; WriteJson $journalPath $snap
    if (-not $DeferCommit) { CommitPending }
  } catch {
    if ($SimulateInterruption) { throw }
    ApplySnapshot $snap; DeleteFile $journalPath
    throw
  }
  # Cache is disposable and outside the rollback set. Never recurse-delete directories.
  foreach ($p in $cacheFiles) { DeleteFile $p }
  Write-Host "$Action complete. Settings/reports retained unless explicitly selected. Backups: $store"
} finally { $lock.Dispose() }
