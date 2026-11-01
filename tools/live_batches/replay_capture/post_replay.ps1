# Post step of rc1-replay-s8-fg2 (the queue runs it in the test tree after the run, README "Gates"):
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/live_batches/replay_capture/post_replay.ps1 -Log <kept log> [-DryRun]
# 1. Finds the pack this run wrote (`[replay] pack 1 written: ... -> <dir>`); none: keeps everything.
# 2. Replays it offline (bin\frame_replay.exe --pack <dir>, output <dir>\replay).
# 3. The offline gate: the replay's `hit material (1 frames)` line has qualified > 0 and sampling
#    (reason 6: a lookup into an image the writer omitted, bound NaN) at most the live rate, the
#    ceiling of the live 120-frame lines' sampling per frame (0 at S8 on 2026-11-01).
# 4. Gate passed: deletes the older captures\replay-* packs of this tree (only the newest pack is
#    kept; nothing useless accumulates). Gate failed or no replay: deletes nothing, says why.
# Deletion is never recursive: each file of the pack and of its one `replay` output folder is
# removed, then the empty folders with [IO.Directory]::Delete(path, $false). A folder with a
# reparse point (junction, symlink) or any other subfolder is left alone and reported.
# -Exe / -ReplayOut: for the offline check of this script (defaults: the tree's bin\frame_replay.exe,
# <pack>\replay). -DryRun: says what it would delete, deletes nothing.
param([Parameter(Mandatory)][string]$Log, [switch]$DryRun, [string]$Exe = '', [string]$ReplayOut = '')
$ErrorActionPreference = 'Stop'
$tree = (Get-Location).Path
$captures = Join-Path $tree 'captures'
if (-not (Test-Path -LiteralPath $Log)) { Write-Host "post-replay: no log ($Log); packs kept"; exit 2 }
$text = Get-Content -LiteralPath $Log
$written = @($text | Select-String -Pattern '\[replay\] pack 1 written: ([0-9.]+) MB .* -> (.+)$' | Select-Object -Last 1)
if (-not $written.Count) { Write-Host 'post-replay: no pack written in this run; older packs kept'; exit 2 }
$pack = $written[0].Matches[0].Groups[2].Value.Trim()
if (-not (Test-Path -LiteralPath (Join-Path $pack 'manifest.txt'))) { Write-Host "post-replay: $pack has no manifest; older packs kept"; exit 2 }
$pack = (Resolve-Path -LiteralPath $pack).Path
Write-Host ("post-replay: new pack {0} ({1} MB)" -f $pack, $written[0].Matches[0].Groups[1].Value)

# Live reference: the 120-frame hit material lines of this run (sampling = the 6th reason).
$liveRate = 0.0
$live = @($text | Select-String -Pattern 'hit material \((\d+) frames\): attempted (\d+) qualified (\d+) unsupported \d+; reasons [^ ]+ (\d+)/(\d+)/(\d+)/(\d+)/(\d+)/(\d+)/')
foreach ($l in $live) {
  $g = $l.Matches[0].Groups
  $liveRate = [math]::Max($liveRate, [double]$g[9].Value / [double]$g[1].Value)
}
$limit = [math]::Ceiling($liveRate)

$exe = if ($Exe) { $Exe } else { Join-Path $tree 'bin\frame_replay.exe' }
if (-not (Test-Path -LiteralPath $exe)) { Write-Host "post-replay: $exe missing; older packs kept"; exit 2 }
$out = if ($ReplayOut) { $ReplayOut } else { Join-Path $pack 'replay' }
$replay = @(& $exe --pack $pack --out $out 2>&1 | ForEach-Object { "$_" })
$code = $LASTEXITCODE
New-Item -ItemType Directory -Force $out | Out-Null
$replay | Set-Content -LiteralPath (Join-Path $out 'replay.txt')
$hm = @($replay | Select-String -Pattern 'hit material \(1 frames\): attempted (\d+) qualified (\d+) unsupported \d+; reasons [^ ]+ (\d+)/(\d+)/(\d+)/(\d+)/(\d+)/(\d+)/' | Select-Object -First 1)
$vs = @($replay | Select-String -Pattern 'vs live giRaw: .*' | Select-Object -First 1)
if ($code -ne 0 -or -not $hm.Count) { Write-Host "post-replay: replay failed (exit $code, see replay.txt); older packs kept"; exit 1 }
$g = $hm[0].Matches[0].Groups
$attempted = [int64]$g[1].Value; $qualified = [int64]$g[2].Value; $sampling = [int64]$g[8].Value
Write-Host ("post-replay: replay hit material attempted {0} qualified {1} sampling {2} (limit {3}: live {4:N2}/frame over {5} lines); {6}" -f
  $attempted, $qualified, $sampling, $limit, $liveRate, $live.Count, $(if ($vs.Count) { $vs[0].Line.Trim() } else { 'no live reference' }))
if (-not ($qualified -gt 0 -and $sampling -le $limit)) { Write-Host 'post-replay: offline gate FAILED; older packs kept'; exit 1 }

# Gate passed: remove the older packs, never recursively.
function Remove-Pack([string]$dir) {
  $item = Get-Item -LiteralPath $dir -Force
  if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { return "reparse point" }
  $subs = @(Get-ChildItem -LiteralPath $dir -Directory -Force)
  if (@($subs | Where-Object { $_.Name -ne 'replay' -or ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) }).Count) { return "unexpected subfolder" }
  foreach ($s in $subs) { if (@(Get-ChildItem -LiteralPath $s.FullName -Directory -Force).Count) { return "nested folder in replay" } }
  if ($DryRun) { return $null }
  foreach ($s in $subs) {
    Get-ChildItem -LiteralPath $s.FullName -File -Force | ForEach-Object { [IO.File]::Delete($_.FullName) }
    [IO.Directory]::Delete($s.FullName, $false)
  }
  Get-ChildItem -LiteralPath $dir -File -Force | ForEach-Object { [IO.File]::Delete($_.FullName) }
  [IO.Directory]::Delete($dir, $false)
  $null
}
$freed = 0.0; $removed = 0
foreach ($d in @(Get-ChildItem -LiteralPath $captures -Directory -Force | Where-Object { $_.Name -match '^replay-\d{8}-\d{6}-[\w-]+$' })) {
  if ($d.FullName -eq $pack) { continue }
  $mb = (@(Get-ChildItem -LiteralPath $d.FullName -File -Force) + @(Get-ChildItem -LiteralPath (Join-Path $d.FullName 'replay') -File -Force -ErrorAction SilentlyContinue) |
         Measure-Object Length -Sum).Sum / 1MB
  $why = Remove-Pack $d.FullName
  if ($why) { Write-Host "post-replay: kept $($d.Name) ($why)"; continue }
  $freed += $mb; $removed++
  Write-Host ("post-replay: {0} {1} ({2:N0} MB)" -f $(if ($DryRun) { 'would delete' } else { 'deleted' }), $d.Name, $mb)
}
Write-Host ("post-replay: offline gate passed; {0} older pack(s) {1}, {2:N0} MB; newest kept: {3}" -f $removed, $(if ($DryRun) { 'to delete' } else { 'deleted' }), $freed, (Split-Path $pack -Leaf))
exit 0
