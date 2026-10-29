# Keeps the games of our test runs out of the user's way while they use the PC.
#
#   .\scripts\game-minimizer.ps1 -WhilePid <pid>    # run until that process exits
#
# Only games whose PID is in a run file of ours (captures\game*.pid in the repository
# or a worktree) are touched, never a BG3 the user started.
#
# Minimising does not work: BG3 restores its window within a second (layer/background.cpp
# reports it as never minimised). Instead the window stays "visible" to the game but is made
# fully transparent (layered, alpha 0), click-through (WS_EX_TRANSPARENT), out of Alt-Tab
# (WS_EX_TOOLWINDOW) and moved to the bottom of the z-order without activation. Background
# runs present headless, so the window's content is never needed.
param([int]$WhilePid = 0)

$root = Split-Path $PSScriptRoot -Parent
Add-Type -Namespace BG3RTX -Name HideWin -MemberDefinition @"
[DllImport("user32.dll")] public static extern int GetWindowLong(System.IntPtr h, int i);
[DllImport("user32.dll")] public static extern int SetWindowLong(System.IntPtr h, int i, int v);
[DllImport("user32.dll")] public static extern bool SetLayeredWindowAttributes(System.IntPtr h, uint key, byte alpha, uint flags);
[DllImport("user32.dll")] public static extern bool SetWindowPos(System.IntPtr h, System.IntPtr after, int x, int y, int cx, int cy, uint flags);
"@ -ErrorAction SilentlyContinue

$GWL_EXSTYLE = -20
$WANT = 0x00080000 -bor 0x00000020 -bor 0x00000080   # WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW
$HWND_BOTTOM = [IntPtr]1
$SWP = 0x0001 -bor 0x0002 -bor 0x0010 -bor 0x0200     # NOSIZE | NOMOVE | NOACTIVATE | NOOWNERZORDER

function Our-Pids {
  $dirs = @(Join-Path $root "captures") +
          @(Get-ChildItem -LiteralPath (Join-Path $root ".claude\worktrees") -Directory -Force -ErrorAction SilentlyContinue |
            ForEach-Object { Join-Path $_.FullName "captures" })
  $files = foreach ($d in $dirs) { Get-ChildItem -LiteralPath $d -Filter "game*.pid" -Force -ErrorAction SilentlyContinue }
  foreach ($f in $files) {
    try {
      $run = Get-Content $f.FullName -Raw | ConvertFrom-Json
      # Real-present timing/quality jobs must stay visible. In particular,
      # do not apply transparency or z-order changes to a foreground A/B.
      if (-not $run.visible_present) { [int]$run.pid }
    } catch { }
  }
}

while ($WhilePid -eq 0 -or (Get-Process -Id $WhilePid -ErrorAction SilentlyContinue)) {
  $ours = @(Our-Pids)
  foreach ($g in Get-Process bg3, bg3_dx11 -ErrorAction SilentlyContinue) {
    if ($ours -notcontains $g.Id) { continue }
    $h = $g.MainWindowHandle
    if ($h -eq [IntPtr]::Zero) { continue }
    $ex = [BG3RTX.HideWin]::GetWindowLong($h, $GWL_EXSTYLE)
    if (($ex -band $WANT) -ne $WANT) {
      [void][BG3RTX.HideWin]::SetWindowLong($h, $GWL_EXSTYLE, $ex -bor $WANT)
      [void][BG3RTX.HideWin]::SetLayeredWindowAttributes($h, 0, 0, 2)   # LWA_ALPHA, fully transparent
    }
    [void][BG3RTX.HideWin]::SetWindowPos($h, $HWND_BOTTOM, 0, 0, 0, 0, $SWP)
  }
  Start-Sleep -Seconds 1
}
