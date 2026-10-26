# Assembles a player package in dist\BG3RTX with the release profile:
# bg3rtx.ini = the repository's bg3rtx.ini + data\release.ini (developer keys
# off: dev=0, dump_shaders=0, debug views and stats off). The repository's own
# bg3rtx.ini and bin\ are not changed.
#   .\scripts\package.ps1             build, then package
#   .\scripts\package.ps1 -NoBuild
param([switch]$NoBuild, [string]$Out = "")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
if (-not $Out) { $Out = Join-Path $root "dist\BG3RTX" }
if (-not $NoBuild) { & (Join-Path $PSScriptRoot "build.ps1") -Test }

$bin = Join-Path $root "bin"
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force (Join-Path $Out "bin") | Out-Null

# The layer, its manifest, the core, the launcher and Streamline (production DLLs).
foreach ($f in @("VkLayer_bg3rtx.dll", "VkLayer_bg3rtx.json", "bg3rtx_core.dll", "BG3RTX.exe")) {
  Copy-Item (Join-Path $bin $f) (Join-Path $Out "bin\$f")
}
Copy-Item -Recurse (Join-Path $bin "sl") (Join-Path $Out "bin\sl")
# The layer compiles its HLSL at run time (layer/shaders.cpp): DXC beside it.
$dxc = @((Join-Path $bin "dxcompiler.dll"), (Join-Path $env:VULKAN_SDK "Bin\dxcompiler.dll")) |
       Where-Object { Test-Path $_ } | Select-Object -First 1
if ($dxc) { Copy-Item $dxc (Join-Path $Out "bin\dxcompiler.dll") } else { Write-Warning "dxcompiler.dll not found" }
Copy-Item -Recurse (Join-Path $root "shaders") (Join-Path $Out "shaders")

# Release profile: later lines win in the ini parser.
$ini = @(Get-Content (Join-Path $root "bg3rtx.ini")) + @(Get-Content (Join-Path $root "data\release.ini"))
$ini | Set-Content -Encoding UTF8 (Join-Path $Out "bg3rtx.ini")
New-Item -ItemType Directory -Force (Join-Path $Out "docs") | Out-Null
Copy-Item (Join-Path $root "docs\USER_SETTINGS.md") (Join-Path $Out "docs\USER_SETTINGS.md")
Write-Host "Packaged $Out (start bin\BG3RTX.exe)"
