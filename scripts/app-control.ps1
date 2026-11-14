# Dot-sourced by scripts that start freshly built executables (build.ps1 -Test, the Setup checks).
# Windows Smart App Control blocks unsigned executables it has no reputation for. On 2026-11-14 it turned itself on and
# a fresh Setup and fresh test executables stopped starting, with errors that did not name it. These helpers name it.

# 0 = off, 1 = on (blocking), 2 = evaluation (not blocking yet); $null when Windows has no such policy value.
function Get-SmartAppControlState {
  try {
    return [int](Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy' `
      -Name VerifiedAndReputablePolicyState -ErrorAction Stop).VerifiedAndReputablePolicyState
  } catch { return $null }
}

# One sentence for an error or a warning, or '' when Smart App Control is not blocking. -State is for tests.
function Get-SmartAppControlHint([string]$What, $State = (Get-SmartAppControlState)) {
  if ($State -ne 1) { return '' }
  return "Windows Smart App Control is ON and blocks unsigned, freshly built executables: $What will not start. " +
    'Turn it off in Windows Security > App & browser control > Smart App Control settings, then run this again.'
}
