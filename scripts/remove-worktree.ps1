# Remove a worktree under .claude/worktrees safely: unlink every junction without following it
# ([IO.Directory]::Delete(path,false)), check that the shared external/ + data/assetdb file count is
# unchanged before and after, then git worktree remove. Never use rm -rf / git worktree remove directly.
#   powershell -File scripts/remove-worktree.ps1 -Name <folder>
param([Parameter(Mandatory = $true)][string]$Name)
$ErrorActionPreference = 'Stop'
$m = Split-Path $PSScriptRoot  # the checkout that holds this script (the repo moved from C: to E: in 2026-11)
$wt = Join-Path $m ".claude\worktrees\$Name"
function SharedCount { (Get-ChildItem "$m\external" -Recurse -File).Count + (Get-ChildItem "$m\data\assetdb" -File).Count }
$before = SharedCount
if ($before -lt 990) { throw "shared folders already incomplete ($before files): stop" }
if (-not (Test-Path $wt)) { "missing $wt"; exit 0 }
# Unlink every reparse point without following it: Directory.Delete(path, false) on a junction removes only the link.
$links = @(Get-ChildItem $wt -Force -Recurse -Attributes ReparsePoint -ErrorAction SilentlyContinue | Sort-Object { $_.FullName.Length } -Descending)
foreach ($l in $links) { [System.IO.Directory]::Delete($l.FullName, $false) }
$left = @(Get-ChildItem $wt -Force -Recurse -Attributes ReparsePoint -ErrorAction SilentlyContinue).Count
if ($left -ne 0) { throw "$left reparse points still in ${wt}, not removing" }
if ((SharedCount) -ne $before) { throw 'shared folders changed while unlinking: stop' }
Set-Location $m
git worktree remove --force $wt
if ((SharedCount) -ne $before) { throw 'SHARED FOLDERS CHANGED during worktree removal' }
"removed $Name ($($links.Count) links unlinked, shared files $before intact)"
