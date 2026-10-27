# Logs every bg3.exe start/exit with the checkout that launched it, and flags
# overlaps (two games while one holds the global lock) and games killed early.
#   .\scripts\game-watchdog.ps1 [-Minutes 600]      -> captures\watchdog.log
param([int]$Minutes = 600, [int]$PollSec = 3)
$root = Split-Path $PSScriptRoot
$log = Join-Path $root "captures\watchdog.log"
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
function W($m) { $l = "$(Get-Date -Format 'HH:mm:ss') $m"; Add-Content $log $l; Write-Host $l }

# Which checkout launched a game: the bin folder its VkLayer_bg3rtx.dll was
# loaded from (exact); falls back to the newest bg3rtx.log written after its start.
function Owner($start, $id) {
  $m = $null
  try { $m = (Get-Process -Id $id -ErrorAction Stop).Modules | Where-Object ModuleName -eq "VkLayer_bg3rtx.dll" | Select-Object -First 1 } catch { }
  if ($m) {
    $leaf = Split-Path (Split-Path $m.FileName) -Parent | Split-Path -Leaf   # <checkout>\bin\VkLayer_bg3rtx.dll -> <checkout>
    return $(if ($leaf -like "agent-*") { $leaf } else { "main" })
  }
  # bg3rtx.<tag>.log too: auto-run.ps1 -Tag runs (docs/TEST_SCENES.md, "Concurrent runs").
  $logs = @(Get-ChildItem (Join-Path $root ".claude\worktrees\*\bg3rtx*.log") -ErrorAction SilentlyContinue) + @(Get-ChildItem (Join-Path $root "bg3rtx*.log") -ErrorAction SilentlyContinue)
  $c = $logs | Where-Object { $_.LastWriteTime -ge $start.AddSeconds(-5) } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (-not $c) { return "?" }
  $leaf = Split-Path (Split-Path $c.FullName) -Leaf
  if ($leaf -like "agent-*") { $leaf } else { "main" }
}

$known = @{}
$end = (Get-Date).AddMinutes($Minutes)
W "watchdog started"
while ((Get-Date) -lt $end) {
  $now = @{}
  foreach ($p in Get-Process bg3 -ErrorAction SilentlyContinue) { $now[$p.Id] = $p }
  foreach ($id in $now.Keys) {
    if (-not $known.ContainsKey($id)) {
      $known[$id] = @{ Start = $now[$id].StartTime; Owner = $null }
      W "START pid $id (running now: $($now.Count))"
      if ($now.Count -gt 1) { W "  OVERLAP: $($now.Count) games running: $(($now.Keys | Sort-Object) -join ', ')" }
    } elseif (-not $known[$id].Owner -and ((Get-Date) - $known[$id].Start).TotalSeconds -gt 20) {
      $o = Owner $known[$id].Start $id
      # The layer can load after 20 s (main menu first): retry until it is seen.
      if ($o -ne "?" -or ((Get-Date) - $known[$id].Start).TotalSeconds -gt 180) {
        $known[$id].Owner = $o
        W "  pid $id owner: $o"
      }
    }
  }
  foreach ($id in @($known.Keys)) {
    if (-not $now.ContainsKey($id)) {
      $age = [int]((Get-Date) - $known[$id].Start).TotalSeconds
      $flag = if ($age -lt 60) { "  SHORT-LIVED (possible kill or crash)" } else { "" }
      W "EXIT pid $id owner $($known[$id].Owner) after ${age}s$flag"
      $known.Remove($id)
    }
  }
  Start-Sleep -Seconds $PollSec
}
W "watchdog stopped"
