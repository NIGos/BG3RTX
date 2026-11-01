# Dot-source in long offline scripts (build, merge-green, dry runs, post_replay): step start/end lines with
# clock time and duration, a heartbeat for captured output, and a closing step table. The PowerShell twin
# of tools/longrun.py (AGENTS.md "Measuring correctly": progress at least every ~30 s, flushed).
#   Start-LongrunStep 'build'; ...; Stop-LongrunStep 'build' 'ok'
#   $hb = New-LongrunHeartbeat 'ctest'; <each output line> | ForEach-Object { $hb.Tick("$_") }
#   Write-LongrunSummary
if (-not (Get-Variable LongrunSteps -Scope Script -ErrorAction SilentlyContinue)) {
  $script:LongrunSteps = [ordered]@{}
  $script:LongrunStart = Get-Date
}

function Format-LongrunDuration([TimeSpan]$t) {
  $s = [int][math]::Round($t.TotalSeconds)
  if ($s -ge 3600) { return '{0}h{1:00}m' -f [int][math]::Floor($s / 3600), [int][math]::Floor(($s % 3600) / 60) }
  if ($s -ge 60) { return '{0}m{1:00}s' -f [int][math]::Floor($s / 60), ($s % 60) }
  "${s}s"
}

function Start-LongrunStep([string]$Name) {
  $script:LongrunSteps[$Name] = [pscustomobject]@{ Name = $Name; Start = Get-Date; End = $null; Result = 'running' }
  Write-Host ("[{0:HH:mm:ss}] {1}: start" -f (Get-Date), $Name)
}

function Stop-LongrunStep([string]$Name, [string]$Result = 'ok') {
  $s = $script:LongrunSteps[$Name]
  if (-not $s) { return }
  $s.End = Get-Date; $s.Result = $Result
  Write-Host ("[{0:HH:mm:ss}] {1}: {2} in {3}" -f $s.End, $Name, $Result, (Format-LongrunDuration ($s.End - $s.Start)))
}

function Write-LongrunSummary([string]$Title = 'steps') {
  $now = Get-Date
  if (-not $script:LongrunSteps.Count) { return }
  $parts = foreach ($s in $script:LongrunSteps.Values) {
    $end = if ($s.End) { $s.End } else { $now }
    '{0} {1} ({2})' -f $s.Name, (Format-LongrunDuration ($end - $s.Start)), $s.Result
  }
  Write-Host ("{0}: {1}; total {2}" -f $Title, ($parts -join ', '), (Format-LongrunDuration ($now - $script:LongrunStart)))
}

# Reuse of a passed offline check: the SHA-256 of its input files' contents (relative names + hashes)
# and its parameters; the passed run's output lines are kept in the main checkout's captures\.cache\.
function Get-LongrunCacheDir {
  $r = Split-Path $PSScriptRoot -Parent
  if ($r -match '^(.*?)\\\.claude\\worktrees\\') { $r = $matches[1] }
  Join-Path $r 'captures\.cache'
}
function Get-LongrunInputHash([string]$Base, [string[]]$Files, [string]$Extra = '') {
  $base = (Resolve-Path -LiteralPath $Base).Path.TrimEnd('\') + '\'
  $lines = foreach ($f in ($Files | Sort-Object -Unique)) {
    $rel = if ($f.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) { $f.Substring($base.Length) } else { $f }
    '{0} {1}' -f $rel, (Get-FileHash -Algorithm SHA256 -LiteralPath $f).Hash
  }
  $bytes = [Text.Encoding]::UTF8.GetBytes((@($lines) + $Extra) -join "`n")
  $sha = [Security.Cryptography.SHA256]::Create()
  try { -join ($sha.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') }) } finally { $sha.Dispose() }
}
# Returns the kept lines of a passed run with the same hash, else $null.
function Get-LongrunReuse([string]$Name, [string]$Hash) {
  $f = Join-Path (Get-LongrunCacheDir) "$Name.json"
  try { $c = Get-Content -LiteralPath $f -Raw -ErrorAction Stop | ConvertFrom-Json } catch { return $null }
  if ($c.hash -ne $Hash) { return $null }
  $c
}
function Save-LongrunReuse([string]$Name, [string]$Hash, [string[]]$Lines) {
  $dir = Get-LongrunCacheDir
  try {
    New-Item -ItemType Directory -Force $dir | Out-Null
    @{ hash = $Hash; utc = [datetime]::UtcNow.ToString('o'); lines = @($Lines) } | ConvertTo-Json -Depth 4 |
      Set-Content -LiteralPath (Join-Path $dir "$Name.json")
  } catch { }
}

# Heartbeat for a step whose output is captured instead of shown: Tick(line) per output line prints
# "<label>: running <elapsed>, <n> lines, last: <line>" at most every $Every seconds; Log (optional)
# receives every line as it arrives, so the step can be followed live (Get-Content -Wait).
function New-LongrunHeartbeat([string]$Label, [double]$Every = 30, [string]$Log = '') {
  if ($Log) { New-Item -ItemType Directory -Force (Split-Path $Log) | Out-Null; Set-Content -LiteralPath $Log "[$(Get-Date -f s)] $Label" }
  $hb = [pscustomobject]@{ Label = $Label; Every = $Every; Log = $Log; Start = Get-Date; Last = Get-Date; Lines = 0 }
  $hb | Add-Member ScriptMethod Tick {
    param([string]$line)
    $this.Lines++
    if ($this.Log) { try { Add-Content -LiteralPath $this.Log $line } catch { } }
    $now = Get-Date
    if (($now - $this.Last).TotalSeconds -ge $this.Every) {
      $this.Last = $now
      $tail = if ($line.Length -gt 120) { $line.Substring(0, 120) + '...' } else { $line }
      Write-Host ("[{0:HH:mm:ss}] {1}: running {2}, {3} lines, last: {4}" -f $now, $this.Label, (Format-LongrunDuration ($now - $this.Start)), $this.Lines, $tail.Trim())
    }
  }
  $hb
}
