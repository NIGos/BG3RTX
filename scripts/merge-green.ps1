# Merge a checked phase into main, build it, run every CTest, compile every
# runtime compute entry point and run the QA detector tests before committing.
# MainRoot permits using this updated gate for its first integration.
param(
  [Parameter(Mandatory = $true)][string]$Ref,
  [string]$MainRoot = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "low-priority.ps1")
. (Join-Path $PSScriptRoot "longrun.ps1")
$root = if ($MainRoot) { (Resolve-Path -LiteralPath $MainRoot).Path } else { Split-Path $PSScriptRoot -Parent }
Set-Location -LiteralPath $root
$script:mergeStarted = $false

function Fail($why) {
  Write-LongrunSummary 'merge-green steps'
  Write-Host "NOT MERGED: $why"
  exit 1
}

# Stored in this checkout's Git directory, including linked worktrees.
$ownerPath = (git rev-parse --git-path bg3rtx-merge-green-owner.json).Trim()
function Undo-OwnedMerge($owner) {
  $current = (git rev-parse HEAD).Trim()
  $merging = (git rev-parse -q --verify MERGE_HEAD 2>$null)
  if ($LASTEXITCODE -eq 0) {
    if ($current -ne $owner.head -or "$merging".Trim() -ne $owner.target) {
      throw 'Merge ownership does not match HEAD/MERGE_HEAD; refusing to abort'
    }
    $ErrorActionPreference = 'Continue'
    try {
      git merge --abort 2>&1 | Out-Host
      if ($LASTEXITCODE -ne 0) {
        Write-Host 'merge --abort failed; trying reset --merge HEAD (tracked files only)'
        git reset --merge HEAD 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'Owned merge recovery failed; ownership marker retained' }
      }
    } finally { $ErrorActionPreference = 'Stop' }
    git rev-parse -q --verify MERGE_HEAD 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0 -or (git rev-parse HEAD).Trim() -ne $owner.head) {
      throw 'Owned merge abort verification failed; ownership marker retained'
    }
    Write-Host 'Aborted unfinished merge owned by merge-green'
  }
  $tracked = git status --porcelain --untracked-files=no
  if ($LASTEXITCODE -ne 0 -or $tracked) {
    throw 'Tracked state is dirty or unreadable after merge recovery; refusing to continue; ownership marker retained'
  }
  Remove-Item -LiteralPath $ownerPath
}

# Run a native gate step and capture stdout+stderr as text. Windows PowerShell 5
# turns every redirected stderr line into an error record, which under 'Stop'
# aborts on the first one (CMake's '-- GLOB mismatch!' when a merge adds files,
# compiler or Python warnings). The step's exit code alone decides pass/fail.
# Each step prints its start, end and duration; while it runs, a heartbeat every ~30 s of output
# (elapsed, lines so far, the last line); every line goes to build\merge-green-<name>.log as it arrives.
function Invoke-Gate([string]$name, [scriptblock]$step) {
  Start-LongrunStep $name
  $hb = New-LongrunHeartbeat $name 30 (Join-Path $root "build\merge-green-$name.log")
  $ErrorActionPreference = 'Continue'
  # A step that never ran (command not found) must not inherit a previous 0.
  $global:LASTEXITCODE = 1
  try {
    $out = & $step 2>&1 | ForEach-Object { $l = "$_"; $hb.Tick($l); $l }
    $script:gateCode = $LASTEXITCODE
  } finally { $ErrorActionPreference = 'Stop' }
  Stop-LongrunStep $name $(if ($script:gateCode -eq 0) { 'passed' } else { "FAILED (exit $($script:gateCode))" })
  return $out
}

if ((git branch --show-current) -ne 'main') { Fail 'target checkout is not on main' }
if (Test-Path -LiteralPath $ownerPath) {
  $previous = Get-Content -LiteralPath $ownerPath -Raw | ConvertFrom-Json
  $ownerProcess = Get-Process -Id $previous.pid -ErrorAction SilentlyContinue
  if ($ownerProcess -and $ownerProcess.StartTime.ToUniversalTime().ToString('o') -eq $previous.started) {
    Fail "merge-green owner PID $($previous.pid) is still running"
  }
  try { Undo-OwnedMerge $previous } catch { Fail $_.Exception.Message }
}
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
$target = git rev-parse --verify "$Ref^{commit}" 2>$null
if ($LASTEXITCODE -ne 0) { Fail "invalid merge ref: $Ref" }
$owner = @{ head=$head; target="$target".Trim(); pid=$PID; started=(Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o') }
# Write before invoking Git: a hard kill even during merge leaves a recovery receipt.
$owner | ConvertTo-Json | Set-Content -LiteralPath $ownerPath
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

  $build = Invoke-Gate 'build-ctest' { powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'scripts\build.ps1') -Test }
  if ($script:gateCode -ne 0) {
    $build | Select-Object -Last 15 | ForEach-Object { Write-Host "  $_" }
    Fail 'build or unit tests failed'
  }
  $build | Select-Object -Last 6 | ForEach-Object { Write-Host "  $_" }

  # The shared compiler covers all entries (including multi-entry rr_post),
  # honors source DXC options, and fails when DXC is absent or any compile fails.
  # --pp-verify 4: four random requests compiled from scratch must match the cache (preprocessed-key self-check).
  $shaders = Invoke-Gate 'shaders' { py -3 tools\compile_shaders.py --pp-verify 4 }
  if ($script:gateCode -ne 0) {
    $shaders | Select-Object -Last 20 | ForEach-Object { Write-Host "  $_" }
    Fail 'runtime shader compilation failed'
  }
  $shaders | Select-Object -Last 1 | ForEach-Object { Write-Host "  $_" }

  $qa = Invoke-Gate 'qa' { py -3 tests\qa_detectors_test.py }
  if ($script:gateCode -ne 0) {
    $qa | Select-Object -Last 8 | ForEach-Object { Write-Host "  $_" }
    Fail 'QA detector tests failed'
  }
  git commit -q --no-edit -m "Merge $Ref (merge-green: build, all CTests, shaders, QA passed)"
  if ($LASTEXITCODE -ne 0) { Fail 'merge commit failed' }
  $script:mergeStarted = $false
  Write-LongrunSummary 'merge-green steps'
  Write-Host "MERGED $Ref onto $head -> $((git rev-parse --short HEAD).Trim())"
} catch { Fail $_.Exception.Message }
finally {
  if (Test-Path -LiteralPath $ownerPath) { Undo-OwnedMerge $owner }
}

# Refresh the test checkout only after a successful integration.
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'scripts\test-tree.ps1')
