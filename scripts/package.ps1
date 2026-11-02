# Builds the player package: dist\BG3RTX-<version>\ and dist\BG3RTX-<version>.zip (+ -symbols.zip
# with the PDBs, for crash reports; never shipped to players). docs/release/RELEASE_BETA.md.
#
# Contents (an explicit list, nothing else):
#   bin\VkLayer_bg3rtx.dll    the lean layer (CMake BG3RTX_DIAGNOSTICS=OFF, build-nodiag\)
#   bin\VkLayer_bg3rtx.json   its manifest, found through VK_ADD_LAYER_PATH (tools/launcher/launch.cpp)
#   bin\bg3rtx_core.dll, bin\BG3RTX.exe (settings window + launcher), bin\dxcompiler.dll (runtime HLSL)
#   bin\sl\                   Streamline + DLSS / DLSS-G / Reflex production DLLs (signed by NVIDIA)
#   shaders\                  HLSL, compiled in game by DXC
#   common\*.h                only the headers the shaders include (found by following #include)
#   data\assetdb\bg3rtx_assetdb.bin   offline game data (lights, materials, roofs)
#   bg3rtx.ini                repository bg3rtx.ini + data\release.ini (developer keys off)
#   README.md, THIRD_PARTY.md, licences\, docs\USER_SETTINGS.md, VERSION.txt, files.txt
#   install.ps1, uninstall.ps1, Install.cmd, Uninstall.cmd   (scripts\release\)
# Not shipped: SHaRC headers (licence, docs/release/THIRD_PARTY.md; -WithSharc adds them; sharc=0 in
# every preset), Aftermath, the OMM SDK, development Streamline DLLs, tools, tests, PDBs.
#
#   .\scripts\package.ps1                  build (tests included) at BelowNormal, then package
#   .\scripts\package.ps1 -NoBuild         package what bin\ and build-nodiag\bin hold
#   .\scripts\package.ps1 -Diagnostics     ship the developer layer of bin\ instead
#   .\scripts\package.ps1 -Version 0.1.0-beta1 -NoZip
#   .\scripts\package.ps1 -ListOnly        validate/list notice files only; no build or output writes
param([switch]$ListOnly, [switch]$NoBuild, [switch]$Diagnostics, [switch]$WithSharc, [switch]$NoZip,
      [string]$Version = "", [string]$OutDir = "")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot

# Validate before builds or package writes. THIRD_PARTY.md owns the required file list.
$noticeDoc = Join-Path $root "docs\release\THIRD_PARTY.md"
$noticeDir = Join-Path $root "docs\release\licences"
if (-not (Test-Path -LiteralPath $noticeDoc -PathType Leaf)) { throw "missing required notice inventory: $noticeDoc" }
$inventory = Get-Content -LiteralPath $noticeDoc -Raw
$requiredSection = [regex]::Match($inventory, '(?ms)^## Required notice files\r?\n(.*?)(?=^## |\z)')
$required = [regex]::Matches($requiredSection.Groups[1].Value, '(?m)^- `([^`]+)`\r?$')
if (-not $requiredSection.Success -or $required.Count -eq 0) { throw "no required notice files listed in $noticeDoc" }
$missing = @()
foreach ($entry in $required) {
  $file = $entry.Groups[1].Value
  if ($file -notmatch '^[A-Za-z0-9_.-]+$') { throw "invalid required notice filename: $file" }
  if (-not (Test-Path -LiteralPath (Join-Path $noticeDir $file) -PathType Leaf)) { $missing += $file }
}
if ($missing.Count) { throw "missing required notices listed in THIRD_PARTY.md: $($missing -join ', ') (expected in $noticeDir)" }
$noticeFiles = @([pscustomobject]@{ Source = $noticeDoc; Destination = "THIRD_PARTY.md" })
$noticeFiles += @(Get-ChildItem -LiteralPath $noticeDir -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
  [pscustomobject]@{ Source = $_.FullName; Destination = "licences\" + $_.FullName.Substring($noticeDir.Length + 1) }
})
if ($ListOnly) {
  # This mode lists only the notice payload, independent of binaries, SDKs and build products.
  $noticeFiles | ConvertTo-Json
  return
}
. (Join-Path $PSScriptRoot "low-priority.ps1")
if (-not $OutDir) { $OutDir = Join-Path $root "dist" }

# A worktree has no external\ or data\assetdb of its own: the main checkout's (as CMake does).
function Find-Shared([string]$rel) {
  foreach ($c in @((Join-Path $root $rel), (Join-Path $root "..\..\..\$rel"))) {
    if (Test-Path $c) { return (Resolve-Path $c).Path }
  }
  return $null
}

