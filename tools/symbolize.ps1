# Resolves module offsets to function names with dbghelp and the build's PDB.
#   .\tools\symbolize.ps1 -Dll <path\VkLayer_bg3rtx.dll> -Offsets 0x8d6c7,0x8c1e9
param([string]$Dll, [string[]]$Offsets)
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices; using System.Text;
public static class Sym {
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
  public struct SYMBOL_INFO { public uint SizeOfStruct, TypeIndex; public ulong Reserved0, Reserved1; public uint Index, Size;
    public ulong ModBase; public uint Flags; public ulong Value, Address; public uint Register, Scope, Tag, NameLen, MaxNameLen;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 1024)] public string Name; }
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
  public struct LINE64 { public uint SizeOfStruct; public IntPtr Key; public uint LineNumber; public IntPtr FileName; public ulong Address; }
  [DllImport("dbghelp.dll", CharSet = CharSet.Ansi)] public static extern bool SymInitialize(IntPtr p, string path, bool invade);
  [DllImport("dbghelp.dll", CharSet = CharSet.Ansi)] public static extern ulong SymLoadModuleEx(IntPtr p, IntPtr f, string img, string mod, ulong b, uint sz, IntPtr d, uint fl);
  [DllImport("dbghelp.dll", CharSet = CharSet.Ansi)] public static extern bool SymFromAddr(IntPtr p, ulong a, out ulong disp, ref SYMBOL_INFO s);
  [DllImport("dbghelp.dll", CharSet = CharSet.Ansi)] public static extern bool SymGetLineFromAddr64(IntPtr p, ulong a, out uint disp, ref LINE64 l);
  [DllImport("dbghelp.dll")] public static extern uint SymSetOptions(uint o);
}
"@
$h = [IntPtr]42
[void][Sym]::SymSetOptions(0x2 -bor 0x10)   # UNDNAME | LOAD_LINES
[void][Sym]::SymInitialize($h, (Split-Path $Dll), $false)
$base = [uint64]0x10000000
[void][Sym]::SymLoadModuleEx($h, [IntPtr]::Zero, $Dll, $null, $base, 0, [IntPtr]::Zero, 0)
foreach ($o in ($Offsets | ForEach-Object { $_ -split "," } | Where-Object { $_ })) {
  $a = $base + [Convert]::ToUInt64($o.Replace("0x", ""), 16)
  $s = New-Object Sym+SYMBOL_INFO; $s.SizeOfStruct = 88; $s.MaxNameLen = 1024
  $d = [uint64]0
  $name = if ([Sym]::SymFromAddr($h, $a, [ref]$d, [ref]$s)) { "$($s.Name)+0x{0:x}" -f $d } else { "?" }
  $l = New-Object Sym+LINE64; $l.SizeOfStruct = [System.Runtime.InteropServices.Marshal]::SizeOf([type][Sym+LINE64])
  $ld = [uint32]0
  $line = if ([Sym]::SymGetLineFromAddr64($h, $a, [ref]$ld, [ref]$l)) { "$([System.Runtime.InteropServices.Marshal]::PtrToStringAnsi($l.FileName)):$($l.LineNumber)" } else { "" }
  "{0,-10} {1}  {2}" -f $o, $name, $line
}
