# Starts BG3's Vulkan renderer with the BG3RTX layer, without touching the
# registry or the game folder: the layer is found through VK_ADD_LAYER_PATH and
# enabled for this process only.
#   .\scripts\launch-bg3.ps1                  layer on, other overlays as usual
#   .\scripts\launch-bg3.ps1 -Clean           also disable the Steam overlay and Fossilize layers
#   .\scripts\launch-bg3.ps1 -Exe <path>      another Vulkan program (e.g. vkcube) for testing
param(
  [string]$Game = "D:\SteamLibrary\steamapps\common\Baldurs Gate 3\bin",
  [string]$Exe,
  [switch]$Clean,
  [switch]$Validation
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$bin = Join-Path $root "bin"
if (-not (Test-Path (Join-Path $bin "VkLayer_bg3rtx.dll"))) { throw "Build first: .\scripts\build.ps1" }

$env:VK_ADD_LAYER_PATH = $bin
$layers = @("VK_LAYER_BG3RTX_host")
if ($Validation) { $layers = @("VK_LAYER_KHRONOS_validation") + $layers }
$env:VK_INSTANCE_LAYERS = ($layers -join ";")
$env:VK_LOADER_LAYERS_ENABLE = ($layers -join ",")
if ($Clean) {
  $env:DISABLE_VK_LAYER_VALVE_steam_overlay_1 = "1"
  $env:DISABLE_VK_LAYER_VALVE_steam_fossilize_1 = "1"
}

if (-not $Exe) {
  $Exe = Join-Path $Game "bg3.exe"
  # Lets steam_api initialise without going back through the Larian launcher.
  $env:SteamAppId = "1086940"
  $env:SteamGameId = "1086940"
}
$wd = Split-Path $Exe
Write-Host "Launching $Exe with $($env:VK_INSTANCE_LAYERS)"
Start-Process -FilePath $Exe -WorkingDirectory $wd
