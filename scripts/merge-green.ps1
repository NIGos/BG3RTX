# Merges a branch into main only if main stays green: no conflicts, the build and every unit
# test pass, every runtime shader compiles with DXC, and the QA detector tests pass. Anything
# else aborts the merge and leaves main untouched.
#
#   .\scripts\merge-green.ps1 <branch-or-commit>
#
# Run from the main checkout. Does not touch the game, the queue or any worktree. It rebuilds
# main's bin/, so don't run it while a queued job uses the main tree (it refuses if one does).
param([Parameter(Mandatory = $true)][string]$Ref)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

function Fail($why) {
  git merge --abort 2>$null
  Write-Host "NOT MERGED: $why"
  exit 1
}

$running = Get-ChildItem (Join-Path $root "captures\queue\running\*.json") -ErrorAction SilentlyContinue |
           ForEach-Object { Get-Content $_.FullName -Raw | ConvertFrom-Json } | Where-Object { -not $_.tree }
if ($running) { Write-Host "NOT MERGED: queued job $($running.name) is using the main tree"; exit 1 }
if (git status --porcelain --untracked-files=no) { Write-Host "NOT MERGED: main has uncommitted changes"; exit 1 }

$head = (git rev-parse HEAD).Trim()
git merge --no-ff --no-commit $Ref 2>&1 | Out-Null
if (git diff --name-only --diff-filter=U) { Fail "conflicts in $((git diff --name-only --diff-filter=U) -join ', ')" }

$build = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") -Test 2>&1 | ForEach-Object { "$_" }
if ($LASTEXITCODE -ne 0 -or ($build | Select-String -Pattern 'FAILED|error C|error LNK| [1-9]\d* failed')) {
  $build | Select-Object -Last 15 | ForEach-Object { Write-Host "  $_" }
  Fail "build or unit tests failed"
}

$dxc = Get-ChildItem "C:\VulkanSDK\*\Bin\dxc.exe" -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -Last 1
if ($dxc) {
  $bad = @()
  foreach ($f in Get-ChildItem shaders -Recurse -Filter *.hlsl) {
    if (-not (Select-String -LiteralPath $f.FullName -Pattern '\bmain\s*\(' -Quiet)) { continue }
    $extra = @(if (Select-String -LiteralPath $f.FullName -Pattern '^// dxc: -enable-16bit-types' -Quiet) { "-enable-16bit-types" })
    $inc = @("-I", "shaders", "-I", "shaders\gi", "-I", "shaders\rt", "-I", "external\sharc-1.8.3\include", "-D", "BG3RTX_SHARC_SDK=1")
    & $dxc.FullName -T cs_6_6 -E main -spirv '-fspv-target-env=vulkan1.3' -HV 2021 @extra @inc $f.FullName -Fo "$env:TEMP\mg.spv" 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { $bad += $f.FullName.Replace("$root\", "") }
  }
  if ($bad) { Fail "shaders failed to compile: $($bad -join ', ')" }
}

$qa = py -3 tests\qa_detectors_test.py 2>&1 | ForEach-Object { "$_" }
if ($LASTEXITCODE -ne 0) { $qa | Select-Object -Last 8 | ForEach-Object { Write-Host "  $_" }; Fail "QA detector tests failed" }

git commit -q --no-edit -m "Merge $Ref (merge-green: build, unit tests, shaders, QA tests passed)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
Write-Host "MERGED $Ref onto $head -> $((git rev-parse --short HEAD).Trim())"
