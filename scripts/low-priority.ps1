# Dot-source at the top of long offline scripts (build, merge-green): the current PowerShell
# process and every child it starts (cl, ninja, dxc, ctest, python) run at BelowNormal priority,
# so the PC stays usable while they run. Windows makes a child of a BelowNormal process inherit
# the class. Set BG3RTX_NORMAL_PRIORITY=1 to keep the normal priority.
if (-not $env:BG3RTX_NORMAL_PRIORITY) {
  try { (Get-Process -Id $PID).PriorityClass = 'BelowNormal' } catch { }
}