if (-not $Version) {
  $sha = (git -C $root rev-parse --short HEAD).Trim()
  $dirty = if (git -C $root status --porcelain --untracked-files=no) { "-dirty" } else { "" }
  $Version = "beta-{0}-{1}{2}" -f (Get-Date -Format "yyyyMMdd"), $sha, $dirty
}
if ($Version -notmatch '^[A-Za-z0-9._-]+$') { throw "version '$Version': letters, digits, . _ - only" }
$name = "BG3RTX-$Version"
$out = Join-Path $OutDir $name

if (-not $NoBuild) {
  # AGENTS.md: never build while the game runs (contention, and a live core would hot-reload).
  if (Get-Process -Name bg3, bg3_dx11 -ErrorAction SilentlyContinue) { throw "bg3.exe is running: package later or use -NoBuild" }
  & (Join-Path $PSScriptRoot "build.ps1") -Test
}

$bin = Join-Path $root "bin"
$layerBin = $bin
if (-not $Diagnostics) {
  $nodiag = Join-Path $root "build-nodiag"
  $layerBin = Join-Path $nodiag "bin"
  if (-not $NoBuild) {
    # build.ps1 entered the MSVC environment; same generator and configuration.
    if (-not (Test-Path (Join-Path $nodiag "build.ninja"))) {
      cmake -S $root -B $nodiag -G Ninja "-DCMAKE_BUILD_TYPE=RelWithDebInfo" "-DBG3RTX_DIAGNOSTICS=OFF" "-DBG3RTX_BIN=$layerBin"
      if ($LASTEXITCODE) { throw "configure of the lean layer failed" }
    }
    cmake --build $nodiag --target VkLayer_bg3rtx
    if ($LASTEXITCODE) { throw "build of the lean layer failed" }
  }
  if (-not (Test-Path (Join-Path $layerBin "VkLayer_bg3rtx.dll"))) { throw "no lean layer in $layerBin (run without -NoBuild)" }
}

if (Test-Path $out) {
  # Only a previous package of ours (it has files.txt), never anything else under dist\.
  if (-not (Test-Path (Join-Path $out "files.txt"))) { throw "$out exists and is not a package: remove it by hand" }
  Remove-Item -Recurse -Force $out
}
New-Item -ItemType Directory -Force (Join-Path $out "bin") | Out-Null
function Copy-To([string]$src, [string]$rel) {
  if (-not (Test-Path $src)) { throw "missing: $src" }
  $dst = Join-Path $out $rel
  New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
  Copy-Item $src $dst
}

# Binaries.
Copy-To (Join-Path $layerBin "VkLayer_bg3rtx.dll") "bin\VkLayer_bg3rtx.dll"
foreach ($f in @("VkLayer_bg3rtx.json", "bg3rtx_core.dll", "BG3RTX.exe")) { Copy-To (Join-Path $bin $f) "bin\$f" }
foreach ($f in @("sl.interposer.dll", "sl.common.dll", "sl.dlss_g.dll", "sl.reflex.dll", "sl.pcl.dll",
                 "nvngx_dlssg.dll", "NvLowLatencyVk.dll", "nvngx_dlss.dll", "nvngx_dlssd.dll")) {
  Copy-To (Join-Path $bin "sl\$f") "bin\sl\$f"
}
# The layer compiles its HLSL at run time (layer/shaders.cpp): DXC beside it.
$dxc = @((Join-Path $bin "dxcompiler.dll"), (Join-Path "$env:VULKAN_SDK" "Bin\dxcompiler.dll")) |
       Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $dxc) { throw "dxcompiler.dll not found (bin\ or VULKAN_SDK\Bin)" }
Copy-To $dxc "bin\dxcompiler.dll"

