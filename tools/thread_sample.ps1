# Samples the instruction pointer of one thread (or the busiest thread) of a
# process and reports which module and offset it is executing in. For
# diagnosing hangs: a busy thread spinning in our layer shows up as
# VkLayer_bg3rtx.dll / bg3rtx_core.*.dll.
#   .\tools\thread_sample.ps1 -ProcessId <pid> [-ThreadId <tid>] [-Samples 40]
param([int]$ProcessId, [int]$ThreadId = 0, [int]$Samples = 40)
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public static class TS {
  [DllImport("kernel32.dll")] public static extern IntPtr OpenThread(int access, bool inherit, int tid);
  [DllImport("kernel32.dll")] public static extern int SuspendThread(IntPtr h);
  [DllImport("kernel32.dll")] public static extern int ResumeThread(IntPtr h);
  [DllImport("kernel32.dll")] public static extern bool GetThreadContext(IntPtr h, IntPtr ctx);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  public static ulong Rip(int tid) {
    IntPtr h = OpenThread(0x0002 | 0x0008 | 0x0040, false, tid);   // SUSPEND_RESUME | GET_CONTEXT | QUERY_INFORMATION
    if (h == IntPtr.Zero) return 0;
    IntPtr raw = Marshal.AllocHGlobal(1232 + 16);
    IntPtr ctx = new IntPtr((raw.ToInt64() + 15) & ~15L);   // CONTEXT must be 16-byte aligned
    Marshal.WriteInt32(ctx, 0x30, 0x100001);                 // ContextFlags = CONTEXT_CONTROL (AMD64)
    ulong rip = 0;
    SuspendThread(h);
    if (GetThreadContext(h, ctx)) rip = (ulong)Marshal.ReadInt64(ctx, 0xF8);
    ResumeThread(h);
    Marshal.FreeHGlobal(raw); CloseHandle(h);
    return rip;
  }
}
"@
$p = Get-Process -Id $ProcessId
if (-not $ThreadId) { $ThreadId = ($p.Threads | Sort-Object { $_.TotalProcessorTime } -Descending | Select-Object -First 1).Id }
$mods = $p.Modules | ForEach-Object { [pscustomobject]@{ Name = $_.ModuleName; Base = [uint64]$_.BaseAddress.ToInt64(); End = [uint64]$_.BaseAddress.ToInt64() + [uint64]$_.ModuleMemorySize } }
$hits = @{}
for ($i = 0; $i -lt $Samples; ++$i) {
  $rip = [TS]::Rip($ThreadId)
  $m = $mods | Where-Object { $rip -ge $_.Base -and $rip -lt $_.End } | Select-Object -First 1
  $key = if ($m) { "{0}+0x{1:x}" -f $m.Name, ($rip - $m.Base) } else { "0x{0:x}" -f $rip }
  $hits[$key] = 1 + [int]$hits[$key]
  Start-Sleep -Milliseconds 25
}
"thread $ThreadId of pid ${ProcessId}:"
$hits.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 12 | ForEach-Object { "{0,4}  {1}" -f $_.Value, $_.Key }
