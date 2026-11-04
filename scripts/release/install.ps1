# Per-user install. No elevation, registry or game changes.
# GameDir is an optional safety check, not the installation destination.
param([string]$Destination = (Join-Path $env:LOCALAPPDATA 'Programs\BG3RTX'),
      [string]$GameDir = '', [switch]$NoShortcut, [switch]$WhatIf)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath (Join-Path $Destination '.bg3rtx-state.json')) {
  throw 'This installation uses the transactional lifecycle. Update it with Windows Setup or lifecycle.ps1, not the legacy installer.'
}
try { (Get-Process -Id $PID).PriorityClass = 'BelowNormal' } catch { }
function Assert-Safe([string]$path) {
  if ($path -match '(?i)[\\/]Larian Studios([\\/]|$)') { throw "Refusing profile folder (saves): $path" }
  for ($p = $path; $p; $p = Split-Path $p) {
    if (Test-Path -LiteralPath $p) {
      if ((Get-Item -LiteralPath $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Refusing linked path: $p" }
      foreach ($exe in @('bin\bg3.exe', 'bin\bg3_dx11.exe', 'bg3.exe', 'bg3_dx11.exe')) {
        if (Test-Path -LiteralPath (Join-Path $p $exe)) { throw "Refusing game folder: $p" }
      }
    }
  }
}
function Inside([string]$root, [string]$rel) {
  if (-not $rel -or $rel -match '(^[\\/]|[:<>"|*?\[\]\x00-\x1f]|[. ]([\\/]|$)|(^|[\\/])\.{1,2}([\\/]|$))' -or
      $rel -match '^(?i:install_manifest\.txt|\.bg3rtx_install|\.bg3rtx_backup)([\\/]|$)' -or
      $rel -ieq 'bg3rtx_user.ini') { throw "Unsafe manifest entry: $rel" }
  $p = [IO.Path]::GetFullPath((Join-Path $root $rel))
  if (-not $p.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Path escapes install: $rel" }
  Assert-Safe $p
  return $p
}
if (Get-Process -Name bg3, bg3_dx11, BG3RTX -ErrorAction SilentlyContinue) { throw 'Close the game and BG3RTX settings first.' }
$src = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$dst = [IO.Path]::GetFullPath($Destination).TrimEnd('\')
Assert-Safe $src
Assert-Safe $dst
if ($GameDir) {
  $game = [IO.Path]::GetFullPath($GameDir).TrimEnd('\')
  if (-not (Test-Path -LiteralPath (Join-Path $game 'bin\bg3.exe') -PathType Leaf)) { throw 'GameDir must contain bin\bg3.exe.' }
  if ($dst -ieq $game -or $dst.StartsWith($game + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Pick a destination outside GameDir.' }
}
if ($src -ieq $dst -or $src.StartsWith($dst + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $dst.StartsWith($src + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Use separate folders. For portable use, run bin\BG3RTX.exe.' }
$list = Join-Path $src 'files.txt'
Assert-Safe $list
if (-not (Test-Path -LiteralPath $list -PathType Leaf)) { throw 'files.txt missing: run from the unzipped package.' }
$files = @(Get-Content -LiteralPath $list | Where-Object { $_ })
if (-not $files.Count) { throw 'Empty package manifest.' }
$manifest = Join-Path $dst 'install_manifest.txt'
$marker = Join-Path $dst '.bg3rtx_install'
$backup = Join-Path $dst '.bg3rtx_backup'
foreach ($p in @($manifest, $marker, $backup)) { Assert-Safe $p }
$old = @()
if (Test-Path -LiteralPath $marker) {
  if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { throw 'Install manifest missing; preserve this folder for recovery.' }
  $old = @(Get-Content -LiteralPath $manifest | Where-Object { $_ })
} elseif ((Test-Path -LiteralPath $manifest) -or (Test-Path -LiteralPath $backup)) { throw 'Installer metadata exists without a marker.' }
if (-not (Test-Path -LiteralPath $marker)) {
  # These trees are removed as runtime output on uninstall. Do not adopt existing data.
  foreach ($rel in @('captures', 'sl_logs', 'bin\hot')) {
    if (Test-Path -LiteralPath (Join-Path $dst $rel)) { throw "Existing runtime folder: $rel. Pick a separate destination." }
  }
}
# Preflight everything before writing or removing any file.
foreach ($f in $files) {
  $from = Inside $src $f
  $to = Inside $dst $f
  $save = Inside $backup $f
  if (-not (Test-Path -LiteralPath $from -PathType Leaf)) { throw "Package incomplete: $f" }
  foreach ($p in @($to, $save)) {
    if ((Test-Path -LiteralPath $p) -and -not (Test-Path -LiteralPath $p -PathType Leaf)) { throw "Expected a file: $p" }
  }
}
foreach ($f in $old) { $null = Inside $dst $f }
if ($WhatIf) { Write-Host "Would install $($files.Count) files in $dst. Originals would be backed up. Nothing changed."; return }
# Stage and verify the payload before modifying the installation. Keep previous
# files until both the payload and its ownership metadata have committed.
$stage = Join-Path ([IO.Path]::GetTempPath()) ('bg3rtx-install-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $stage | Out-Null
$journal = @()
$createdBackups = @()
$backupTemps = @()
$hadBackup = Test-Path -LiteralPath $backup
$rollbackFailed = $false
function Copy-Verified([string]$from, [string]$to) {
  New-Item -ItemType Directory -Force -Path (Split-Path $to) | Out-Null
  Copy-Item -LiteralPath $from -Destination $to -Force
  if ((Get-FileHash -LiteralPath $from).Hash -ne (Get-FileHash -LiteralPath $to).Hash) { throw "Copy verification failed: $to" }
}
try {
  foreach ($f in $files) { Copy-Verified (Inside $src $f) (Inside $stage ('payload\' + $f)) }
  $targets = @($files + $old | Sort-Object -Unique) + @('install_manifest.txt', '.bg3rtx_install')
  foreach ($f in $targets) {
    $to = Join-Path $dst $f
    $previous = Join-Path $stage ('previous\' + $f)
    $exists = Test-Path -LiteralPath $to -PathType Leaf
    if ($exists) { Copy-Verified $to $previous }
    $journal += @{ Path = $to; Previous = $previous; Existed = $exists }
  }
  # Only verified, atomically renamed backups are authoritative on retry.
  foreach ($f in $files) {
    $to = Inside $dst $f
    $save = Inside $backup $f
    if ($f -notin $old -and (Test-Path -LiteralPath $to) -and -not (Test-Path -LiteralPath $save)) {
      $tmp = $save + '.' + [guid]::NewGuid() + '.tmp'
      Assert-Safe $tmp
      $backupTemps += $tmp
      Copy-Verified $to $tmp
      Move-Item -LiteralPath $tmp -Destination $save -Force
      $createdBackups += $save
    }
  }
  foreach ($f in $files) { Copy-Verified (Inside $stage ('payload\' + $f)) (Inside $dst $f) }
  foreach ($f in $old) {
    if ($f -notin $files) {
      $to = Inside $dst $f
      if (Test-Path -LiteralPath $to -PathType Leaf) { Remove-Item -LiteralPath $to -Force }
    }
  }
  @($old + $files | Sort-Object -Unique) | Set-Content -LiteralPath $manifest -Encoding UTF8
  'BG3RTX per-user installation' | Set-Content -LiteralPath $marker -Encoding UTF8
} catch {
  $failure = $_
  foreach ($entry in $journal) {
    try {
      if ($entry.Existed) { Copy-Verified $entry.Previous $entry.Path }
      elseif (Test-Path -LiteralPath $entry.Path -PathType Leaf) { Remove-Item -LiteralPath $entry.Path -Force }
    } catch { $rollbackFailed = $true; Write-Warning "Could not restore $($entry.Path). Recovery files: $stage" }
  }
  if ($rollbackFailed) { throw "Install failed and rollback needs recovery from $stage. Original error: $failure" }
  foreach ($save in @($createdBackups + $backupTemps)) {
    if (Test-Path -LiteralPath $save -PathType Leaf) { Remove-Item -LiteralPath $save -Force }
  }
  if (-not $hadBackup -and (Test-Path -LiteralPath $backup)) {
    Get-ChildItem -LiteralPath $backup -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending | ForEach-Object {
      if (-not (Get-ChildItem -LiteralPath $_.FullName -Force)) { [IO.Directory]::Delete($_.FullName, $false) }
    }
    if (-not (Get-ChildItem -LiteralPath $backup -Force)) { [IO.Directory]::Delete($backup, $false) }
  }
  throw $failure
} finally {
  # This is a freshly created temp tree, never an installation or shared tree.
  $resolvedStage = [IO.Path]::GetFullPath($stage)
  if (-not $resolvedStage.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()), [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe staging cleanup path' }
  if (-not $rollbackFailed) { Remove-Item -LiteralPath $resolvedStage -Recurse -Force }
}
if (-not $NoShortcut) {
  $lnk = Join-Path ([Environment]::GetFolderPath('Programs')) 'BG3RTX.lnk'
  if (Test-Path -LiteralPath $lnk) {
    $target = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk).TargetPath
    if ($target -ine (Join-Path $dst 'bin\BG3RTX.exe')) { Write-Warning 'Existing shortcut points elsewhere; kept it.'; $lnk = $null }
  }
  if ($lnk) {
    $s = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
    $s.TargetPath = Join-Path $dst 'bin\BG3RTX.exe'
    $s.WorkingDirectory = Join-Path $dst 'bin'
    $s.Description = 'BG3RTX settings and launcher'
    $s.Save()
  }
}
$exe = Join-Path $dst 'bin\BG3RTX.exe'
Write-Host "BG3RTX installed in $dst. Start $exe; Browse selects the game if needed."
Write-Host "Steam launch option: `"$exe`" %command%"
Write-Host 'Use Vulkan and DLSS. Uninstall with Uninstall.cmd.'
