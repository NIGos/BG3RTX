# Actual compiled setup only; called by verify_rc.ps1 with isolated LOCALAPPDATA/TEMP.
param(
  [Parameter(Mandatory=$true)][string]$PayloadDir,
  [Parameter(Mandatory=$true)][string]$OutDir,
  [Parameter(Mandatory=$true)][string]$TestRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$install = Join-Path $TestRoot 'setup installed beta'
$keyName = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{8A32895E-1307-462E-B807-BE5B8072A9F5}_is1'
function Require([bool]$ok, [string]$message) { if (-not $ok) { throw $message } }
function AppsEntries {
  foreach ($view in @([Microsoft.Win32.RegistryView]::Registry32, [Microsoft.Win32.RegistryView]::Registry64)) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, $view)
    try {
      $key = $base.OpenSubKey($keyName)
      if ($key) {
        try { [pscustomobject]@{ Location = $key.GetValue('InstallLocation'); Icon = $key.GetValue('DisplayIcon'); Version = $key.GetValue('DisplayVersion') } }
        finally { $key.Dispose() }
      }
    } finally { $base.Dispose() }
  }
}
function Execute([string]$exe, [string]$arguments) {
  Write-Host "[$(Get-Date -Format HH:mm:ss)] Setup test: $exe $arguments"
  $process = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Hidden
  $handle = $process.Handle
  $watch = [Diagnostics.Stopwatch]::StartNew()
  while (-not $process.WaitForExit(1000)) {
    if ($watch.Elapsed.TotalSeconds -ge 30) { Write-Host 'Setup test still running'; $watch.Restart() }
  }
  Require ($process.ExitCode -eq 0) "Setup process failed: $($process.ExitCode)"
  if ((Split-Path $exe -Leaf) -eq 'unins000.exe') {
    # Inno can delegate final cleanup to a temporary copy of the uninstaller.
    $cleanup = [Diagnostics.Stopwatch]::StartNew()
    while ((Test-Path -LiteralPath $exe) -or @(AppsEntries).Count) {
      Require ($cleanup.Elapsed.TotalSeconds -lt 30) 'Timed out waiting for uninstaller cleanup'
      Start-Sleep -Milliseconds 100
    }
  }
}
function CheckPayload {
  foreach ($file in $manifest) {
    Require ((Get-FileHash -LiteralPath (Join-Path $install $file)).Hash -eq (Get-FileHash -LiteralPath (Join-Path $PayloadDir $file)).Hash) "Setup hash mismatch: $file"
  }
  Require ((Get-Content -LiteralPath (Join-Path $install 'bg3rtx_user.ini') -Raw) -ceq 'keep my settings') 'Setup modified settings'
  $apps = @(AppsEntries)
  Require ($apps.Count -gt 0) 'Missing HKCU Apps entry'
  foreach ($entry in $apps) {
    Require ($entry.Location.TrimEnd('\') -ieq $install) 'Apps entry points outside throwaway install'
    Require ($entry.Icon -ieq (Join-Path $install 'bin\ui\branding\bg3rtx.ico')) 'Wrong Apps icon'
    Require ($entry.Version -ceq $version) 'Wrong Apps version'
  }
  Require (Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA 'BG3RTX\backups')) 'Lifecycle did not use isolated LOCALAPPDATA'
}
function CheckRemoved([bool]$keep) {
  Require (@(AppsEntries).Count -eq 0) 'Uninstaller left the HKCU Apps entry'
  $remaining = @(Get-ChildItem -LiteralPath $install -Recurse -File -Force | ForEach-Object { $_.FullName.Substring($install.Length + 1) })
  $expected = @('unrelated.txt')
  if ($keep) { $expected += 'bg3rtx_user.ini' }
  Require (@(Compare-Object ($expected | Sort-Object) ($remaining | Sort-Object)).Count -eq 0) "Uninstaller leftovers/settings mismatch: $($remaining -join ', ')"
  Require (-not (Test-Path -LiteralPath $cache)) 'Uninstaller retained shader cache file'
}
Require (@(AppsEntries).Count -eq 0) 'Existing BG3RTX Apps entry: refusing to modify an existing installation'
$TestRoot = [IO.Path]::GetFullPath($TestRoot)
Require ($TestRoot -ieq (Split-Path $env:LOCALAPPDATA)) 'Setup check requires isolated LOCALAPPDATA under TestRoot'
Require ([IO.Path]::GetFullPath($env:TEMP).StartsWith($TestRoot + '\', [StringComparison]::OrdinalIgnoreCase)) 'Setup check requires isolated TEMP'
Require (-not (Test-Path -LiteralPath $install)) 'Throwaway setup destination already exists'
Require (-not (Get-Process -Name bg3,bg3_dx11,BG3RTX -ErrorAction SilentlyContinue)) 'Close game and launcher before setup verification; no process was stopped'
& (Join-Path $PSScriptRoot 'build_setup.ps1') -PayloadDir $PayloadDir -OutDir $OutDir
$version = (Get-Content -LiteralPath (Join-Path $PayloadDir 'VERSION.txt') -TotalCount 1).Substring(7)
$setup = Join-Path $OutDir "BG3RTX-$version-Setup.exe"
$manifest = @(Get-Content -LiteralPath (Join-Path $PayloadDir 'files.txt') | Where-Object { $_ })
Require ($manifest.Count -eq 221) 'Setup qualification expects the 221-file release payload'
New-Item -ItemType Directory -Path $install | Out-Null
[IO.File]::WriteAllText((Join-Path $install 'bg3rtx_user.ini'), 'keep my settings')
[IO.File]::WriteAllText((Join-Path $install 'unrelated.txt'), 'unrelated file')
$cache = Join-Path $env:LOCALAPPDATA 'BG3RTX\shader_cache\setup-test.bin'
New-Item -ItemType Directory -Force -Path (Split-Path $cache) | Out-Null
[IO.File]::WriteAllText($cache, 'throwaway shader cache')
# No shell shortcuts are created in the user's real Start menu or desktop.
$setupArguments = '/VERYSILENT /SUPPRESSMSGBOXES /CURRENTUSER /NORESTART /TASKS="" /DIR="' + $install + '"'
$uninstaller = Join-Path $install 'setup-support\unins000.exe'
$activeMarker = Join-Path $OutDir 'setup-active.txt'
[IO.File]::WriteAllText($activeMarker, $install)
try {
  Execute $setup ($setupArguments + ' /LOG="' + (Join-Path $OutDir 'install.log') + '"')
  CheckPayload
  [IO.File]::WriteAllText((Join-Path $install 'bin\bg3rtx_core.dll'), 'corrupt before update')
  Execute $setup ($setupArguments + ' /LOG="' + (Join-Path $OutDir 'update.log') + '"')
  CheckPayload
  Execute $uninstaller '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART'
  CheckRemoved $true
  Execute $setup ($setupArguments + ' /LOG="' + (Join-Path $OutDir 'reinstall.log') + '"')
  CheckPayload
  Execute $uninstaller '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /REMOVESETTINGS=1'
  CheckRemoved $false
  Write-Host 'SETUP_PASS: 221 hashes after install/update; settings retained by default and removed on opt-in; cache removed; 0 owned files/Apps entries remain.'
} finally {
  # On failed qualification, only the generated uninstaller may remove its Apps entry.
  if (Test-Path -LiteralPath $uninstaller) { Execute $uninstaller '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' }
  Require (@(AppsEntries).Count -eq 0) 'Setup cleanup failed: throwaway Apps entry remains; run its generated uninstaller'
  Remove-Item -LiteralPath $activeMarker
}
