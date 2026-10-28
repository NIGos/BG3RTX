# Configures (first run) and builds with MSVC + Ninja.
#   .\scripts\build.ps1               build everything (RelWithDebInfo)
#   .\scripts\build.ps1 -Core         rebuild only the hot-reloadable core
#   .\scripts\build.ps1 -Watch        rebuild the core on every save under core/
#   .\scripts\build.ps1 -Launcher     build only the player launcher, bin\BG3RTX.exe
#   .\scripts\build.ps1 -Test         build everything, then run every registered CTest
param([switch]$Core, [switch]$Watch, [switch]$Launcher, [switch]$Test, [string]$Config = "RelWithDebInfo")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$build = Join-Path $root "build"

function Enter-VsShell {
  if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if (-not $vs) { throw "No Visual Studio with the C++ x64 tools found" }
  $vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
  cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match "^(.*?)=(.*)$") { Set-Item "env:$($matches[1])" $matches[2] }
  }
}

function Invoke-Build([string]$target) {
  if (-not (Test-Path (Join-Path $build "build.ninja"))) {
    cmake -S $root -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config"
    if ($LASTEXITCODE) { throw "configure failed" }
  }
  if ($target) { cmake --build $build --target $target } else { cmake --build $build }
  if ($LASTEXITCODE) { throw "build failed" }
}

Enter-VsShell
if (-not $Watch) {
  Invoke-Build $(if ($Core) { "bg3rtx_core" } elseif ($Launcher) { "bg3rtx_launcher" } else { "" })
  if ($Test) {
    # merge-green relies on this being the complete configured suite. An empty
    # test registry is a configuration failure, not a successful validation.
    ctest --test-dir $build --output-on-failure --no-tests=error
    if ($LASTEXITCODE) { throw "unit tests failed" }
  }
  return
}

Invoke-Build "bg3rtx_core"
Write-Host "Watching core/ and common/ - the running game reloads the core after each build. Ctrl+C to stop."
$stamp = { (Get-ChildItem (Join-Path $root "core"), (Join-Path $root "common") -Recurse -File |
            Measure-Object LastWriteTimeUtc -Maximum).Maximum }
$last = & $stamp
while ($true) {
  Start-Sleep -Milliseconds 300
  $now = & $stamp
  if ($now -ne $last) {
    $last = $now
    try { Invoke-Build "bg3rtx_core"; Write-Host "$(Get-Date -f HH:mm:ss) core rebuilt" -ForegroundColor Green }
    catch { Write-Host "$(Get-Date -f HH:mm:ss) $_" -ForegroundColor Red }
  }
}
