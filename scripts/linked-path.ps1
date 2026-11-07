# Dot-sourced by scripts that refuse to write through links (package, setup, RC check).
# A junction or symlink on the path is refused: a recursive clean once followed one and emptied the SDKs.
# A whole-volume mount point (a disk mounted as a folder, target "Volume{guid}\") is a real
# disk, not a redirect into another tree, so it is allowed (the checkout may live on such a disk).

function Test-VolumeMountTarget([string]$Target) {
  return $Target -match '^(\\\\\?\\|\\\?\?\\)?Volume\{[0-9A-Fa-f-]+\}\\?$'
}

# True when $Path itself is a reparse point other than a whole-volume mount point.
function Test-LinkedPath([string]$Path) {
  if (-not (Test-Path -LiteralPath $Path)) { return $false }
  $item = Get-Item -LiteralPath $Path -Force
  if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { return $false }
  $targets = @($item.Target)
  if ($targets.Count -eq 1 -and (Test-VolumeMountTarget ([string]$targets[0]))) { return $false }
  return $true
}

# First linked ancestor of $Path (itself included), or $null.
function Find-LinkedAncestor([string]$Path) {
  for ($p = [IO.Path]::GetFullPath($Path); $p; $p = Split-Path $p) {
    if (Test-LinkedPath $p) { return $p }
  }
  return $null
}
