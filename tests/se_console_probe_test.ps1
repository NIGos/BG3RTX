# CPU only: never calls AttachConsole, input APIs or any game process.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot
foreach($name in @('auto-run.ps1','se-console-probe.ps1')){
 $errors=@();[void][Management.Automation.Language.Parser]::ParseFile((Join-Path $repo "scripts/$name"),[ref]$null,[ref]$errors)
 if($errors.Count){throw ($errors|ForEach-Object Message|Out-String)}
}
Add-Type -Path (Join-Path $repo 'scripts/se-console-probe.cs')
if([BG3RTX.SeConsoleProbe]::RecordSize() -ne 20){throw 'Wrong Win32 INPUT_RECORD layout'}
if([BG3RTX.SeConsoleProbe]::Normalize("a`r`nb`nc") -ne "a`rb`rc"){throw 'Console CRLF/LF normalization failed'}
if([BG3RTX.SeConsoleProbe]::Normalize('') -ne ''){throw 'Empty input changed'}
if([BG3RTX.SeConsoleProbe]::Normalize("`n") -ne "`r"){throw 'Native newline terminator lost'}
$rejected=$false;try{[void][BG3RTX.SeConsoleProbe]::Normalize(('x'*16385))}catch{$rejected=$true}
if(-not $rejected){throw 'Oversized probe accepted'}
$count=7
function Check([bool]$ok,[string]$label){if(-not $ok){throw $label};$script:count++}
function Snap([int]$x,[int]$y,[string]$line,[int]$width=120){
 $s=New-Object BG3RTX.SeConsoleProbe+Snapshot
 $s.Width=$width;$s.Height=300;$s.X=$x;$s.Y=$y;$s.Line=$line;return $s
}
$before=Snap 5 10 'S >> '
Check ($null -eq [BG3RTX.SeConsoleProbe]::FreshPrompt($before,$before)) 'Old prompt accepted'
Check ([BG3RTX.SeConsoleProbe]::FreshPrompt($before,(Snap 5 11 'S >> ')) -eq 'S') 'New server prompt lost'
Check ([BG3RTX.SeConsoleProbe]::FreshPrompt($before,(Snap 5 20 'C >> ')) -eq 'C') 'New client prompt lost'
Check ($null -eq [BG3RTX.SeConsoleProbe]::FreshPrompt($before,(Snap 5 9 'C >> '))) 'Scrolled cursor accepted'
Check ($null -eq [BG3RTX.SeConsoleProbe]::FreshPrompt($before,(Snap 5 11 'C >> ' 121))) 'Resized buffer accepted'
Check ($null -eq [BG3RTX.SeConsoleProbe]::FreshPrompt($before,(Snap 5 11 'fake '))) 'Non-prompt accepted'
Check ($null -eq [BG3RTX.SeConsoleProbe]::FreshPrompt($before,(Snap 5 11 'C >> extra'))) 'Prompt substring accepted'
Check ($null -eq [BG3RTX.SeConsoleProbe]::FreshPrompt($before,(Snap 5 11 '...> '))) 'Continuation prompt accepted'
$lua=Get-Content (Join-Path $repo 'scripts/probes/se-blackbody.lua') -Raw
$line=[BG3RTX.SeConsoleProbe]::SingleLine($lua.Replace('__BG3RTX_PROBE_NONCE__',('a'*32)))
Check ($line.Length -gt 0 -and $line.Length -le 8190 -and $line -notmatch '[\r\n]' -and $line -notmatch '--') 'Fixed payload not bounded single-line'
Check ([BG3RTX.SeConsoleProbe]::PlannedEvents($line+"`n") -eq 2*($line.Length+1)) 'Single native batch event count wrong'
foreach($bad in @('x -- hidden',('x'*8191),'-- comment only')){
 $reject=$false;try{[void][BG3RTX.SeConsoleProbe]::SingleLine($bad)}catch{$reject=$true}
 Check $reject 'Unsafe single-line source accepted'
}
$helper=Get-Content (Join-Path $repo 'scripts/se-console-probe.ps1') -Raw
Check ([regex]::Matches($helper,'\[BG3RTX.SeConsoleProbe\]::Write\(').Count -eq 1 -and $helper -match 'function Write-OwnedConsole[^}]*Assert-OwnedProbe[^}]*::Write\(') 'Console write bypasses ownership check'
Check ($helper -match 'function Wait-FreshProbePrompt[^}]*Assert-OwnedProbe' -and $helper -match 'if\(\$safeToRestore -and -not \$script:inputUncertain\)') 'Wait/restoration safety boundary absent'
Write-Output "SE console probe: $count CPU checks PASS; no transport executed"
