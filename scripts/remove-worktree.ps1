# Remove a worktree under .claude/worktrees safely: unlink every junction without following it
# ([IO.Directory]::Delete(path,false)), check that the shared external/ + data/assetdb file count is
# unchanged before and after, then git worktree remove. Never use rm -rf / git worktree remove directly.
#   powershell -File scripts/remove-worktree.ps1 -Name <folder>
# Run the main checkout's copy: a worktree's copy would count that worktree's own (missing or linked) shared folders.
# A folder that is not a registered worktree only gets its junctions unlinked; the folder itself is left in place.
param([Parameter(Mandatory = $true)][string]$Name)
$ErrorActionPreference = 'Stop'
$m = Split-Path $PSScriptRoot  # the checkout that holds this script (the repo moved from C: to E: in 2026-11)
if (-not (Test-Path -LiteralPath (Join-Path $m '.git') -PathType Container)) {
  throw "remove-worktree.ps1 must be run from the main checkout: $m is a linked worktree (or not a checkout), so its external/ and data/assetdb are not the shared folders this script protects. Run <main checkout>\scripts\remove-worktree.ps1 -Name $Name"
}
if ($Name -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') { throw "worktree name '$Name': a folder name under .claude\worktrees, without path separators" }
if ($Name -eq 'test-main') { throw 'test-main is the live test tree of merge-green and the queue runner: it is never removed' }
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
$registered = @(git worktree list --porcelain | Where-Object { $_ -like 'worktree *' } | ForEach-Object { [IO.Path]::GetFullPath($_.Substring(9)) }) -contains [IO.Path]::GetFullPath($wt)
if (-not $registered) {
  "$Name is not a registered worktree: $($links.Count) links unlinked, shared files $before intact; the folder is left in place (delete it by hand once you have looked inside)"
  exit 0
}
git worktree remove --force $wt
if ($LASTEXITCODE) { throw "git worktree remove failed (exit $LASTEXITCODE): $wt is still there; its junctions are already unlinked" }
if ((SharedCount) -ne $before) { throw 'SHARED FOLDERS CHANGED during worktree removal' }
"removed $Name ($($links.Count) links unlinked, shared files $before intact)"
