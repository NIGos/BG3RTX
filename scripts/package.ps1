# Builds the player package: dist\BG3RTX-<version>\ and dist\BG3RTX-<version>.zip (+ -symbols.zip
# with the PDBs, for crash reports; never shipped to players). docs/release/RELEASE_BETA.md.
#
# Contents (an explicit list, nothing else):
#   bin\VkLayer_bg3rtx.dll    the lean layer (CMake BG3RTX_DIAGNOSTICS=OFF, build-nodiag\)
#   bin\VkLayer_bg3rtx.json   its manifest, found through VK_ADD_LAYER_PATH (tools/launcher/launch.cpp)
#   bin\bg3rtx_core.dll, bin\BG3RTX.exe (settings window + launcher), bin\dxcompiler.dll (runtime HLSL)
#   bin\sl\                   Streamline + DLSS / DLSS-G / Reflex production DLLs (signed by NVIDIA)
#   shaders\                  HLSL and runtime alpha / camera-fade classification tables
#   common\*.h                only the headers the shaders include (found by following #include)
#   data\assetdb\bg3rtx_assetdb.bin   offline game data (lights, materials, roofs)
#   bg3rtx.ini                repository bg3rtx.ini + data\release.ini (developer keys off)
#   README.md, LICENSE, LICENSE-EXCEPTION.md, THIRD_PARTY.md, licences\, docs\USER_SETTINGS.md, VERSION.txt, files.txt
#   install.ps1, uninstall.ps1, Install.cmd, Uninstall.cmd   (scripts\release\)
# Not shipped: SHaRC headers (licence, docs/release/THIRD_PARTY.md; -WithSharc adds them; sharc=0 in
# every preset), Aftermath, the OMM SDK, development Streamline DLLs, tools, tests, PDBs.
#
#   .\scripts\package.ps1                  build (tests included) at BelowNormal, then package
#   .\scripts\package.ps1 -NoBuild         package what bin\ and build-nodiag\bin hold
#   .\scripts\package.ps1 -Diagnostics     ship the developer layer of bin\ instead
#   .\scripts\package.ps1 -Version 0.1.0-beta1 -NoZip
#   .\scripts\package.ps1 -ListOnly        validate/list notice files only; no build or output writes
#   .\scripts\package.ps1 -DryRun          validate/list notices and launcher UI assets; no writes
#   .\scripts\package.ps1 -DryRun -OutDir <scratch>  full payload audit, files.txt and manifest diff; no build/copies
param([switch]$ListOnly, [switch]$DryRun, [switch]$NoBuild, [switch]$Diagnostics, [switch]$WithSharc, [switch]$NoZip,
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
# One manifest owns every copied third-party binary and its notice dependencies.
$thirdParty = Get-Content -LiteralPath (Join-Path $PSScriptRoot "package-third-party.json") -Raw | ConvertFrom-Json
$destinations = @{}
foreach ($entry in @($thirdParty.binaries) + @($thirdParty.compiled_components)) {
  if (@($entry.licences).Count -eq 0) { throw "third-party component has no licences" }
  foreach ($file in $entry.licences) {
    if ($file -notin @($required | ForEach-Object { $_.Groups[1].Value })) { throw "unlisted third-party licence: $file" }
  }
}
foreach ($entry in $thirdParty.binaries) {
  if ($entry.destination -notmatch '^bin/(sl/)?[A-Za-z0-9_.-]+\.dll$' -or $destinations.ContainsKey($entry.destination)) {
    throw "invalid or duplicate third-party destination: $($entry.destination)"
  }
  $destinations[$entry.destination] = $true
}
$missing = @()
foreach ($entry in $required) {
  $file = $entry.Groups[1].Value
  if ($file -notmatch '^[A-Za-z0-9_.-]+$') { throw "invalid required notice filename: $file" }
  if (-not (Test-Path -LiteralPath (Join-Path $noticeDir $file) -PathType Leaf)) { $missing += $file }
}
if ($missing.Count) { throw "missing required notices listed in THIRD_PARTY.md: $($missing -join ', ') (expected in $noticeDir)" }
$noticeFiles = @([pscustomobject]@{ Source = $noticeDoc; Destination = "THIRD_PARTY.md" })
foreach ($file in @('LICENSE', 'LICENSE-EXCEPTION.md')) {
  $source = Join-Path $root $file
  if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "missing project licence: $file" }
  $noticeFiles += [pscustomobject]@{ Source = $source; Destination = $file }
}
$noticeFiles += @(Get-ChildItem -LiteralPath $noticeDir -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
  [pscustomobject]@{ Source = $_.FullName; Destination = "licences\" + $_.FullName.Substring($noticeDir.Length + 1) }
})
if ($ListOnly) {
  # This mode lists only the notice payload, independent of binaries, SDKs and build products.
  $noticeFiles | ConvertTo-Json
  return
}