# Shaders, and the common\ headers they include (transitively; -I shaders as in layer/shaders.cpp).
Copy-Item -Recurse (Join-Path $root "shaders") (Join-Path $out "shaders")
$seen = @{}
$queue = New-Object System.Collections.Generic.Queue[string]
Get-ChildItem (Join-Path $root "shaders") -Recurse -File -Include *.hlsl, *.hlsli, *.h | ForEach-Object { $queue.Enqueue($_.FullName) }
$commonDir = (Resolve-Path (Join-Path $root "common")).Path
while ($queue.Count) {
  $f = $queue.Dequeue()
  if ($seen.ContainsKey($f)) { continue }
  $seen[$f] = $true
  foreach ($m in [regex]::Matches((Get-Content -Raw $f), '(?m)^\s*#\s*include\s*[<"]([^>"]+)[>"]')) {
    $inc = $m.Groups[1].Value
    foreach ($base in @((Split-Path $f), (Join-Path $root "shaders"))) {
      $p = [IO.Path]::GetFullPath((Join-Path $base $inc))
      if (Test-Path $p -PathType Leaf) { $queue.Enqueue($p); break }
    }
  }
}
$headers = $seen.Keys | Where-Object { $_.StartsWith($commonDir + "\", [StringComparison]::OrdinalIgnoreCase) } | Sort-Object
foreach ($h in $headers) { Copy-To $h ("common\" + $h.Substring($commonDir.Length + 1)) }

# Asset database (offline game data; docs/ASSET_DB.md).
$db = Find-Shared "data\assetdb\bg3rtx_assetdb.bin"
if (-not $db) { throw "data\assetdb\bg3rtx_assetdb.bin not found: python -m tools.assets build --out data/assetdb" }
Copy-To $db "data\assetdb\bg3rtx_assetdb.bin"

# Release profile: later lines win in the ini parser.
$ini = @(Get-Content (Join-Path $root "bg3rtx.ini")) + @(Get-Content (Join-Path $root "data\release.ini"))
$ini | Set-Content -Encoding UTF8 (Join-Path $out "bg3rtx.ini")

# Every checked-in notice, including any additional files/subdirectories.
foreach ($notice in $noticeFiles) { Copy-To $notice.Source $notice.Destination }
if ($WithSharc) {
  $ext = Find-Shared "external"
  if (-not $ext) { throw "external\ (NVIDIA SDKs) not found" }
  Copy-To (Join-Path $ext "sharc-1.8.3\License.md") "LICENSES\SHaRC-NVIDIA-RTX-SDKs.md"
}
if ($WithSharc) {
  $sharc = Join-Path $ext "sharc-1.8.3\include"
  Get-ChildItem $sharc -File | ForEach-Object { Copy-To $_.FullName ("external\sharc-1.8.3\include\" + $_.Name) }
}

# Player documents and the install scripts.
Copy-To (Join-Path $root "docs\release\README.md") "README.md"
Copy-To (Join-Path $root "docs\USER_SETTINGS.md") "docs\USER_SETTINGS.md"
foreach ($f in @("install.ps1", "uninstall.ps1", "Install.cmd", "Uninstall.cmd")) {
  Copy-To (Join-Path $PSScriptRoot "release\$f") $f
}
$sha = (git -C $root rev-parse HEAD).Trim()
@("BG3RTX $Version", "commit $sha", "layer $(if ($Diagnostics) { 'diagnostics' } else { 'lean' })",
  "built $(Get-Date -Format 'yyyy-MM-dd HH:mm')") | Set-Content -Encoding UTF8 (Join-Path $out "VERSION.txt")

# files.txt: every shipped file (install.ps1 copies exactly these; uninstall.ps1 removes them).
$files = Get-ChildItem $out -Recurse -File | ForEach-Object { $_.FullName.Substring($out.Length + 1) } | Sort-Object
@($files) + @("files.txt") | Set-Content -Encoding UTF8 (Join-Path $out "files.txt")
$mb = (Get-ChildItem $out -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("Packaged {0}: {1} files, {2:N1} MB, {3} common headers, layer {4}" -f $out, ($files.Count + 1), $mb,
            @($headers).Count, $(if ($Diagnostics) { 'diagnostics' } else { 'lean' }))

if (-not $NoZip) {
  $zip = Join-Path $OutDir "$name.zip"
  if (Test-Path $zip) { Remove-Item -Force $zip }
  Compress-Archive -Path $out -DestinationPath $zip -CompressionLevel Optimal
  # Symbols for crash reports (kept by the developer, not uploaded with the mod).
  $pdbs = @((Join-Path $layerBin "VkLayer_bg3rtx.pdb"), (Join-Path $bin "bg3rtx_core_RelWithDebInfo.pdb"),
            (Join-Path $bin "bg3rtx_core.pdb"), (Join-Path $bin "BG3RTX.pdb")) | Where-Object { Test-Path $_ }
  $sym = Join-Path $OutDir "$name-symbols.zip"
  if (Test-Path $sym) { Remove-Item -Force $sym }
  if ($pdbs) { Compress-Archive -Path $pdbs -DestinationPath $sym -CompressionLevel Optimal }
  Write-Host ("Zip {0} ({1:N1} MB); symbols {2}" -f $zip, ((Get-Item $zip).Length / 1MB), $(if ($pdbs) { $sym } else { "none" }))
}
