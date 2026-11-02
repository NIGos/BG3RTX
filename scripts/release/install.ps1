# BG3RTX installer (ships in the package root; scripts/package.ps1). Windows PowerShell 5.1 or later.
#
# BG3RTX changes nothing in the game folder, the registry or the Vulkan loader: BG3RTX.exe starts
# bg3.exe with the layer enabled for that process only (VK_ADD_LAYER_PATH, tools/launcher/launch.cpp).
# Installing is therefore a copy of this folder plus a Start menu shortcut; running BG3RTX.exe
# straight from the unzipped folder works as well.
#
#   Install.cmd                                     (double-click) install to the default folder
#   powershell -ExecutionPolicy Bypass -File install.ps1 [-Destination <folder>] [-NoShortcut]
#
# Default folder: %LOCALAPPDATA%\Programs\BG3RTX. Your saves and the game folder are never touched.
param([string]$Destination = (Join-Path $env:LOCALAPPDATA "Programs\BG3RTX"), [switch]$NoShortcut)
$ErrorActionPreference = "Stop"
$src = $PSScriptRoot
$list = Join-Path $src "files.txt"
if (-not (Test-Path $list)) { throw "files.txt not found next to install.ps1: run it from the unzipped BG3RTX folder" }
if (Get-Process -Name bg3, bg3_dx11 -ErrorAction SilentlyContinue) { throw "Baldur's Gate 3 is running: close it first" }

$dst = [IO.Path]::GetFullPath($Destination).TrimEnd('\')
# Never inside the game folder (its bin\ would mix with ours) or Larian's profile folder (saves).
if ($dst -match '\\Larian Studios(\\|$)') { throw "refusing ${dst}: that is the game's profile folder (saves)" }
for ($p = $dst; $p; $p = Split-Path $p) {
  if (Test-Path (Join-Path $p "bin\bg3.exe")) { throw "refusing ${dst}: inside the game folder $p. Pick a folder of its own." }
}
$marker = Join-Path $dst ".bg3rtx_install"
if ((Test-Path $dst) -and (Get-ChildItem $dst -Force | Select-Object -First 1) -and -not (Test-Path $marker) -and
    ($dst -ne $src.TrimEnd('\'))) {
  throw "$dst exists and is not a BG3RTX install: pick an empty or new folder"
}

$files = Get-Content $list | Where-Object { $_ -and -not $_.Contains("..") -and -not [IO.Path]::IsPathRooted($_) }
if ($dst -ne $src.TrimEnd('\')) {
  # An update: files of the previous version that this one no longer has go first.
  $old = Join-Path $dst "install_manifest.txt"
  if (Test-Path $old) {
    $keep = @{}; foreach ($f in $files) { $keep[$f.ToLowerInvariant()] = $true }
    foreach ($f in Get-Content $old) {
      if (-not $f -or $f.Contains("..") -or [IO.Path]::IsPathRooted($f) -or $keep.ContainsKey($f.ToLowerInvariant())) { continue }
      $p = Join-Path $dst $f
      if (Test-Path $p -PathType Leaf) { Remove-Item -Force $p }
    }
  }
  foreach ($f in $files) {
    $from = Join-Path $src $f
    if (-not (Test-Path $from)) { throw "package incomplete: $f is missing" }
    $to = Join-Path $dst $f
    New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
    Copy-Item -Force $from $to
  }
}
# bg3rtx_user.ini (your settings) is not in the package and survives an update.
$files | Set-Content -Encoding UTF8 (Join-Path $dst "install_manifest.txt")
"BG3RTX install marker: uninstall.ps1 removes only what install_manifest.txt lists and the files BG3RTX writes." |
  Set-Content -Encoding UTF8 $marker

$lnk = Join-Path ([Environment]::GetFolderPath("Programs")) "BG3RTX.lnk"
if (-not $NoShortcut) {
  $sh = New-Object -ComObject WScript.Shell
  $s = $sh.CreateShortcut($lnk)
  $s.TargetPath = Join-Path $dst "bin\BG3RTX.exe"
  $s.WorkingDirectory = Join-Path $dst "bin"
  $s.Description = "BG3RTX settings and launcher"
  $s.Save()
}

$exe = Join-Path $dst "bin\BG3RTX.exe"
Write-Host "BG3RTX installed in $dst"
if (-not $NoShortcut) { Write-Host "Start menu: BG3RTX" }
Write-Host ""
Write-Host "Next:"
Write-Host "  1. Start $exe, pick a preset (it suggests one for your GPU) and press Save and launch."
Write-Host "  2. Or, to start through Steam: game Properties > General > Launch options:"
Write-Host "       `"$exe`" %command%"
Write-Host "  3. In the game's Graphics options set the upscaler to DLSS."
Write-Host "Uninstall: $(Join-Path $dst 'Uninstall.cmd')"
