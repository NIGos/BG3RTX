# Compile the exact assembled ZIP payload. Metadata stays outside the 229-file payload.
param(
  [Parameter(Mandatory=$true)][string]$PayloadDir,
  [Parameter(Mandatory=$true)][string]$OutDir,
  [string]$Compiler = 'C:\Users\quali\AppData\Local\Programs\Inno Setup 6\ISCC.exe'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$PayloadDir = (Resolve-Path -LiteralPath $PayloadDir).Path
$OutDir = [IO.Path]::GetFullPath($OutDir)
$scratch = Join-Path $root '.scratch'
if (-not $OutDir.StartsWith($scratch + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Setup output must be under this checkout .scratch.' }
foreach ($path in @($OutDir, $PayloadDir)) {
  for ($p = $path; $p; $p = Split-Path $p) {
    if ((Test-Path -LiteralPath $p) -and ((Get-Item -LiteralPath $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Linked setup path: $p" }
  }
}
$versionLine = Get-Content -LiteralPath (Join-Path $PayloadDir 'VERSION.txt') -TotalCount 1
if ($versionLine -notmatch '^BG3RTX ([A-Za-z0-9._-]+)$') { throw 'Invalid payload VERSION.txt' }
$version = $Matches[1]
$manifest = @(Get-Content -LiteralPath (Join-Path $PayloadDir 'files.txt') | Where-Object { $_ } | ForEach-Object { $_.Replace('/', '\') })
$actual = @(Get-ChildItem -LiteralPath $PayloadDir -Recurse -File -Force | ForEach-Object { $_.FullName.Substring($PayloadDir.Length + 1) })
if (@($manifest | Group-Object | Where-Object Count -gt 1).Count -or @(Compare-Object ($manifest | Sort-Object) ($actual | Sort-Object)).Count) { throw 'Payload differs from files.txt' }
$entries = @(foreach ($file in $manifest) {
  if ($file -match '(^[\\/]|[:<>"|*?\x00-\x1f]|(^|[\\/])\.{1,2}([\\/]|$))') { throw "Unsafe payload path: $file" }
  $source = Join-Path $PayloadDir $file
  for ($p = $source; $p -and $p.Length -ge $PayloadDir.Length; $p = Split-Path $p) {
    if ((Get-Item -LiteralPath $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked payload: $p" }
  }
  [pscustomobject]@{ path = $file.Replace('\', '/'); sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() }
})
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$hashFile = Join-Path $OutDir 'payload.sha256.json'
[pscustomobject]@{ schema = 1; files = $entries } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $hashFile -Encoding UTF8
$license = Join-Path $OutDir 'setup-license.txt'
((Get-Content -LiteralPath (Join-Path $PayloadDir 'LICENSE') -Raw) + "`r`n`r`n" + (Get-Content -LiteralPath (Join-Path $PayloadDir 'LICENSE-EXCEPTION.md') -Raw)) | Set-Content -LiteralPath $license -Encoding UTF8
# ISCC has no Windows VERSIONINFO: pin the compiler's own banner, not file properties.
$banner = & $Compiler '/?' 2>&1
if ($LASTEXITCODE -ne 0) { throw 'Cannot query Inno compiler' }
if (($banner -join "`n") -notmatch '(?<![\d.])6\.7\.3(?![\d.])') { throw 'Inno Setup 6.7.3 is required' }
Write-Host "Compiling Setup: Inno 6.7.3; $($entries.Count) payload hashes; version $version"
& $Compiler "/DPayloadDir=$PayloadDir" "/DProductVersion=$version" "/DHashManifest=$hashFile" "/DLicensePath=$license" "/O$OutDir" (Join-Path $PSScriptRoot 'BG3RTX.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC failed: $LASTEXITCODE" }
$setup = Join-Path $OutDir "BG3RTX-$version-Setup.exe"
if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) { throw 'Setup executable missing' }
Write-Host "Setup: $setup ($((Get-Item -LiteralPath $setup).Length) bytes)"
