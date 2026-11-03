# BG3RTX uninstaller (ships in the package root; scripts/package.ps1). Windows PowerShell 5.1 or later.
#
# Removes, inside the install folder only: the files install_manifest.txt lists, the files BG3RTX
# writes while it runs (bg3rtx.log, bg3rtx_user.ini, bg3rtx_launcher.ini, bin\hot\, sl_logs\,
# captures\), then the empty folders. Outside it: the Start menu shortcut and BG3RTX's shader cache
# (%LOCALAPPDATA%\BG3RTX\shader_cache). BG3RTX writes nothing else: no registry, no Vulkan layer
# registration, nothing in the game folder. Your saves are never read or touched.
#
#   Uninstall.cmd                                   (double-click)
#   powershell -ExecutionPolicy Bypass -File uninstall.ps1 [-InstallDir <folder>] [-KeepSettings] [-WhatIf]
param([string]$InstallDir = $PSScriptRoot, [string]$GameDir = '', [switch]$NoShortcut,
      [switch]$KeepSettings, [switch]$KeepShaderCache, [switch]$WhatIf,
      [string]$ShaderCache = (Join-Path $env:LOCALAPPDATA "BG3RTX\shader_cache"))
$ErrorActionPreference = "Stop"
try { (Get-Process -Id $PID).PriorityClass = 'BelowNormal' } catch { }
$dir = [IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
if (Test-Path -LiteralPath (Join-Path $dir '.bg3rtx-state.json')) {
  throw "This installation uses the transactional lifecycle. For Windows Setup ownership, use Windows Settings > Apps > BG3RTX > Uninstall ($dir\setup-support\unins000.exe). Do not remove its files with the legacy script."
}
if ($GameDir) {
  $game = [IO.Path]::GetFullPath($GameDir).TrimEnd('\')
  if (-not (Test-Path -LiteralPath (Join-Path $game 'bin\bg3.exe') -PathType Leaf)) { throw 'GameDir must contain bin\bg3.exe.' }
  if ($dir -ieq $game -or $dir.StartsWith($game + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Refusing GameDir.' }
}
if (-not (Test-Path -LiteralPath (Join-Path $dir ".bg3rtx_install") -PathType Leaf)) { throw "$dir is not a BG3RTX install (no .bg3rtx_install)" }
if (Get-Process -Name bg3, bg3_dx11 -ErrorAction SilentlyContinue) { throw "Baldur's Gate 3 is running: close it first" }
if (Get-Process -Name BG3RTX -ErrorAction SilentlyContinue) { throw "BG3RTX.exe is open: close the settings window first" }

$removed = 0
function Test-Safe([string]$p) {
  # Refuse anything in Larian's profile folder (saves) or in a game folder.
  if ($p -match '(?i)[\\/]Larian Studios([\\/]|$)') { return $false }
  for ($q = $p; $q; $q = Split-Path $q) {
    if ((Test-Path -LiteralPath $q) -and
        ((Get-Item -LiteralPath $q -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { return $false }
    foreach ($exe in @('bin\bg3.exe', 'bin\bg3_dx11.exe', 'bg3.exe', 'bg3_dx11.exe')) {
      if (Test-Path -LiteralPath (Join-Path $q $exe)) { return $false }
    }
  }
  return $true
}
function Remove-OneFile([string]$p) {
  if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { return }
  if (-not (Test-Safe $p)) { Write-Warning "skipped (protected): $p"; return }
  if ($WhatIf) { Write-Host "would remove $p" } else { Remove-Item -LiteralPath $p -Force }
  $script:removed++
}
# Files of a folder, recursively, without following junctions or symbolic links.
function Remove-Tree([string]$d) {
  if (-not (Test-Safe $d)) { Write-Warning "skipped protected or linked tree: $d"; return }
  $item = Get-Item -LiteralPath $d -Force -ErrorAction SilentlyContinue
  if (-not $item -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { return }
  foreach ($c in Get-ChildItem -LiteralPath $d -Force) {
    if ($c.Attributes -band [IO.FileAttributes]::ReparsePoint) { Write-Warning "skipped link: $($c.FullName)"; continue }
    if ($c.PSIsContainer) { Remove-Tree $c.FullName } else { Remove-OneFile $c.FullName }
  }
  Remove-EmptyDir $d
}
function Remove-EmptyDir([string]$d) {
  if (-not (Test-Safe $d)) { return }
  if ((Test-Path -LiteralPath $d -PathType Container) -and -not (Get-ChildItem -LiteralPath $d -Force | Select-Object -First 1)) {
    if ($WhatIf) { Write-Host "would remove folder $d" } else { Remove-Item -LiteralPath $d -Force }
  }
}
function Inside([string]$rel) {
  if (-not $rel -or $rel -match '(^[\\/]|[:<>"|*?\[\]\x00-\x1f]|[. ]([\\/]|$)|(^|[\\/])\.{1,2}([\\/]|$))' -or
      $rel -match '^(?i:install_manifest\.txt|\.bg3rtx_install|\.bg3rtx_backup)([\\/]|$)' -or
      $rel -ieq 'bg3rtx_user.ini') { throw "Unsafe manifest entry: $rel" }
  $p = [IO.Path]::GetFullPath((Join-Path $dir $rel))
  if (-not $p.StartsWith($dir + '\', [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Safe $p)) { throw "Unsafe path: $p" }
  return $p
}

# 1. What the installer copied.
$manifest = Join-Path $dir "install_manifest.txt"
$backup = Join-Path $dir '.bg3rtx_backup'
foreach ($p in @($dir, $manifest, $backup, (Join-Path $dir '.bg3rtx_install'))) {
  if (-not (Test-Safe $p)) { throw "Unsafe install path: $p" }
}
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { throw 'Install manifest missing; preserve the folder for recovery.' }
$listed = @(Get-Content -LiteralPath $manifest | Where-Object { $_ })
# Preflight the whole manifest and backups before deleting anything.
foreach ($f in $listed) {
  $null = Inside $f
  $p = Join-Path $backup $f
  if (-not (Test-Safe $p)) { throw "Unsafe backup: $p" }
  if ((Test-Path -LiteralPath $p) -and -not (Test-Path -LiteralPath $p -PathType Leaf)) { throw "Invalid backup: $p" }
}
if (-not $KeepShaderCache) {
  $expectedCache = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'BG3RTX\shader_cache'))
  if ([IO.Path]::GetFullPath($ShaderCache).TrimEnd('\') -ine $expectedCache.TrimEnd('\')) { throw 'Refusing a nonstandard shader cache path; use KeepShaderCache.' }
}
foreach ($f in $listed) { $p = Inside $f; if ($p) { Remove-OneFile $p } }
# 2. What BG3RTX writes at run time (layer/util.cpp, layer/hotreload.cpp, layer/streamline.cpp, the launcher).
$written = @("bg3rtx_launcher.ini", "bg3rtx_user.ini.tmp", "trace.request")
if (-not $KeepSettings) { $written += "bg3rtx_user.ini" }
foreach ($f in $written) { Remove-OneFile (Join-Path $dir $f) }
Get-ChildItem -LiteralPath $dir -Force -File -Filter "bg3rtx*.log" | ForEach-Object { Remove-OneFile $_.FullName }
foreach ($d in @("bin\hot", "sl_logs", "captures")) { Remove-Tree (Join-Path $dir $d) }
# Restore pre-install files after runtime cleanup, before discarding ownership.
foreach ($f in $listed) {
  $saved = Join-Path $backup $f
  if (Test-Path -LiteralPath $saved -PathType Leaf) {
    $to = Inside $f
    if ($WhatIf) { Write-Host "would restore $to" } else {
      New-Item -ItemType Directory -Force -Path (Split-Path $to) | Out-Null
      Copy-Item -LiteralPath $saved -Destination $to -Force
    }
  }
}
# 3. The Start menu shortcut, if it points here.
$lnk = Join-Path ([Environment]::GetFolderPath("Programs")) "BG3RTX.lnk"
if (-not $NoShortcut -and (Test-Path -LiteralPath $lnk)) {
  $target = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk).TargetPath
  if ($target -and $target.StartsWith($dir + "\", [StringComparison]::OrdinalIgnoreCase)) {
    if ($WhatIf) { Write-Host "would remove $lnk" } else { Remove-Item -LiteralPath $lnk -Force }
  }
}
# 4. The shader cache (compiled shaders only; rebuilt on the next start if BG3RTX is reinstalled).
if (-not $KeepShaderCache -and (Test-Path -LiteralPath $ShaderCache)) {
  Remove-Tree $ShaderCache
  Remove-EmptyDir (Split-Path $ShaderCache)
}
# 5. Empty folders, deepest first, then the marker and the install folder itself if nothing else is left.
foreach ($f in $listed | Sort-Object { $_.Length } -Descending) {
  $p = Inside $f
  if (-not $p) { continue }
  for ($d = Split-Path $p; $d -and $d.Length -gt $dir.Length; $d = Split-Path $d) { Remove-EmptyDir $d }
  for ($d = Split-Path (Join-Path $backup $f); $d -and $d.Length -gt $dir.Length; $d = Split-Path $d) { Remove-EmptyDir $d }
}
if ($WhatIf) { Write-Host "WhatIf: $removed files would be removed; nothing was deleted."; return }
Remove-OneFile $manifest
Remove-OneFile (Join-Path $dir '.bg3rtx_install')
# Retain backups through all uninstall operations. A failed earlier operation must
# not turn restored originals into files deleted without a backup on the next try.
foreach ($f in $listed) { Remove-OneFile (Join-Path $backup $f) }
foreach ($f in $listed | Sort-Object { $_.Length } -Descending) {
  for ($d = Split-Path (Join-Path $backup $f); $d -and $d.Length -gt $dir.Length; $d = Split-Path $d) { Remove-EmptyDir $d }
}
$left = Get-ChildItem -LiteralPath $dir -Force | Where-Object { $_.Name -ne ".bg3rtx_install" }
if (-not $left) {
  Remove-EmptyDir $dir
  Write-Host "BG3RTX removed ($removed files)."
} else {
  Write-Host "BG3RTX removed ($removed files). Left in ${dir}: $(($left | ForEach-Object Name) -join ', ') (not ours, or kept)."
}
Write-Host "If you set a Steam launch option for BG3RTX, clear it: game Properties > General > Launch options."
