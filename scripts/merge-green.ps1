# Merge a checked phase into main, build it, run every CTest, compile every
# runtime compute entry point and run the QA detector tests before committing.
# MainRoot permits using this updated gate for its first integration.
param(
  [Parameter(Mandatory = $true)][string]$Ref,
  [string]$MainRoot = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "low-priority.ps1")
$root = if ($MainRoot) { (Resolve-Path -LiteralPath $MainRoot).Path } else { Split-Path $PSScriptRoot -Parent }
Set-Location -LiteralPath $root
$script:mergeStarted = $false

function Fail($why) {
  if ($script:mergeStarted) { git merge --abort 2>$null }
  Write-Host "NOT MERGED: $why"
  exit 1
}

# Run a native gate step and capture stdout+stderr as text. Windows PowerShell 5
# turns every redirected stderr line into an error record, which under 'Stop'
# aborts on the first one (CMake's '-- GLOB mismatch!' when a merge adds files,
# compiler or Python warnings). The step's exit code alone decides pass/fail.
function Invoke-Gate([scriptblock]$step) {
  $ErrorActionPreference = 'Continue'
  # A step that never ran (command not found) must not inherit a previous 0.
  $global:LASTEXITCODE = 1
  try {
    $out = & $step 2>&1 | ForEach-Object { "$_" }
    $script:gateCode = $LASTEXITCODE
  } finally { $ErrorActionPreference = 'Stop' }
  return $out
}

if ((git branch --show-current) -ne 'main') { Fail 'target checkout is not on main' }
git rev-parse -q --verify MERGE_HEAD 2>$null | Out-Null
if ($LASTEXITCODE -eq 0) { Fail 'an existing merge must be resolved first' }
if (Test-Path (Join-Path $root 'captures\queue\USER_PLAYING')) { Fail 'user is playing' }
if (Get-CimInstance Win32_Process | Where-Object { $_.Name -in @('bg3.exe', 'bg3_dx11.exe') }) {
  Fail 'BG3 is running; wait for the current game to finish'
}
$running = Get-ChildItem (Join-Path $root 'captures\queue\running\*.json') -ErrorAction SilentlyContinue |
  ForEach-Object { Get-Content $_.FullName -Raw | ConvertFrom-Json } |
  Where-Object {
    if (-not $_.tree) { $true }
    else { [IO.Path]::GetFullPath((Join-Path $root $_.tree)).TrimEnd('\') -eq $root.TrimEnd('\') }
  }
if ($running) { Fail "queued job $($running.name) is using the main tree" }
if (git status --porcelain --untracked-files=no) { Fail 'main has uncommitted changes' }

$head = (git rev-parse HEAD).Trim()
try {
  # Windows PowerShell 5 treats Git's ordinary stderr progress as error records.
  # Capture it without throwing before we can record ownership of MERGE_HEAD.
  $ErrorActionPreference = 'Continue'
  try {
    $mergeOutput = git merge --no-ff --no-commit $Ref 2>&1 | ForEach-Object { "$_" }
    $mergeCode = $LASTEXITCODE
  } finally { $ErrorActionPreference = 'Stop' }
  git rev-parse -q --verify MERGE_HEAD 2>$null | Out-Null
  $script:mergeStarted = $LASTEXITCODE -eq 0
  if ($mergeCode -ne 0) { Fail "merge failed: $($mergeOutput -join ' ')" }
  if (-not $script:mergeStarted) { Write-Host "ALREADY MERGED: $Ref"; exit 0 }

  $build = Invoke-Gate { powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'scripts\build.ps1') -Test }
  if ($script:gateCode -ne 0) {
    $build | Select-Object -Last 15 | ForEach-Object { Write-Host "  $_" }
    Fail 'build or unit tests failed'
  }
  $build | Select-Object -Last 6 | ForEach-Object { Write-Host "  $_" }

  # The shared compiler covers all entries (including multi-entry rr_post),
  # honors source DXC options, and fails when DXC is absent or any compile fails.
  $shaders = Invoke-Gate { py -3 tools\compile_shaders.py }
  if ($script:gateCode -ne 0) {
    $shaders | Select-Object -Last 20 | ForEach-Object { Write-Host "  $_" }
    Fail 'runtime shader compilation failed'
  }
  $shaders | Select-Object -Last 1 | ForEach-Object { Write-Host "  $_" }

  $qa = Invoke-Gate { py -3 tests\qa_detectors_test.py }
  if ($script:gateCode -ne 0) {
    $qa | Select-Object -Last 8 | ForEach-Object { Write-Host "  $_" }
    Fail 'QA detector tests failed'
  }
  git commit -q --no-edit -m "Merge $Ref (merge-green: build, all CTests, shaders, QA passed)"
  if ($LASTEXITCODE -ne 0) { Fail 'merge commit failed' }
  $script:mergeStarted = $false
  Write-Host "MERGED $Ref onto $head -> $((git rev-parse --short HEAD).Trim())"
} catch { Fail $_.Exception.Message }

# Refresh the test checkout only after a successful integration.
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'scripts\test-tree.ps1')
