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
param([string]$InstallDir = $PSScriptRoot, [switch]$KeepSettings, [switch]$KeepShaderCache, [switch]$WhatIf,
      [string]$ShaderCache = (Join-Path $env:LOCALAPPDATA "BG3RTX\shader_cache"))
$ErrorActionPreference = "Stop"
$dir = [IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
if (-not (Test-Path (Join-Path $dir ".bg3rtx_install"))) { throw "$dir is not a BG3RTX install (no .bg3rtx_install)" }
if (Get-Process -Name bg3, bg3_dx11 -ErrorAction SilentlyContinue) { throw "Baldur's Gate 3 is running: close it first" }
if (Get-Process -Name BG3RTX -ErrorAction SilentlyContinue) { throw "BG3RTX.exe is open: close the settings window first" }

$removed = 0
function Test-Safe([string]$p) {
  # Refuse anything in Larian's profile folder (saves) or in a game folder.
  if ($p -match '\\Larian Studios(\\|$)') { return $false }
  for ($q = Split-Path $p; $q; $q = Split-Path $q) { if (Test-Path (Join-Path $q "bin\bg3.exe")) { return $false } }
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
  $item = Get-Item -LiteralPath $d -Force -ErrorAction SilentlyContinue
  if (-not $item -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { return }
  foreach ($c in Get-ChildItem -LiteralPath $d -Force) {
    if ($c.Attributes -band [IO.FileAttributes]::ReparsePoint) { Write-Warning "skipped link: $($c.FullName)"; continue }
    if ($c.PSIsContainer) { Remove-Tree $c.FullName } else { Remove-OneFile $c.FullName }
  }
  Remove-EmptyDir $d
}
function Remove-EmptyDir([string]$d) {
  if ((Test-Path -LiteralPath $d -PathType Container) -and -not (Get-ChildItem -LiteralPath $d -Force | Select-Object -First 1)) {
    if ($WhatIf) { Write-Host "would remove folder $d" } else { Remove-Item -LiteralPath $d -Force }
  }
}
function Inside([string]$rel) {
  if (-not $rel -or $rel.Contains("..") -or [IO.Path]::IsPathRooted($rel)) { return $null }
  return Join-Path $dir $rel
}

# 1. What the installer copied.
$manifest = Join-Path $dir "install_manifest.txt"
$listed = if (Test-Path $manifest) { @(Get-Content $manifest) } else { @() }
foreach ($f in $listed) { $p = Inside $f; if ($p) { Remove-OneFile $p } }
# 2. What BG3RTX writes at run time (layer/util.cpp, layer/hotreload.cpp, layer/streamline.cpp, the launcher).
$written = @("bg3rtx_launcher.ini", "bg3rtx_user.ini.tmp", "trace.request")
if (-not $KeepSettings) { $written += "bg3rtx_user.ini" }
foreach ($f in $written) { Remove-OneFile (Join-Path $dir $f) }
Get-ChildItem -LiteralPath $dir -Force -File -Filter "bg3rtx*.log" | ForEach-Object { Remove-OneFile $_.FullName }
foreach ($d in @("bin\hot", "sl_logs", "captures")) { Remove-Tree (Join-Path $dir $d) }
Remove-OneFile $manifest
# 3. The Start menu shortcut, if it points here.
$lnk = Join-Path ([Environment]::GetFolderPath("Programs")) "BG3RTX.lnk"
if (Test-Path $lnk) {
  $target = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk).TargetPath
  if ($target -and $target.StartsWith($dir + "\", [StringComparison]::OrdinalIgnoreCase)) {
    if ($WhatIf) { Write-Host "would remove $lnk" } else { Remove-Item -Force $lnk }
  }
}
# 4. The shader cache (compiled shaders only; rebuilt on the next start if BG3RTX is reinstalled).
if (-not $KeepShaderCache -and (Test-Path $ShaderCache)) {
  Remove-Tree $ShaderCache
  Remove-EmptyDir (Split-Path $ShaderCache)
}
# 5. Empty folders, deepest first, then the marker and the install folder itself if nothing else is left.
foreach ($f in $listed | Sort-Object { $_.Length } -Descending) {
  $p = Inside $f
  if (-not $p) { continue }
  for ($d = Split-Path $p; $d -and $d.Length -gt $dir.Length; $d = Split-Path $d) { Remove-EmptyDir $d }
}
if ($WhatIf) { Write-Host "WhatIf: $removed files would be removed; nothing was deleted."; return }
$left = Get-ChildItem -LiteralPath $dir -Force | Where-Object { $_.Name -ne ".bg3rtx_install" }
if (-not $left) {
  Remove-OneFile (Join-Path $dir ".bg3rtx_install")
  Remove-EmptyDir $dir
  Write-Host "BG3RTX removed ($removed files)."
} else {
  Write-Host "BG3RTX removed ($removed files). Left in ${dir}: $(($left | ForEach-Object Name) -join ', ') (not ours, or kept)."
}
Write-Host "If you set a Steam launch option for BG3RTX, clear it: game Properties > General > Launch options."
