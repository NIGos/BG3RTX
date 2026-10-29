// Loaded by PowerShell only for the opt-in visible_unfocused queue mode.
// No code runs merely by compiling this type. All window calls target the owned game.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

namespace BG3RTX {
  public static class UnfocusedWindow {
    public const int ShowNoActivate = 4;
    public const uint NoActivatePositionFlags = 0x1 | 0x2 | 0x10 | 0x40 | 0x200;
    [StructLayout(LayoutKind.Sequential)] public struct Rect {
      public int Left, Top, Right, Bottom;
      public Rect(int l,int t,int r,int b) { Left=l; Top=t; Right=r; Bottom=b; }
      public long Area { get { return Math.Max(0L,(long)Right-Left)*Math.Max(0L,(long)Bottom-Top); } }
    }
    [StructLayout(LayoutKind.Sequential)] struct Point { public int X,Y; }
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct Startup {
      public int cb; public string reserved,desktop,title;
      public int x,y,cx,cy,xChars,yChars,fill; public uint flags;
      public short show, reservedSize; public IntPtr reservedData,input,output,error;
    }
    [StructLayout(LayoutKind.Sequential)] struct ProcessInfo { public IntPtr process,thread; public uint pid,tid; }
    delegate bool EnumProc(IntPtr h,IntPtr state);
    delegate bool MonitorProc(IntPtr h,IntPtr dc,ref Rect r,IntPtr state);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern bool CreateProcess(string app,StringBuilder command,IntPtr pa,IntPtr ta,bool inherit,uint flags,IntPtr env,string cwd,ref Startup startup,out ProcessInfo pi);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h,int how);
    [DllImport("user32.dll",SetLastError=true)] static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int cx,int cy,uint flags);
    [DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")] static extern IntPtr GetStyle(IntPtr h,int index);
    [DllImport("user32.dll",EntryPoint="SetWindowLongPtrW",SetLastError=true)] static extern IntPtr SetStyle(IntPtr h,int index,IntPtr value);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h,out Rect r);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h,out Rect r);
    [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr h,ref Point p);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback,IntPtr state);
    [DllImport("user32.dll")] static extern bool EnumDisplayMonitors(IntPtr dc,IntPtr clip,MonitorProc callback,IntPtr state);
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr h,int attribute,out int value,int bytes);

    public static Process Start(string exe,string arguments,string cwd) {
      Startup s=new Startup(); s.cb=Marshal.SizeOf(typeof(Startup));
      s.flags=1; s.show=ShowNoActivate; // STARTF_USESHOWWINDOW, first show without activation.
      ProcessInfo p;
      if(!CreateProcess(exe,new StringBuilder("\""+exe+"\" "+arguments),IntPtr.Zero,IntPtr.Zero,false,0,IntPtr.Zero,cwd,ref s,out p))
        throw new Win32Exception(Marshal.GetLastWin32Error());
      try { return Process.GetProcessById((int)p.pid); }
      finally { CloseHandle(p.thread); CloseHandle(p.process); }
    }

    public static uint ForegroundProcess() {
      uint pid; GetWindowThreadProcessId(GetForegroundWindow(),out pid); return pid;
    }

    public static void ShowBehind(IntPtr h) {
      // WS_EX_NOACTIVATE; HWND_BOTTOM also removes topmost status. Never activate
      // the game or restore another app's focus with SetForegroundWindow.
      long style=GetStyle(h,-20).ToInt64();
      SetStyle(h,-20,new IntPtr((style | 0x08000000L) & ~0x8L));
      if(!IsWindowVisible(h) || IsIconic(h)) ShowWindow(h,ShowNoActivate);
      if(!SetWindowPos(h,new IntPtr(1),0,0,0,0,NoActivatePositionFlags))
        throw new Win32Exception(Marshal.GetLastWin32Error());
      long applied=GetStyle(h,-20).ToInt64();
      if((applied & 0x08000008L)!=0x08000000L) throw new InvalidOperationException("No-activate/non-topmost window style was not applied");
    }

    // Exact rectangular subtraction; deliberately conservative for rounded,
    // translucent or shaped windows. A false occlusion stops, never passes a run.
    public static List<Rect> Subtract(Rect r,Rect cover) {
      var output=new List<Rect>();
      int l=Math.Max(r.Left,cover.Left),t=Math.Max(r.Top,cover.Top);
      int right=Math.Min(r.Right,cover.Right),bottom=Math.Min(r.Bottom,cover.Bottom);
      if(l>=right || t>=bottom) { if(r.Area>0) output.Add(r); return output; }
      Rect[] pieces={new Rect(r.Left,r.Top,r.Right,t),new Rect(r.Left,bottom,r.Right,r.Bottom),
                     new Rect(r.Left,t,l,bottom),new Rect(right,t,r.Right,bottom)};
      foreach(var piece in pieces) if(piece.Area>0) output.Add(piece);
      return output;
    }
    public static long RemainingArea(Rect r,Rect[] covers) {
      var pieces=new List<Rect>(); if(r.Area>0) pieces.Add(r);
      foreach(var cover in covers) {
        var next=new List<Rect>(); foreach(var piece in pieces) next.AddRange(Subtract(piece,cover)); pieces=next;
      }
      long area=0; foreach(var piece in pieces) area+=piece.Area; return area;
    }

    public static long VisibleClientPixels(IntPtr h) {
      if(!IsWindowVisible(h) || IsIconic(h)) return 0;
      Rect r; Point origin=new Point();
      if(!GetClientRect(h,out r) || !ClientToScreen(h,ref origin)) return 0;
      r=new Rect(r.Left+origin.X,r.Top+origin.Y,r.Right+origin.X,r.Bottom+origin.Y);
      var covers=new List<Rect>(); bool found=false;
      EnumWindows(delegate(IntPtr other,IntPtr unused) {
        if(other==h) { found=true; return false; }
        int cloaked; Rect cover;
        if(IsWindowVisible(other) && !IsIconic(other) &&
           !(DwmGetWindowAttribute(other,14,out cloaked,4)==0 && cloaked!=0) && GetWindowRect(other,out cover)) covers.Add(cover);
        return true;
      },IntPtr.Zero);
      if(!found) return 0;
      long visible=0;
      // Clip to real monitors, not their bounding box (which includes gaps).
      EnumDisplayMonitors(IntPtr.Zero,IntPtr.Zero,delegate(IntPtr monitor,IntPtr dc,ref Rect bounds,IntPtr unused) {
        Rect clipped=new Rect(Math.Max(r.Left,bounds.Left),Math.Max(r.Top,bounds.Top),Math.Min(r.Right,bounds.Right),Math.Min(r.Bottom,bounds.Bottom));
        visible+=RemainingArea(clipped,covers.ToArray()); return true;
      },IntPtr.Zero);
      return visible;
    }
  }
}
