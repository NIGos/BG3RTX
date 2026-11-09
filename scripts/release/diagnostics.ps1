# BG3RTX diagnostic package. Windows PowerShell 5.1; no game launch or settings changes.
# -SavePath is an explicit selection for scripted use/tests; ordinary runs include no save.
# -IncludeLatestSave (opt-in; Diagnostics.cmd asks, default No) adds the newest Story save and its
# thumbnail under saves/<folder>/. -PlayerProfiles overrides the Larian profile folder (tests).
param([string]$InstallDir = $PSScriptRoot, [switch]$IncludeSave, [string]$SavePath = "",
      [switch]$IncludeLatestSave, [string]$PlayerProfiles = "")
$ErrorActionPreference = "Stop"
try { (Get-Process -Id $PID).PriorityClass = 'BelowNormal' } catch { }

function Assert-GameClosed {
  if (Get-Process -Name bg3, bg3_dx11 -ErrorAction SilentlyContinue) {
    throw "Baldur's Gate 3 is running: close it before including a save."
  }
}
function Redact([string]$text) {
  # Also cover other user profiles and escaped/forward-slash paths in JSON and URLs.
  $text = [regex]::Replace($text, '(?i)([a-z]:[\\/]+(?:Users|Documents and Settings)[\\/]+)[^\\/\r\n"'']+', '${1}[USER]')
  if ($env:USERPROFILE) {
    $text = [regex]::Replace($text, [regex]::Escape($env:USERPROFILE), '[USERPROFILE]', 'IgnoreCase')
  }
  $credential = '(?i)(["'']?\b(?:api[_-]?key|access[_-]?token|refresh[_-]?token|auth[_-]?token|client[_-]?secret|private[_-]?key|license[_-]?key|password|passwd|secret|token|key)\b["'']?\s*[:=]\s*)(?:"[^"\r\n]*"|''[^''\r\n]*''|[^\s,;&}\]\r\n]+)'
  $text = [regex]::Replace($text, $credential, '${1}[REDACTED]')
  $text = [regex]::Replace($text, '(?i)(\bBearer\s+)[a-z0-9._~+/-]+=*', '${1}[REDACTED]')
  $text = [regex]::Replace($text, '(?i)(https?://)[^\s/@:]+:[^\s/@]+@', '${1}[REDACTED]@')
  $text = [regex]::Replace($text, '\b(?:sk-[a-zA-Z0-9_-]{16,}|gh[pousr]_[a-zA-Z0-9_]{16,}|AKIA[A-Z0-9]{16})\b', '[REDACTED]')
  return $text
}
function Assert-NoLinks([string]$path) {
  # Do not follow junctions/symlinks into unrelated folders, including through ancestors.
  for ($p = [IO.Path]::GetFullPath($path); $p; $p = Split-Path $p) {
    if ((Test-Path -LiteralPath $p) -and
        ((Get-Item -LiteralPath $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
      throw "Refusing a linked input/output path."
    }
  }
}

$MaxSaveBytes = 200MB # Same report limit as the launcher (diagnostics.h default_max_save_bytes).
function Test-Link($item) { [bool]($item.Attributes -band [IO.FileAttributes]::ReparsePoint) }
# Newest <PlayerProfiles>\<profile>\Savegames\Story\<folder>\*.lsv by last write. Bounded,
# nonrecursive listing; linked folders/files are skipped; nothing is opened here.
function Find-LatestSave([string]$profiles) {
  if (-not $profiles -or -not (Test-Path -LiteralPath $profiles -PathType Container)) { return $null }
  try { Assert-NoLinks $profiles } catch { return $null }
  $best = $null
  foreach ($profile in @(Get-ChildItem -LiteralPath $profiles -Directory -Force | Select-Object -First 64)) {
    if (Test-Link $profile) { continue }
    $games = Join-Path $profile.FullName 'Savegames'
    $story = Join-Path $games 'Story'
    if (-not (Test-Path -LiteralPath $story -PathType Container)) { continue }
    if ((Test-Link (Get-Item -LiteralPath $games -Force)) -or (Test-Link (Get-Item -LiteralPath $story -Force))) { continue }
    foreach ($folder in @(Get-ChildItem -LiteralPath $story -Directory -Force | Select-Object -First 8192)) {
      if (Test-Link $folder) { continue }
      foreach ($lsv in @(Get-ChildItem -LiteralPath $folder.FullName -File -Force | Select-Object -First 64)) {
        if ($lsv.Extension -ine '.lsv' -or (Test-Link $lsv)) { continue }
        if (-not $best -or $lsv.LastWriteTimeUtc -gt $best.LastWriteTimeUtc) { $best = $lsv }
      }
    }
  }
  return $best
}

$root = [IO.Path]::GetFullPath($InstallDir)
Assert-NoLinks $root
if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw "Install folder not found." }
if ($SavePath -or $IncludeSave) { Assert-GameClosed }
if ($IncludeSave) { Add-Type -AssemblyName System.Windows.Forms }
if ($IncludeSave -and -not $SavePath) {
  $dialog = New-Object System.Windows.Forms.OpenFileDialog
  try {
    $dialog.Title = "Optional: choose ONE save to include (Cancel includes no save)"
    $dialog.Filter = "Baldur's Gate 3 save (*.lsv)|*.lsv"
    $dialog.Multiselect = $false
    # No save directory discovery or enumeration: the user navigates to their chosen file.
    if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { $SavePath = $dialog.FileName }
  } finally { $dialog.Dispose() }
}
$save = $null
if ($SavePath) {
  Assert-GameClosed
  Assert-NoLinks $SavePath
  $save = Get-Item -LiteralPath $SavePath -Force
  if ($save.PSIsContainer -or $save.Extension -ine '.lsv') { throw "Select one .lsv save file." }
  Write-Host ("Selected save: {0:N0} bytes ({1:N2} MB). Original is opened read-only." -f $save.Length, ($save.Length / 1MB))
  if ($IncludeSave) {
    $answer = [System.Windows.Forms.MessageBox]::Show(
      ("Include this save ({0:N0} bytes)? Saves can contain personal data and cannot be redacted." -f $save.Length),
      "BG3RTX diagnostics", 'YesNo', 'Warning', 'Button2')
    if ($answer -ne [System.Windows.Forms.DialogResult]::Yes) { $save = $null }
  }
}

Add-Type -AssemblyName System.IO.Compression
$utf8 = New-Object System.Text.UTF8Encoding($false)
$contents = New-Object System.Collections.Generic.List[string]
$cfg = New-Object System.Text.StringBuilder
$device = New-Object System.Text.StringBuilder
$notes = New-Object System.Collections.Generic.List[string]
$progressClock = [Diagnostics.Stopwatch]::StartNew()
function Report-Progress([string]$name, [long]$done, [long]$total, [Diagnostics.Stopwatch]$timer) {
  if ($progressClock.Elapsed.TotalSeconds -ge 30) {
    $rate = $done / [Math]::Max($timer.Elapsed.TotalSeconds, 0.001)
    $eta = ($total - $done) / [Math]::Max($rate, 1)
    Write-Host ("Collecting {0}: {1:N0}/{2:N0} bytes, {3:N2} MB/s, ETA {4:N0} s, failures 0" -f
      $name, $done, $total, ($rate / 1MB), $eta)
    $progressClock.Restart()
  }
}
function Add-Text([string]$name, [string]$text) {
  $entry = $archive.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
  $writer = New-Object IO.StreamWriter($entry.Open(), $utf8)
  try { $writer.Write((Redact $text)) } finally { $writer.Dispose() }
  $contents.Add($name)
}
function Add-Log([IO.FileInfo]$file, [string]$name) {
  $timer = [Diagnostics.Stopwatch]::StartNew()
  Assert-NoLinks $file.FullName
  $entry = $archive.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
  $reader = New-Object IO.StreamReader($file.FullName, $utf8, $true)
  try {
    $writer = New-Object IO.StreamWriter($entry.Open(), $utf8)
    try {
      while ($null -ne ($line = $reader.ReadLine())) {
        $line = Redact $line
        $writer.WriteLine($line)
        Report-Progress $name $reader.BaseStream.Position $file.Length $timer
        if ($line -match '\[cfg\]\s+(knobs|keys):') { [void]$cfg.AppendLine("${name}: $line") }
        if ($line -match '(?i)\[layer\].*GPU|\[(?:sl|device|gpu)\].*(?:device|driver|GPU|OS version)') {
          [void]$device.AppendLine("${name}: $line")
        }
      }
    } finally { $writer.Dispose() }
  } finally { $reader.Dispose() }
  $contents.Add($name)
}
function Add-Binary([IO.FileInfo]$file, [string]$name, [bool]$isSave = $false) {
  $timer = [Diagnostics.Stopwatch]::StartNew()
  Assert-NoLinks $file.FullName
  if ($isSave) { Assert-GameClosed }
  # Read access only; deny writers during the copy. Never change source attributes or timestamps.
  $inputStream = [IO.File]::Open($file.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
  try {
    $entry = $archive.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
    if ($isSave) { $entry.ExternalAttributes = 1 } # DOS read-only bit on the archived copy only.
    $outputStream = $entry.Open()
    try {
      $buffer = New-Object byte[] (1MB)
      while (($count = $inputStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
        $outputStream.Write($buffer, 0, $count)
        Report-Progress $name $inputStream.Position $inputStream.Length $timer
      }
    } finally { $outputStream.Dispose() }
  } finally { $inputStream.Dispose() }
  $contents.Add("$name ($($file.Length) bytes; binary, unredacted)")
}

$latestFiles = @()
$latestFolder = ''
if ($IncludeLatestSave) {
  Assert-GameClosed
  if (-not $PlayerProfiles -and $env:LOCALAPPDATA) {
    $PlayerProfiles = Join-Path $env:LOCALAPPDATA "Larian Studios\Baldur's Gate 3\PlayerProfiles"
  }
  $latest = Find-LatestSave $PlayerProfiles
  if (-not $latest) {
    $notes.Add("Latest save: none found in the Baldur's Gate 3 Story save folders; not included.")
  } else {
    $latestFolder = $latest.Directory.Name
    $latestFiles = @($latest)
    $thumb = Join-Path $latest.DirectoryName ([IO.Path]::GetFileNameWithoutExtension($latest.Name) + '.webp')
    if (Test-Path -LiteralPath $thumb -PathType Leaf) {
      $thumbItem = Get-Item -LiteralPath $thumb -Force
      if (-not (Test-Link $thumbItem)) { $latestFiles += $thumbItem }
    }
    $latestBytes = [long]($latestFiles | Measure-Object -Property Length -Sum).Sum
    if ($latestBytes -gt $MaxSaveBytes) {
      $notes.Add(("Latest save {0}: {1:N1} MB, over the {2:N0} MB report limit; not included. Send it separately if asked." -f
        $latestFolder, ($latestBytes / 1MB), ($MaxSaveBytes / 1MB)))
      $latestFiles = @()
    } else {
      Write-Host ("Latest save: {0} ({1:N0} bytes, {2:N2} MB). Originals are opened read-only." -f
        $latestFolder, $latestBytes, ($latestBytes / 1MB))
    }
  }
}

$zip = Join-Path $root ("BG3RTX-diagnostics-{0}-{1}.zip" -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), ([guid]::NewGuid().ToString('N').Substring(0, 8)))
$stream = $null
$archive = $null
$created = $false
try {
  $stream = [IO.File]::Open($zip, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
  $created = $true
  $archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create, $true)
  # Only install-root logs/settings and exact crash folders; never recurse or scan for saves.
  $logs = @(Get-ChildItem -LiteralPath $root -File -Force | Where-Object { $_.Name -match '^bg3rtx(?:\.[a-zA-Z0-9_-]+)*\.log(?:\.\d+)?$' } | Sort-Object Name)
  $index = 0
  foreach ($log in $logs) {
    $index++
    $name = if ($log.Name -in @('bg3rtx.log', 'bg3rtx.prev.log')) { $log.Name } else { "log-{0:D3}.log" -f $index }
    Add-Log $log "logs/$name"
  }
  if (-not $logs.Count) { $notes.Add('No bg3rtx logs found.') }
  foreach ($name in @('bg3rtx.ini', 'bg3rtx_user.ini', 'VERSION.txt')) {
    $path = Join-Path $root $name
    if (Test-Path -LiteralPath $path -PathType Leaf) {
      Assert-NoLinks $path
      Add-Text "settings/$name" ([IO.File]::ReadAllText($path))
    } else { $notes.Add("Missing: $name") }
  }
  Add-Text 'settings/effective-log.txt' $cfg.ToString()
  Add-Text 'system/device-log.txt' $device.ToString()
  $system = "OS version: $([Environment]::OSVersion.VersionString)`r`n"
  try {
    $gpu = Get-CimInstance Win32_VideoController -OperationTimeoutSec 10 |
      Select-Object Name, DriverVersion, DriverDate, AdapterCompatibility, VideoProcessor
    $system += ($gpu | Format-List | Out-String -Width 240)
  } catch { $system += "GPU query unavailable (Win32_VideoController).`r`n" }
  Add-Text 'system/system.txt' $system
  $dumpIndex = 0
  foreach ($rel in @('captures\crash', 'captures\aftermath')) {
    $dir = Join-Path $root $rel
    if (Test-Path -LiteralPath $dir -PathType Container) {
      Assert-NoLinks $dir
      foreach ($dump in (Get-ChildItem -LiteralPath $dir -File -Force | Where-Object { $_.Extension -in @('.dmp', '.nv-gpudmp') } | Sort-Object Name)) {
        $dumpIndex++
        Add-Binary $dump ("crash/dump-{0:D3}{1}" -f $dumpIndex, $dump.Extension.ToLowerInvariant())
      }
    }
  }
  if ($save) { Add-Binary $save 'save/selected.lsv' $true }
  foreach ($file in $latestFiles) { Add-Binary $file ("saves/{0}/{1}" -f $latestFolder, $file.Name) $true }
  $privacy = @(
    'BG3RTX diagnostic package. Nothing was uploaded. Crash dumps were not enabled.',
    'Text: Windows profile names and obvious credentials redacted; review before sharing.',
    'Binary crash dumps and the optional selected save are unchanged and may contain personal data.',
    'No save included unless explicitly selected (a chosen save, or your latest save when you answered Y).',
    'Original files are never modified.',
    '', 'Contents:', 'CONTENTS.txt') + @($contents) + @('', 'Notes:') + @($notes)
  Add-Text 'CONTENTS.txt' ($privacy -join "`r`n")
} catch {
  if ($archive) { $archive.Dispose(); $archive = $null }
  if ($stream) { $stream.Dispose(); $stream = $null }
  # Only remove the exact new zip created by this invocation, never a directory.
  if ($created -and (Test-Path -LiteralPath $zip)) { Remove-Item -LiteralPath $zip -Force }
  throw
} finally {
  if ($archive) { $archive.Dispose() }
  if ($stream) { $stream.Dispose() }
}
Write-Host 'Package contents:'
foreach ($item in $contents) { Write-Host "  $item" }
foreach ($note in $notes) { Write-Host "  $note" }
Write-Host ("Created {0} ({1:N0} bytes, {2:N2} MB)" -f $zip, (Get-Item -LiteralPath $zip).Length, ((Get-Item -LiteralPath $zip).Length / 1MB))
