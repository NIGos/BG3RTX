# Real ZIP candidate verification. Run from any directory with Windows PowerShell 5.1:
# powershell -NoProfile -ExecutionPolicy Bypass -File scripts/release/verify_rc.ps1
# No game, launcher, upload, Git mutation, shortcut or existing installation runs.
# Setup is compiled and executed only against the isolated throwaway tree.
param([ValidateRange(1,100000)][int]$ExpectedFiles = 219, [switch]$NoBuild)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$report = Join-Path $root 'docs\release\RC_CHECK.md'
$results = [ordered]@{}
foreach ($name in @('Build','Archive','Manifest','Hashes','Exclusions','Licences','Version','Install','Update','Uninstall','Setup','Cleanup')) {
  $results[$name] = [pscustomobject]@{ Status = 'FAIL'; Detail = 'Not reached' }
}
$stage = 'Build'
$scratch = $null
$run = $null
$archive = $null
$commit = 'unknown'
$version = 'unknown'
$trackedState = 'unknown'
$oldEnv = @{}
foreach ($name in @('TEMP','TMP','LOCALAPPDATA','GIT_OPTIONAL_LOCKS')) { $oldEnv[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
function Quote([string]$s) { "'" + $s.Replace("'", "''") + "'" }
function Pass([string]$name, [string]$detail) {
  $results[$name].Status = 'PASS'; $results[$name].Detail = $detail
  Write-Host "[$(Get-Date -Format HH:mm:ss)] PASS ${name}: $detail"
}
function Require([bool]$ok, [string]$message) { if (-not $ok) { throw $message } }
function Hash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function NoLinks([string]$path) {
  for ($p = [IO.Path]::GetFullPath($path); $p; $p = Split-Path $p) {
    if ((Test-Path -LiteralPath $p) -and ((Get-Item -LiteralPath $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
      throw "Refusing linked path: $p"
    }
  }
}
function Files([string]$path) {
  @(Get-ChildItem -LiteralPath $path -Recurse -Force -File | ForEach-Object { $_.FullName.Substring($path.Length + 1).Replace('/', '\') } | Sort-Object)
}
function Snapshot([string]$path) {
  $map = @{}
  foreach ($f in (Files $path)) { $map[$f] = Hash (Join-Path $path $f) }
  return $map
}
function SameSnapshot($before, $after) {
  Require ($before.Count -eq $after.Count) "Snapshot count changed: $($before.Count) -> $($after.Count)"
  foreach ($f in $before.Keys) { Require ($after.ContainsKey($f) -and $before[$f] -eq $after[$f]) "Snapshot changed: $f" }
}
function Run([string]$label, [string]$command) {
  Write-Host "[$(Get-Date -Format HH:mm:ss)] $label"
  $logName = $label -replace '[^A-Za-z0-9_-]', '_'
  $stdout = Join-Path $run "$logName.stdout.log"
  $stderr = Join-Path $run "$logName.stderr.log"
  $body = '$ErrorActionPreference = ''Stop''; try { Set-Location -LiteralPath ' + (Quote $root) + '; ' + $command + '; exit 0 } catch { Write-Error $_ -ErrorAction Continue; exit 1 }'
  $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($body))
  $process = Start-Process -FilePath "$PSHOME\powershell.exe" -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded) `
    -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  # Retain the process handle before it exits: Windows PowerShell's Start-Process
  # can otherwise lose ExitCode for short-lived children.
  $handle = $process.Handle
  $watch = [Diagnostics.Stopwatch]::StartNew()
  while (-not $process.WaitForExit(1000)) {
    if ($watch.Elapsed.TotalSeconds -ge 30) {
      Write-Host "[$(Get-Date -Format HH:mm:ss)] $label still running; logs: $logName.stdout.log / .stderr.log"
      $watch.Restart()
    }
  }
  $code = $process.ExitCode
  Get-Content -LiteralPath $stdout -Tail 12 | ForEach-Object { Write-Host $_ }
  if ($code -ne 0) {
    Get-Content -LiteralPath $stderr -Tail 20 | ForEach-Object { Write-Host $_ }
    throw "$label failed (exit $code); see $stdout and $stderr"
  }
}
function RemoveScratch([string]$path) {
  $tempRoot = [IO.Path]::GetFullPath($oldEnv['TEMP']).TrimEnd('\')
  $full = [IO.Path]::GetFullPath($path)
  Require ($full.StartsWith($tempRoot + '\', [StringComparison]::OrdinalIgnoreCase) -and
    (Split-Path $full -Leaf) -match '^bg3rtx-rc-[0-9a-f]{32}$') "Unsafe scratch cleanup: $full"
  NoLinks $full
  # Preflight all descendants before any deletion, never follow a junction.
  function CheckTree([string]$dir) {
    foreach ($child in Get-ChildItem -LiteralPath $dir -Force) {
      Require (-not ($child.Attributes -band [IO.FileAttributes]::ReparsePoint)) "Linked scratch descendant: $($child.FullName)"
      if ($child.PSIsContainer) { CheckTree $child.FullName }
    }
  }
  CheckTree $full
  Remove-Item -LiteralPath $full -Recurse -Force
  Require (-not (Test-Path -LiteralPath $full)) 'Scratch cleanup failed'
}
try {
  if (-not $NoBuild) {
    Require (-not (Get-Process -Name bg3,bg3_dx11,BG3RTX -ErrorAction SilentlyContinue)) 'Close the game and launcher before building the RC. No process was stopped.'
  }
  $env:GIT_OPTIONAL_LOCKS = '0'
  $commit = (& git -C $root rev-parse HEAD).Trim()
  Require ($LASTEXITCODE -eq 0) 'Cannot read Git HEAD'
  $changes = @(& git -c ("core.excludesfile=" + (Join-Path $root '.gitignore')) -C $root status --porcelain --untracked-files=no)
  Require ($LASTEXITCODE -eq 0) 'Cannot read tracked worktree status'
  $trackedState = if ($changes.Count) { 'dirty tracked files: ' + (($changes | ForEach-Object { $_.Trim() }) -join ', ') } else { 'clean tracked files' }
  $id = [Guid]::NewGuid().ToString('N')
  $scratch = Join-Path $oldEnv['TEMP'] "bg3rtx-rc-$id"
  NoLinks $scratch
  New-Item -ItemType Directory -Path $scratch | Out-Null
  foreach ($name in @('TEMP','TMP','LOCALAPPDATA')) {
    $folder = Join-Path $scratch $name
    New-Item -ItemType Directory -Path $folder | Out-Null
    [Environment]::SetEnvironmentVariable($name, $folder, 'Process')
  }
  $run = Join-Path $root ".scratch\rc\$((Get-Date).ToString('yyyyMMdd-HHmmss'))-$($id.Substring(0,8))"
  NoLinks $run
  New-Item -ItemType Directory -Path $run | Out-Null
  # Existing CMake caches must not redirect writes to a different checkout.
  foreach ($buildName in @('build','build-nodiag')) {
    $buildDir = Join-Path $root $buildName
    $expectedBin = if ($buildName -eq 'build') { Join-Path $root 'bin' } else { Join-Path $buildDir 'bin' }
    NoLinks $buildDir
    NoLinks $expectedBin
    $cachePath = Join-Path $buildDir 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cachePath) {
      $existingCache = Get-Content -LiteralPath $cachePath -Raw
      foreach ($setting in @(@('CMAKE_HOME_DIRECTORY',$root), @('BG3RTX_BIN',$expectedBin))) {
        $value = [regex]::Match($existingCache, ('(?m)^' + $setting[0] + ':[^=]+=([^\r\n]+)')).Groups[1].Value
        Require ($value -and [IO.Path]::GetFullPath($value) -ieq $setting[1]) "Unsafe $buildName cache $($setting[0]): $value"
      }
    }
  }
  $version = "rc-$((Get-Date).ToString('yyyyMMdd'))-$($commit.Substring(0,8))-$($id.Substring(0,8))"
  if ($changes.Count) { $version += '-dirty' }
  if (-not $NoBuild) {
    Run 'build core' ('& ' + (Quote (Join-Path $root 'scripts\build.ps1')) + ' -Core')
    Run 'build launcher' ('& ' + (Quote (Join-Path $root 'scripts\build.ps1')) + ' -Launcher')
    Run 'build lean layer' ('& ' + (Quote (Join-Path $root 'scripts\build.ps1')) + ' -ReleaseLayer')
  }
  foreach ($output in @('bin\bg3rtx_core.dll','bin\BG3RTX.exe','build-nodiag\bin\VkLayer_bg3rtx.dll')) {
    Require (Test-Path -LiteralPath (Join-Path $root $output) -PathType Leaf) "Missing build output: $output"
  }
  $cache = Get-Content -LiteralPath (Join-Path $root 'build-nodiag\CMakeCache.txt') -Raw
  Require ($cache -match '(?m)^BG3RTX_DIAGNOSTICS:BOOL=OFF\r?$') 'Layer diagnostics are not OFF'
  if ($NoBuild) {
    Pass 'Build' 'Existing outputs reused (-NoBuild); compilation/freshness not checked by this invocation. Output files/cache paths checked; diagnostics OFF.'
  } else {
    Pass 'Build' 'Core, launcher and lean layer built from this tree; diagnostics OFF. No shader compile or game run.'
  }

  $stage = 'Archive'
  $packer = Quote (Join-Path $root 'scripts\package.ps1')
  Run 'package real ZIP' ("& $packer -NoBuild -Version " + (Quote $version) + ' -OutDir ' + (Quote $run))
  $archive = Join-Path $run "BG3RTX-$version.zip"
  Require (Test-Path -LiteralPath $archive -PathType Leaf) 'Real archive missing'
  $stage = 'Manifest'
  Run 'package source plan' ("& $packer -DryRun -NoBuild -Version " + (Quote $version) + ' -OutDir ' + (Quote (Join-Path $run 'plan')))
  $planRoot = Join-Path $run "plan\BG3RTX-$version"
  # Windows PowerShell 5.1 emits a JSON array as one pipeline object. Assignment
  # keeps its elements directly; wrapping that pipeline in @() would nest it.
  $plan = Get-Content -LiteralPath (Join-Path $planRoot 'payload-list.json') -Raw | ConvertFrom-Json
  Require ($plan.Count -eq ($ExpectedFiles - 1)) "Unexpected source plan count: $($plan.Count)"
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $zip = [IO.Compression.ZipFile]::OpenRead($archive)
  try {
    $entries = @($zip.Entries | Where-Object { $_.Name })
    $prefix = "BG3RTX-$version/"
    $zipPaths = @()
    foreach ($entry in $entries) {
      $entryPath = $entry.FullName.Replace('\', '/')
      Require ($entryPath.StartsWith($prefix, [StringComparison]::Ordinal)) "Unexpected archive root: $($entry.FullName)"
      $rel = $entryPath.Substring($prefix.Length).Replace('/', '\')
      Require ($rel -and $rel -notmatch '(^[\\/]|[:<>"|*?\x00-\x1f]|(^|[\\/])\.{1,2}([\\/]|$))') "Unsafe ZIP entry: $rel"
      Require (($entry.ExternalAttributes -shr 16 -band 0xF000) -ne 0xA000) "ZIP symlink: $rel"
      $zipPaths += $rel
    }
    Require (@($zipPaths | Group-Object | Where-Object Count -gt 1).Count -eq 0) 'Duplicate ZIP files'
  } finally { $zip.Dispose() }
  [IO.Compression.ZipFile]::ExtractToDirectory($archive, (Join-Path $scratch 'extracted'))
  $payload = Join-Path $scratch "extracted\BG3RTX-$version"
  $manifest = @(Get-Content -LiteralPath (Join-Path $payload 'files.txt') | Where-Object { $_ })
  $actual = @(Files $payload)
  $planned = @($plan | ForEach-Object { $_.Destination.Replace('/', '\') }) + @('files.txt')
  $differences = @()
  foreach ($pair in @(@($manifest,$actual), @($manifest,$zipPaths), @($manifest,$planned))) {
    $differences += @(Compare-Object ($pair[0] | Sort-Object) ($pair[1] | Sort-Object))
  }
  $countDelta = $actual.Count - $ExpectedFiles
  $detail = "$($actual.Count) files; expected $ExpectedFiles (delta $countDelta); $($differences.Count) differences across ZIP / extracted / files.txt / source plan."
  if ($differences.Count) { $detail += ' Paths: ' + (($differences | ForEach-Object { "$($_.SideIndicator) $($_.InputObject)" }) -join ', ') }
  Require ($differences.Count -eq 0 -and $countDelta -eq 0 -and @($manifest | Group-Object | Where-Object Count -gt 1).Count -eq 0) $detail
  Pass 'Manifest' $detail
  Pass 'Archive' ("Real ZIP, {0:N2} MiB; SHA256 {1}; symbols kept in separate archive." -f ((Get-Item -LiteralPath $archive).Length / 1MB), (Hash $archive))

  $stage = 'Hashes'
  $dlls = 0; $sources = 0
  foreach ($entry in $plan) {
    if ($entry.Source.StartsWith('<')) { continue }
    $shipped = Join-Path $payload $entry.Destination
    Require ((Hash $shipped) -eq (Hash $entry.Source)) "Source hash mismatch: $($entry.Destination)"
    $sources++
    if ([IO.Path]::GetExtension($entry.Destination) -ieq '.dll') { $dlls++ }
  }
  $expectedSources = @($plan | Where-Object { -not $_.Source.StartsWith('<') }).Count
  Require ($sources -gt 0 -and $sources -eq $expectedSources -and $dlls -eq 12) "Incomplete hash verification: $sources/$expectedSources sources, $dlls/12 DLLs"
  $sdkCandidates = @((Join-Path $root 'external\streamline-sdk-v2.14.16'), $env:BG3RTX_SL_SDK,
    (Join-Path $root '..\..\..\external\streamline-sdk-v2.14.16'))
  $sdkCache = Get-Content -LiteralPath (Join-Path $root 'build-nodiag\CMakeCache.txt') | Select-String '^BG3RTX_SL_SDK:PATH=(.+)$'
  if ($sdkCache) { $sdkCandidates = @($sdkCache.Matches[0].Groups[1].Value) + $sdkCandidates }
  $sdk = $sdkCandidates | Where-Object { $_ -and (Test-Path -LiteralPath (Join-Path $_ 'include\sl.h')) } | Select-Object -First 1
  Require ([bool]$sdk) 'Production Streamline SDK unavailable for provenance check'
  $sl = @(Get-ChildItem -LiteralPath (Join-Path $payload 'bin\sl') -File -Filter '*.dll')
  foreach ($dll in $sl) {
    Require ((Hash $dll.FullName) -eq (Hash (Join-Path $sdk "bin\x64\$($dll.Name)"))) "Not a production Streamline DLL: $($dll.Name)"
  }
  Pass 'Hashes' "$dlls shipped DLLs and launcher match build/copy sources; $sources verbatim files checked; $($sl.Count) Streamline DLLs match production SDK."

  $stage = 'Exclusions'
  $thirdParty = Get-Content -LiteralPath (Join-Path $root 'scripts\package-third-party.json') -Raw | ConvertFrom-Json
  $allowedDlls = @('bin\VkLayer_bg3rtx.dll','bin\bg3rtx_core.dll') + @($thirdParty.binaries | ForEach-Object { $_.destination.Replace('/', '\') })
  $bad = @($actual | Where-Object {
    $_ -match '(?i)\.(pdb|ilk|obj|lib|exp|py|pyc|pyd|spv|nupkg)$|(^|\\)(external|sdk|tools|tests|sl_dev|development|__pycache__)(\\|$)' -or
    ($_ -match '(?i)\.(exe|msi)$' -and $_ -ine 'bin\BG3RTX.exe') -or
    ($_ -match '(?i)\.dll$' -and $_ -notin $allowedDlls)
  })
  Require ($bad.Count -eq 0) "Forbidden payload: $($bad -join ', ')"
  Pass 'Exclusions' '0 symbols, developer tools, SDK payloads, Aftermath DLLs, Python or unexpected binaries. Private symbols outside player ZIP.'

  $stage = 'Licences'
  $noticeDoc = Get-Content -LiteralPath (Join-Path $payload 'THIRD_PARTY.md') -Raw
  $section = [regex]::Match($noticeDoc, '(?ms)^## Required notice files\r?\n(.*?)(?=^## |\z)')
  $notices = [regex]::Matches($section.Groups[1].Value, '(?m)^- `([^`]+)`\r?$')
  Require ($notices.Count -gt 0) 'Missing required notice inventory'
  foreach ($notice in $notices) { Require (Test-Path -LiteralPath (Join-Path $payload ('licences\' + $notice.Groups[1].Value)) -PathType Leaf) "Missing notice: $notice" }
  foreach ($file in @('LICENSE','LICENSE-EXCEPTION.md','bin\ui\fonts\OFL.txt')) {
    Require (Test-Path -LiteralPath (Join-Path $payload $file) -PathType Leaf) "Missing licence: $file"
  }
  $fontIdentity = Get-Content -LiteralPath (Join-Path $payload 'bin\ui\fonts\manifest.json') -Raw | ConvertFrom-Json
  foreach ($property in $fontIdentity.sha256.PSObject.Properties) {
    Require ((Hash (Join-Path $payload ('bin\ui\fonts\' + $property.Name))) -eq $property.Value) "Font identity mismatch: $($property.Name)"
  }
  $readme = @(Get-Content -LiteralPath (Join-Path $payload 'README.md'))
  Require ($readme[2] -match 'GPL-3\.0-or-later with the NVIDIA SDK exception' -and
    $readme[3] -match '\[LICENSE\]\(LICENSE\).*\[LICENSE-EXCEPTION.md\]\(LICENSE-EXCEPTION.md\)') 'README licence lines 3-4 missing or changed'
  Pass 'Licences' "$($notices.Count) required notices plus project licences and font OFL present, verbatim hashes checked; README licence lines 3-4 PASS."

  $stage = 'Version'
  $versionLines = @(Get-Content -LiteralPath (Join-Path $payload 'VERSION.txt'))
  Require ($versionLines[0] -ceq "BG3RTX $version" -and $versionLines[1] -ceq "commit $commit" -and $versionLines[2] -ceq 'layer lean') 'VERSION.txt does not match candidate/HEAD/lean layer'
  Pass 'Version' "$version; commit matches HEAD; layer lean."

  # Exercise exactly the installer shipped in the ZIP, not the separate Inno prototype.
  # Match release_install_test.py: process discovery is mocked only in the child
  # shells operating on this synthetic TEMP installation. Build guards stay real.
  $stage = 'Install'
  $game = Join-Path $scratch 'fake game'
  $install = Join-Path $scratch 'installed beta'
  New-Item -ItemType Directory -Path (Join-Path $game 'bin'),(Join-Path $install 'bin') | Out-Null
  [IO.File]::WriteAllText((Join-Path $game 'bin\bg3.exe'), 'fake game; never executed')
  foreach ($file in @('bin\BG3RTX.exe','bin\bg3rtx_core.dll','README.md','unrelated.txt','bg3rtx_user.ini')) {
    [IO.File]::WriteAllText((Join-Path $install $file), "original $file")
  }
  $original = Snapshot $install
  $gameBefore = Snapshot $game
  $processMock = 'function Get-Process { [CmdletBinding()] param([string[]]$Name, [int]$Id) if ($Id) { Microsoft.PowerShell.Management\Get-Process -Id $Id } }; '
  $installCommand = $processMock + '& ' + (Quote (Join-Path $payload 'install.ps1')) + ' -Destination ' + (Quote $install) + ' -GameDir ' + (Quote $game) + ' -NoShortcut'
  Run 'install real payload' $installCommand
  foreach ($file in $manifest) { Require ((Hash (Join-Path $install $file)) -eq (Hash (Join-Path $payload $file))) "Installed payload differs: $file" }
  Require (Test-Path -LiteralPath (Join-Path $install '.bg3rtx_install')) 'Install marker missing'
  SameSnapshot $gameBefore (Snapshot $game)
  Pass 'Install' "$($manifest.Count) real payload files hash-match after install; fake game unchanged; 3 original collisions backed up."

  $stage = 'Update'
  [IO.File]::WriteAllText((Join-Path $install 'bin\bg3rtx_core.dll'), 'corrupted installed candidate')
  Run 'update real payload' $installCommand
  foreach ($file in $manifest) { Require ((Hash (Join-Path $install $file)) -eq (Hash (Join-Path $payload $file))) "Updated payload differs: $file" }
  foreach ($file in @('bin\BG3RTX.exe','bin\bg3rtx_core.dll','README.md')) {
    Require ((Hash (Join-Path $install ('.bg3rtx_backup\' + $file))) -eq $original[$file]) "Original backup overwritten: $file"
  }
  foreach ($file in @('bg3rtx_user.ini','unrelated.txt')) { Require ((Hash (Join-Path $install $file)) -eq $original[$file]) "User file modified: $file" }
  Pass 'Update' "$($manifest.Count) files hash-match after same-candidate update; corrupted installed DLL repaired; original backups, settings and unrelated file preserved."

  $stage = 'Uninstall'
  Run 'uninstall real payload' ($processMock + '& ' + (Quote (Join-Path $payload 'uninstall.ps1')) + ' -InstallDir ' + (Quote $install) + ' -GameDir ' + (Quote $game) + ' -NoShortcut -KeepShaderCache -KeepSettings')
  SameSnapshot $original (Snapshot $install)
  SameSnapshot $gameBefore (Snapshot $game)
  Pass 'Uninstall' "$($original.Count)/$($original.Count) original files restored byte-for-byte; fake game unchanged; no owned payload/metadata remains."

  $stage = 'Setup'
  Run 'verify compiled Setup' ('& ' + (Quote (Join-Path $PSScriptRoot 'verify_setup.ps1')) + ' -PayloadDir ' + (Quote $payload) + ' -OutDir ' + (Quote (Join-Path $run 'setup')) + ' -TestRoot ' + (Quote $scratch))
  Pass 'Setup' 'Inno 6.7.3 compiled; 219 ZIP payload hashes match after install/update; settings retained by default and removed on opt-in; shader cache removed; 0 owned files or HKCU Apps entries remain. Shortcuts disabled in qualification; no launcher execution.'

} catch {
  $results[$stage].Status = 'FAIL'
  $results[$stage].Detail = $_.Exception.Message.Replace("`r", ' ').Replace("`n", ' ')
  Write-Host "FAIL ${stage}: $($results[$stage].Detail)"
} finally {
  foreach ($name in $oldEnv.Keys) { [Environment]::SetEnvironmentVariable($name, $oldEnv[$name], 'Process') }
  if ($scratch -and (Test-Path -LiteralPath $scratch)) {
    try {
      Require (-not ($run -and (Test-Path -LiteralPath (Join-Path $run 'setup\setup-active.txt')))) "Setup cleanup is incomplete: retaining $scratch and its generated uninstaller for recovery"
      RemoveScratch $scratch
      Pass 'Cleanup' 'Owned TEMP test tree deleted; no existing installation, cache, shortcuts or game files changed. Setup Apps cleanup is checked in the Setup row.'
    }
    catch { $results['Cleanup'].Detail = $_.Exception.Message; Write-Host "FAIL Cleanup: $_" }
  }
  $allPass = @($results.Values | Where-Object Status -ne 'PASS').Count -eq 0
  $lines = @('# Release candidate check', '', "Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz'). Commit: $commit.",
    "Source state at start: $trackedState (untracked files excluded).",
    "Result: $(if ($allPass) { 'PASS' } else { 'FAIL' }). Candidate version: $version.", '',
    'Rerun: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/release/verify_rc.ps1`.', '',
    '| Item | Result | Evidence |', '|---|---|---|')
  foreach ($name in $results.Keys) { $lines += "| $name | $($results[$name].Status) | $($results[$name].Detail.Replace('|','/')) |" }
  $lines += @('', "Archive: $archive", '',
    'Scope: real ZIP legacy install.ps1/uninstall.ps1 route, no shortcut, settings retained. Update reapplies this candidate, not a different release.',
    'Only synthetic installer child shells mock named-process discovery, as release_install_test.py does; build process guards remain real. No game or payload binary executes.',
    'Setup row covers compiled Inno setup/update/uninstall in TEMP, with shortcuts disabled and isolated LOCALAPPDATA; only PASS qualifies that route. No fresh-PC, signing, shortcut UI, locked-file, interruption, game/visual acceptance or release/upload approval is claimed.',
    'Git metadata is read-only. Generated archives, symbols and logs remain under .scratch/rc and must not be added to Git.')
  if ($NoBuild) {
    $lines += 'This invocation used -NoBuild: only existing output presence/cache paths and lean configuration were checked. Compilation and source freshness require separate build evidence; rerun the default command after the last promotions.'
  }
  $lines | Set-Content -LiteralPath $report -Encoding UTF8
  Write-Host "Report: $report"
}
if ($allPass) { exit 0 } else { exit 1 }
