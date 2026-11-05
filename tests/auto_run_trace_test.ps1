# CPU-only runner scheduling checks. No queue entry point or game process.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$runner=Get-Content (Join-Path $repo 'scripts/auto-run.ps1') -Raw
$count=0
function Check([bool]$condition,[string]$label) {
  if(-not $condition) { throw "FAIL $label" }
  $script:count++
}
$scratch=Join-Path $env:TEMP ('bg3-trace-'+[guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $scratch)
try {
# Execute the runner's real phase/trace decisions using a fake request file and
# clock. A slow guard poll crossed an entire phase in pt-dialogue3 (23 and 25).
$tokens=$null;$errors=$null
$runnerAst=[Management.Automation.Language.Parser]::ParseInput($runner,[ref]$tokens,[ref]$errors)
Check (-not $errors.Count) 'runner parses'
$overlay=$runnerAst.Find({param($n) $n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -eq '$env:DISABLE_VK_LAYER_reshade_1'},$true)
$launch=$runnerAst.Find({param($n) $n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -eq '$game' -and $n.Right.Extent.Text -match 'launch-bg3.ps1'},$true)
Check ($overlay -and $overlay.Extent.StartOffset -lt $launch.Extent.StartOffset) 'ReShade disabled before game launch'
$previousOverlay=$env:DISABLE_VK_LAYER_reshade_1
try {
 $env:DISABLE_VK_LAYER_reshade_1='0'
 Invoke-Expression $overlay.Extent.Text
 Check ($env:DISABLE_VK_LAYER_reshade_1 -eq '1') 'test process overrides inherited ReShade enable'
} finally {$env:DISABLE_VK_LAYER_reshade_1=$previousOverlay}
Check ($runner -match 'Write-Host "Test overlay disabled: VK_LAYER_reshade') 'runner logs disabled overlay'
$advance=$runnerAst.Find({param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text.StartsWith('if ($panCaptureSent -and $phaseFinished')},$true)
$advanceCondition=$advance.Clauses[0].Item1.Extent.Text
function Test-Advance {
 $phaseFinished=$elapsed -ge $step.seconds
 Invoke-Expression $advanceCondition
}
$trace=$runnerAst.Find({param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text.StartsWith('if ($null -ne $step.trace_at -and')},$true)
$traceBlock=[scriptblock]::Create($trace.Extent.Text)
function Tagged($dir,$name,$ext) { Join-Path $dir ($name+$ext) }
$root=$scratch;$panIndex=25;$phaseTraceIndex=0;$phaseReady=$true;$traceWrites=0;$phaseTraceWritesStart=0
$step=@{label='energy-on-02';seconds=6.1;trace_at=@(1)}
$elapsed=10;$panCaptureSent=$false
Check (-not (Test-Advance)) 'stalled runner cannot skip settings apply'
$panCaptureSent=$true
Check (-not (Test-Advance)) 'stalled runner cannot skip due trace'
$request=Tagged $root trace .request
Set-Content -LiteralPath $request 'previous phase request'
$planClock=@{Elapsed=@{TotalMilliseconds=10000}}
. $traceBlock
Check ($phaseTraceIndex -eq 0 -and (Get-Content $request -Raw).Trim() -eq 'previous phase request') 'previous pending trace is serialized without overwrite'
Remove-Item -LiteralPath $request
. $traceBlock
Check ($phaseTraceIndex -eq 1 -and (Get-Content $request -Raw) -match 'phase=25.*repeat=0') 'late trace request keeps correct phase attribution'
Check ($elapsed -eq 1 -and $phaseStartMs -eq 9000) 'late trace retains the planned 5.1 s completion tail'
Check (-not (Test-Advance)) 'phase remains stable for late trace completion'
$elapsed=6.1
Check (-not (Test-Advance)) 'phase waits for trace export after completion tail'
$traceWrites++
Check (Test-Advance) 'phase closes after export and completion tail'
Remove-Item -LiteralPath $request
$step.trace_at=@(0);$phaseTraceIndex=0;$elapsed=0
. $traceBlock
Check ($phaseTraceIndex -eq 1 -and (Test-Path -LiteralPath $request)) 'trace_at zero issues a request'
Remove-Item -LiteralPath $request
$step.trace_at=@(0,1);$phaseTraceIndex=0;$elapsed=10
. $traceBlock
Check ($phaseTraceIndex -eq 1) 'first of multiple late requests is issued alone'
$elapsed=10
. $traceBlock
Check ($phaseTraceIndex -eq 1 -and -not (Test-Advance)) 'second request waits without closing phase'
Remove-Item -LiteralPath $request
. $traceBlock
Check ($phaseTraceIndex -eq 2 -and $elapsed -eq 1) 'second late serialized request retains its completion tail'
Remove-Item -LiteralPath $request
$step.Remove('trace_at');$elapsed=6.1
Check (Test-Advance) 'ordinary completed phase still advances'
Write-Host "$count runner trace checks passed"
} finally {
  # Only the explicitly created scratch request and empty scratch directory.
  Remove-Item -LiteralPath (Join-Path $scratch 'trace.request') -ErrorAction SilentlyContinue
  [IO.Directory]::Delete($scratch,$false)
}
