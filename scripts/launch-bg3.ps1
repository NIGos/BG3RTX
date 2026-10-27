# Starts BG3's Vulkan renderer with the BG3RTX layer, without touching the
# registry or the game folder: the layer is found through VK_ADD_LAYER_PATH and
# enabled for this process only.
#   .\scripts\launch-bg3.ps1                  layer on, other overlays as usual
#   .\scripts\launch-bg3.ps1 -Clean           also disable third-party overlay layers (Steam, Fossilize, EOS, GOG)
#   .\scripts\launch-bg3.ps1 -GameArgs -continueGame   load the most recent save directly
#   .\scripts\launch-bg3.ps1 -Exe <path>      another Vulkan program (e.g. vkcube) for testing
param(
  [string]$Game = "D:\SteamLibrary\steamapps\common\Baldurs Gate 3\bin",
  [string]$Exe,
  [string[]]$GameArgs = @(),
  [switch]$Clean,
  [switch]$Validation
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$bin = Join-Path $root "bin"
if (-not (Test-Path (Join-Path $bin "VkLayer_bg3rtx.dll"))) { throw "Build first: .\scripts\build.ps1" }

$env:VK_ADD_LAYER_PATH = $bin
$layers = @("VK_LAYER_BG3RTX_host")
# Validation goes below our layer (later in the list), so it also checks what the layer records.
if ($Validation) { $layers = $layers + @("VK_LAYER_KHRONOS_validation") }
$env:VK_INSTANCE_LAYERS = ($layers -join ";")
$env:VK_LOADER_LAYERS_ENABLE = ($layers -join ",")
if ($Clean) {
  $env:DISABLE_VK_LAYER_VALVE_steam_overlay_1 = "1"
  $env:DISABLE_VK_LAYER_VALVE_steam_fossilize_1 = "1"
  $env:EOS_OVERLAY_DISABLE_VULKAN_WIN64 = "1"
  $env:DISABLE_GALAXY_OVERLAY = "1"
  # NVIDIA's present layer is Smooth Motion: it interleaves driver-generated
  # presents with the game's. Off for analysis, and it must be off for DLSS-G.
  $env:DISABLE_LAYER_NV_PRESENT_1 = "1"
}

if (-not $Exe) {
  $Exe = Join-Path $Game "bg3.exe"
  # Lets steam_api initialise without going back through the Larian launcher.
  $env:SteamAppId = "1086940"
  $env:SteamGameId = "1086940"
}
$wd = Split-Path $Exe
Write-Host "Launching $Exe $GameArgs with $($env:VK_INSTANCE_LAYERS)"
# Returns the started process: auto-run.ps1 tracks only its own game, by this PID
# (with SteamAppId set the game does not relaunch itself through Steam).
if ($GameArgs.Count) { Start-Process -FilePath $Exe -WorkingDirectory $wd -ArgumentList $GameArgs -PassThru }
else { Start-Process -FilePath $Exe -WorkingDirectory $wd -PassThru }