# The same explicit static payload is used in portable and Inno packages. Runtime
# discovery falls back to native controls; never fetch or bundle a browser runtime.
$launcherFiles = @()
foreach ($f in @("index.html", "app.css", "app.js")) {
  $launcherFiles += [pscustomobject]@{ Source = (Join-Path $root "tools\launcher\ui\$f"); Destination = "bin\ui\$f" }
}
foreach ($f in @("LatoWeb-Regular.ttf", "LatoWeb-Semibold.ttf", "OFL.txt", "README.md", "manifest.json")) {
  $launcherFiles += [pscustomobject]@{ Source = (Join-Path $root "assets\fonts\$f"); Destination = "bin\ui\fonts\$f" }
}
foreach ($f in @("bg3rtx.svg", "bg3rtx-small.svg", "bg3rtx-small-contrast.svg", "bg3rtx.ico")) {
  $launcherFiles += [pscustomobject]@{ Source = (Join-Path $root "assets\branding\$f"); Destination = "bin\ui\branding\$f" }
}
foreach ($f in @("README.md", "corner.svg", "dark-stone.svg", "divider.svg", "frame.svg", "glyph-help.svg", "glyph-light.svg", "glyph-memory.svg", "glyph-performance.svg", "glyph-water.svg", "hero-lantern.png", "parchment-panel.svg")) {
  $launcherFiles += [pscustomobject]@{ Source = (Join-Path $root "assets\branding\ornaments\$f"); Destination = "bin\ui\branding\ornaments\$f" }
}
foreach ($entry in $launcherFiles) {
  if (-not (Test-Path -LiteralPath $entry.Source -PathType Leaf)) { throw "missing launcher asset: $($entry.Source)" }
  if ((Get-Item -LiteralPath $entry.Source).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "linked launcher asset: $($entry.Source)" }
}
if ($DryRun -and -not $OutDir) {
  @($noticeFiles) + @($launcherFiles) | ConvertTo-Json
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

if (-not $NoBuild -and -not $DryRun) {
  # AGENTS.md: never build while the game runs (contention, and a live core would hot-reload).
  if (Get-Process -Name bg3, bg3_dx11 -ErrorAction SilentlyContinue) { throw "bg3.exe is running: package later or use -NoBuild" }
  & (Join-Path $PSScriptRoot "build.ps1") -Test
}

$bin = Join-Path $root "bin"
$layerBin = $bin
if (-not $Diagnostics) {
  $nodiag = Join-Path $root "build-nodiag"
  $layerBin = Join-Path $nodiag "bin"
  if (-not $NoBuild -and -not $DryRun) {
    & (Join-Path $PSScriptRoot "build.ps1") -ReleaseLayer
  }
  if (-not $DryRun -and -not (Test-Path (Join-Path $layerBin "VkLayer_bg3rtx.dll"))) { throw "no lean layer in $layerBin (run without -NoBuild)" }
}

# Never replace/recursively clean a previous package: it could contain a junction.
$out = [IO.Path]::GetFullPath($out)
. (Join-Path $PSScriptRoot "linked-path.ps1")
$linked = Find-LinkedAncestor $out
if ($linked) { throw "linked package output: $linked" }
if (Test-Path -LiteralPath $out) { throw "$out already exists: choose a fresh output/version" }
New-Item -ItemType Directory -Path $out | Out-Null
$payload = New-Object System.Collections.Generic.List[object]
function Copy-To([string]$src, [string]$rel) {
  # Launcher-staged, user-supplied NR is never a redistribution payload, even if
  # a future manifest or developer folder happens to name it.
  if ([IO.Path]::GetFileName($rel) -ieq 'nvngx_dlssnr.dll') { throw 'DLSS 5 NR runtime is user-supplied and must never be packaged' }
  $exists = Test-Path -LiteralPath $src -PathType Leaf
  $payload.Add([pscustomobject]@{ Source = $src; Destination = $rel; Exists = $exists })
  if ($DryRun) { return }
  if (-not $exists) { throw "missing: $src" }
  $dst = Join-Path $out $rel
  New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
  Copy-Item -LiteralPath $src -Destination $dst
}

# Binaries.
Copy-To (Join-Path $layerBin "VkLayer_bg3rtx.dll") "bin\VkLayer_bg3rtx.dll"
Copy-To (Join-Path $layerBin "VkLayer_bg3rtx.json") "bin\VkLayer_bg3rtx.json"
# bg3rtx_hairconv.exe: the native strand groom converter the launcher runs (no Python, LSLib, granny2 or PowerShell 7).
foreach ($f in @("bg3rtx_core.dll", "BG3RTX.exe", "bg3rtx_hairconv.exe")) { Copy-To (Join-Path $bin $f) "bin\$f" }
foreach ($entry in $launcherFiles) { Copy-To $entry.Source $entry.Destination }
# Only the manifest can add a third-party binary to the payload.
foreach ($entry in $thirdParty.binaries) {
  $source = Join-Path $root $entry.destination
  # -ReleaseLayer copies production Streamline beside the lean layer, including
  # on a fresh worktree where the ordinary layer target has never been built.
  if (-not $Diagnostics -and $entry.destination.StartsWith('bin/sl/')) {
    $source = Join-Path $layerBin $entry.destination.Substring(4)
  }
  if ($entry.destination -eq "bin/dxcompiler.dll" -and -not (Test-Path $source)) {
    $source = Join-Path "$env:VULKAN_SDK" "Bin\dxcompiler.dll"
  }
  Copy-To $source $entry.destination
}

# Shaders, and the common\ headers they include (transitively; -I shaders as in layer/shaders.cpp).
# Source files only; never carry shader caches, debug output or editor files.
Get-ChildItem (Join-Path $root "shaders") -Recurse -File | Where-Object {
  $_.Extension -in @('.hlsl', '.hlsli', '.h') -or $_.Name -eq 'variants.txt'
} | ForEach-Object { Copy-To $_.FullName ("shaders\" + $_.FullName.Substring((Join-Path $root "shaders").Length + 1)) }
# Runtime classification data are opened by layer/alphatest.cpp, not by DXC.
# Keep this explicit: captures, caches and optional offline VT masks are not payload.
foreach ($f in @('shaders\rt\alpha_fs.txt', 'shaders\rt\camera_fade_fs.txt')) {
  Copy-To (Join-Path $root $f) $f
}
# The launcher's compile-before-Play list (BG3RTX.exe --precompile-shaders).
if (Test-Path (Join-Path $root 'shaders\precompile.txt')) { Copy-To (Join-Path $root 'shaders\precompile.txt') 'shaders\precompile.txt' }
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
$payload.Add([pscustomobject]@{ Source = '<generated release defaults>'; Destination = 'bg3rtx.ini'; Exists = $true })
if (-not $DryRun) { $ini | Set-Content -Encoding UTF8 (Join-Path $out "bg3rtx.ini") }

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
foreach ($f in @("install.ps1", "uninstall.ps1", "Install.cmd", "Uninstall.cmd", "diagnostics.ps1", "Diagnostics.cmd")) {
  Copy-To (Join-Path $PSScriptRoot "release\$f") $f
}
$sha = (git -C $root rev-parse HEAD).Trim()
$payload.Add([pscustomobject]@{ Source = '<generated version>'; Destination = 'VERSION.txt'; Exists = $true })
if (-not $DryRun) {
  @("BG3RTX $Version", "commit $sha", "layer $(if ($Diagnostics) { 'diagnostics' } else { 'lean' })",
    "built $(Get-Date -Format 'yyyy-MM-dd HH:mm')") | Set-Content -Encoding UTF8 (Join-Path $out "VERSION.txt")
}
$planned = @($payload | ForEach-Object { $_.Destination.Replace('/', '\') } | Sort-Object)
if (@($planned | Group-Object | Where-Object Count -gt 1).Count) { throw 'duplicate payload destination' }
$expected = @($planned) + @('files.txt') | Sort-Object
if ($DryRun) {
  $expected | Set-Content -Encoding UTF8 (Join-Path $out 'files.txt')
  $payload | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $out 'payload-list.json')
  $manifest = @(Get-Content -LiteralPath (Join-Path $out 'files.txt'))
  $diff = @(Compare-Object $expected $manifest)
  ConvertTo-Json -InputObject @($diff) | Set-Content -Encoding UTF8 (Join-Path $out 'files.diff.json')
  $missingInputs = @($payload | Where-Object { -not $_.Exists })
  [pscustomobject]@{ Files = $expected.Count; Missing = @($missingInputs); ManifestDifferences = $diff.Count;
    Mode = 'plan only: no payload copied; not a distributable package' } |
    ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $out 'audit.json')
  Write-Host "Dry-run payload: $($expected.Count) files; $($missingInputs.Count) missing inputs; $($diff.Count) manifest differences. $out"
  return
}

# files.txt: every shipped file (install.ps1 copies exactly these; uninstall.ps1 removes them).
$files = Get-ChildItem $out -Recurse -File | ForEach-Object { $_.FullName.Substring($out.Length + 1) } | Sort-Object
@($files) + @("files.txt") | Set-Content -Encoding UTF8 (Join-Path $out "files.txt")
$manifest = @(Get-Content -LiteralPath (Join-Path $out 'files.txt'))
if (@(Compare-Object $expected ($manifest | Sort-Object)).Count) { throw 'files.txt differs from planned payload' }
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
