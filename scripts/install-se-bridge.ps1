# Installs the BG3RTX Script Extender bridge (mods/BG3RTX_Bridge, docs/SE_BRIDGE.md)
# as loose files, without a .pak and without touching modsettings.lsx.
#
# The Script Extender loads Mods/<Folder>/ScriptExtender/Config.json for every module
# in the load order, through the game's file system, which also reads loose files from
# <game>\Data. The bridge therefore rides on the base module (GustavX, always loaded):
#   <game>\Data\Mods\GustavX\ScriptExtender\Config.json
#   <game>\Data\Mods\GustavX\ScriptExtender\Lua\Bootstrap{Client,Server}.lua
# Its ModTable is "BG3RTX_Bridge". A marker file makes -Uninstall remove only our files.
#
#   .\scripts\install-se-bridge.ps1              install or update
#   .\scripts\install-se-bridge.ps1 -Uninstall   remove
#   .\scripts\install-se-bridge.ps1 -Status      show what is installed and the bridge's output
param(
  [string]$Game = "D:\SteamLibrary\steamapps\common\Baldurs Gate 3",
  [string]$HostModule = "GustavX",
  [switch]$Uninstall,
  [switch]$Status
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$src = Join-Path $root "mods\BG3RTX_Bridge\ScriptExtender"
$dst = Join-Path $Game "Data\Mods\$HostModule\ScriptExtender"
$marker = Join-Path $dst "BG3RTX_Bridge.installed"
$out = Join-Path $env:LOCALAPPDATA "Larian Studios\Baldur's Gate 3\Script Extender\bg3rtx"

if (-not (Test-Path (Join-Path $Game "bin\bg3.exe"))) { throw "No bg3.exe under $Game\bin" }
if (-not (Test-Path (Join-Path $Game "bin\DWrite.dll"))) { Write-Warning "bin\DWrite.dll (the Script Extender) is missing: the bridge will not run" }

if ($Status) {
  Write-Host "installed: $(Test-Path $marker) ($dst)"
  if (Test-Path $out) { Get-ChildItem $out | Format-Table Name, Length, LastWriteTime -AutoSize }
  return
}

if ((Test-Path $dst) -and -not (Test-Path $marker)) {
  throw "$dst exists and is not ours; refusing to touch it"
}
if ($Uninstall) {
  if (Test-Path $marker) {
    Remove-Item -Recurse -Force $dst
    Write-Host "removed $dst"
  } else { Write-Host "not installed" }
  return
}

New-Item -ItemType Directory -Force $dst | Out-Null
Copy-Item -Recurse -Force (Join-Path $src "*") $dst
Set-Content $marker "BG3RTX Script Extender bridge, installed $(Get-Date -Format s) from $src"
Write-Host "installed the bridge into $dst"
Write-Host "output: $out\lights.txt (read by the layer, se_bridge=1)"
