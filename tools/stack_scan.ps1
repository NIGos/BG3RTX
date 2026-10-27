# For every thread of a process: read the top of its stack and list the
# return-address candidates that fall inside the given modules (default: our
# layer, the hot core and the NVIDIA Vulkan driver). A hung thread whose top
# frames are in VkLayer_bg3rtx.dll / bg3rtx_core is blocked in our code.
# Heuristic (no symbols, no unwinding), good enough to find who waits where.
#   .\tools\stack_scan.ps1 -ProcessId <pid> [-Bytes 32768] [-Modules VkLayer_bg3rtx.dll,bg3rtx_core]
param([int]$ProcessId, [int]$Bytes = 32768, [string[]]$Modules = @("VkLayer_bg3rtx.dll", "bg3rtx_core", "nvoglv64.dll", "sl.", "nvngx"))
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public static class SS {
  [DllImport("kernel32.dll")] public static extern IntPtr OpenThread(int access, bool inherit, int tid);
  [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int access, bool inherit, int pid);
  [DllImport("kernel32.dll")] public static extern int SuspendThread(IntPtr h);
  [DllImport("kernel32.dll")] public static extern int ResumeThread(IntPtr h);
  [DllImport("kernel32.dll")] public static extern bool GetThreadContext(IntPtr h, IntPtr ctx);
  [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, IntPtr n, out IntPtr r);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  public static ulong[] Regs(int tid) {   // rip, rsp
    IntPtr h = OpenThread(0x0002 | 0x0008 | 0x0040, false, tid);
    if (h == IntPtr.Zero) return null;
    IntPtr raw = Marshal.AllocHGlobal(1232 + 16);
    IntPtr ctx = new IntPtr((raw.ToInt64() + 15) & ~15L);
    Marshal.WriteInt32(ctx, 0x30, 0x100001);
    ulong[] r = null;
    SuspendThread(h);
    if (GetThreadContext(h, ctx)) r = new ulong[] { (ulong)Marshal.ReadInt64(ctx, 0xF8), (ulong)Marshal.ReadInt64(ctx, 0x98) };
    ResumeThread(h);
    Marshal.FreeHGlobal(raw); CloseHandle(h);
    return r;
  }
  public static byte[] Read(int pid, ulong addr, int n) {
    IntPtr p = OpenProcess(0x0010, false, pid);   // VM_READ
    byte[] b = new byte[n]; IntPtr got;
    bool ok = ReadProcessMemory(p, new IntPtr((long)addr), b, new IntPtr(n), out got);
    CloseHandle(p);
    if (!ok) { int m = (int)got.ToInt64(); if (m <= 0) return null; Array.Resize(ref b, m); }
    return b;
  }
}
"@
$p = Get-Process -Id $ProcessId
$mods = $p.Modules | ForEach-Object { [pscustomobject]@{ Name = $_.ModuleName; Base = [uint64]$_.BaseAddress.ToInt64(); End = [uint64]$_.BaseAddress.ToInt64() + [uint64]$_.ModuleMemorySize } }
$watch = $mods | Where-Object { $n = $_.Name; $Modules | Where-Object { $n -like "$_*" } }
function Where-Addr($a) { $mods | Where-Object { $a -ge $_.Base -and $a -lt $_.End } | Select-Object -First 1 }
foreach ($t in $p.Threads) {
  $r = [SS]::Regs($t.Id)
  if (-not $r) { continue }
  $top = Where-Addr $r[0]
  $stack = [SS]::Read($ProcessId, $r[1], $Bytes)
  if (-not $stack) { continue }
  $found = @()
  for ($o = 0; $o -le $stack.Length - 8; $o += 8) {
    $v = [BitConverter]::ToUInt64($stack, $o)
    $m = $watch | Where-Object { $v -ge $_.Base -and $v -lt $_.End } | Select-Object -First 1
    if ($m) { $found += "{0}+0x{1:x}" -f $m.Name, ($v - $m.Base) }
  }
  if ($found.Count) {
    $rip = if ($top) { "{0}+0x{1:x}" -f $top.Name, ($r[0] - $top.Base) } else { "0x{0:x}" -f $r[0] }
    "thread $($t.Id) [$($t.ThreadState)/$($t.WaitReason), cpu $([int]$t.TotalProcessorTime.TotalSeconds)s] at $rip"
    $found | Select-Object -First 8 | ForEach-Object { "    $_" }
  }
}
