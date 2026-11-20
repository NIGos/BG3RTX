# Dot-sourced by verify_rc.ps1 and verify_setup.ps1: the HKCU Apps entry of an installed BG3RTX Setup.
# The compiled Setup route only runs where no BG3RTX is installed for this user: it never touches an existing install.
# Where one is present, the RC check reports the Setup row as SKIPPED (not FAIL) and names the install.
$BG3RTXAppsKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{8A32895E-1307-462E-B807-BE5B8072A9F5}_is1'

# The version of the files in an install folder: the first line of its VERSION.txt ("BG3RTX <version>"), '' if absent.
# The Apps entry's DisplayVersion is written only by the Setup; a ZIP update or a later copy over the folder leaves it
# stale (RC 2026-11-20 named hairtest10 while VERSION.txt said hairtest18).
function Get-InstalledVersion($location) {
  if (-not $location) { return '' }
  $file = Join-Path ([string]$location) 'VERSION.txt'
  if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { return '' }
  $line = Get-Content -LiteralPath $file -TotalCount 1 -ErrorAction SilentlyContinue
  if ($line -match '^\s*BG3RTX\s+(\S+)') { return $Matches[1] }
  return ''
}

function Get-BG3RTXAppsEntries {
  foreach ($view in @([Microsoft.Win32.RegistryView]::Registry32, [Microsoft.Win32.RegistryView]::Registry64)) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, $view)
    try {
      $key = $base.OpenSubKey($BG3RTXAppsKey)
      if ($key) {
        try {
          $location = $key.GetValue('InstallLocation')
          [pscustomobject]@{ Location = $location; Icon = $key.GetValue('DisplayIcon'); Version = $key.GetValue('DisplayVersion')
                             Installed = Get-InstalledVersion $location }
        }
        finally { $key.Dispose() }
      }
    } finally { $base.Dispose() }
  }
}

# '' when the Setup route may run; otherwise the reason it is skipped, with the install it found.
function Get-SetupSkipReason($entries) {
  $entries = @($entries | Where-Object { $_ })
  if (-not $entries.Count) { return '' }
  $e = $entries[0]
  $where = if ($e.Location) { [string]$e.Location } else { 'unknown folder' }
  $installed = if ($e.PSObject.Properties['Installed']) { [string]$e.Installed } else { '' }
  $ver = if ($installed) {
    if ($e.Version -and [string]$e.Version -ne $installed) { "$installed from VERSION.txt; Apps entry says $($e.Version)" } else { $installed }
  } elseif ($e.Version) { [string]$e.Version } else { 'unknown version' }
  return "skipped: another BG3RTX install present ($where, $ver). The compiled Setup was not run, so it is not qualified here: run verify_rc on a PC without a BG3RTX install, or uninstall it first."
}

# The report's Result from the row statuses: FAIL if any row failed; skipped rows are named, never counted as PASS.
function Get-RcResult([string[]]$statuses) {
  if ($statuses -contains 'FAIL') { return 'FAIL' }
  $skipped = @($statuses | Where-Object { $_ -eq 'SKIPPED' }).Count
  if ($skipped) { return "PASS with $skipped skipped row$(if ($skipped -gt 1) { 's' })" }
  return 'PASS'
}
