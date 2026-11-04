# Configures (first run) and builds with MSVC + Ninja.
#   .\scripts\build.ps1               build everything (RelWithDebInfo)
#   .\scripts\build.ps1 -Core         rebuild only the hot-reloadable core
#   .\scripts\build.ps1 -Watch        rebuild the core on every save under core/
#   .\scripts\build.ps1 -Launcher     build only the player launcher, bin\BG3RTX.exe
#   .\scripts\build.ps1 -ReleaseLayer build only the lean release layer, build-nodiag\bin\
#   .\scripts\build.ps1 -Test         build everything, then run every registered CTest
#   .\scriptsuild.ps1 -Test -TestRegex <regex>  build, then run selected CTests
param([switch]$Core, [switch]$Watch, [switch]$Launcher, [switch]$ReleaseLayer, [switch]$Test, [string]$Config = "RelWithDebInfo", [string]$TestRegex = "")
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "low-priority.ps1")
. (Join-Path $PSScriptRoot "longrun.ps1")
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
if ($ReleaseLayer) {
  if ($Core -or $Watch -or $Launcher -or $Test) { throw '-ReleaseLayer must be used alone (with optional -Config)' }
  if (Get-Process -Name bg3, bg3_dx11 -ErrorAction SilentlyContinue) { throw 'game is running: build the release layer later' }
  $build = Join-Path $root 'build-nodiag'
  $layerBin = Join-Path $build 'bin'
  Start-LongrunStep 'release layer'
  # Reassert these settings on every invocation, including existing build trees.
  cmake -S $root -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" '-DBG3RTX_DIAGNOSTICS=OFF' "-DBG3RTX_BIN=$layerBin"
  if ($LASTEXITCODE) { throw 'configure of the lean layer failed' }
  cmake --build $build --target VkLayer_bg3rtx
  if ($LASTEXITCODE) { throw 'build of the lean layer failed' }
  Stop-LongrunStep 'release layer'
  return
}
if (-not $Watch) {
  if ($Test) { Start-LongrunStep 'build' }
  Invoke-Build $(if ($Core) { "bg3rtx_core" } elseif ($Launcher) { "bg3rtx_launcher" } else { "" })
  if ($Test) {
    Stop-LongrunStep 'build'
    # The default is the complete configured suite; merge-green's shader scope
    # supplies a source-consumer regex. An empty
    # test registry is a configuration failure, not a successful validation.
    # ctest prints one line per finished test (n/total, time); the slowest ones are summarised after it.
    Start-LongrunStep 'ctest'
    $times = New-Object System.Collections.Generic.List[object]
    $testArgs = @()
    if ($TestRegex) { $testArgs += @('-R', $TestRegex) }
    ctest --test-dir $build --output-on-failure --no-tests=error @testArgs | ForEach-Object {
      if ($_ -match '^\s*\d+/\d+ Test\s+#\d+: (\S+) \.*\s*(.*?)\s+([0-9.]+) sec') { $times.Add([pscustomobject]@{ Name = $matches[1]; Sec = [double]$matches[3] }) }
      $_
    }
    $code = $LASTEXITCODE
    Stop-LongrunStep 'ctest' $(if ($code) { "FAILED (exit $code)" } else { 'passed' })
    if ($times.Count) {
      $sum = ($times | Measure-Object Sec -Sum).Sum
      $top = $times | Sort-Object Sec -Descending | Select-Object -First 5 | ForEach-Object { '{0} {1:N1} s' -f $_.Name, $_.Sec }
      Write-Host ("slowest tests of {0} ({1:N0} s summed): {2}" -f $times.Count, $sum, ($top -join ', '))
    }
    Write-LongrunSummary 'build.ps1 steps'
    if ($code) { throw "unit tests failed" }
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
