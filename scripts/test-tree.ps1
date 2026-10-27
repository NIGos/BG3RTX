# The test tree: a separate checkout of main that in-game jobs without a "tree" run on, so
# development and merges in the main checkout never change a build under a running game.
#
#   .\scripts\test-tree.ps1            # create it (first time) or move it to main's HEAD, then build
#   .\scripts\test-tree.ps1 -Status    # print the commit it is on
#
# Location: .claude\worktrees\test-main (detached at a main commit). Don't sync it while a
# queued job runs on it: the core would hot-reload mid-run (this script refuses).
param([switch]$Status)

$ErrorActionPreference = "Continue"   # build.ps1 writes native stderr (vswhere) that is not an error
$root = Split-Path $PSScriptRoot -Parent
$tree = Join-Path $root ".claude\worktrees\test-main"

if ($Status) {
  if (Test-Path $tree) { git -C $tree log --oneline -1 } else { "no test tree yet" }
  return
}

$busy = Get-ChildItem (Join-Path $root "captures\queue\running\*.json") -ErrorAction SilentlyContinue |
        ForEach-Object { Get-Content $_.FullName -Raw | ConvertFrom-Json } |
        Where-Object { -not $_.tree -or $_.tree -match 'test-main' }
if ($busy) { Write-Host "NOT SYNCED: job $($busy.name) is running on the test tree"; exit 1 }

$head = (git -C $root rev-parse main).Trim()
if (-not (Test-Path $tree)) {
  git -C $root worktree add --detach $tree $head | Out-Null
} else {
  git -C $tree checkout --detach $head 2>&1 | Out-Null
}
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $tree "scripts\build.ps1") 2>&1 |
  Select-String 'error|FAILED' | ForEach-Object { Write-Host "  $_" }
if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED in the test tree"; exit 1 }
Write-Host "test tree at $(git -C $tree log --oneline -1)"
