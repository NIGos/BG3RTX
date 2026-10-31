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
Write-Output 'SE console probe: 7 CPU layout/budget/normalization/parser checks PASS; no transport executed'
