// Fixed SE diagnostic stdin transport. No window activation or game launch.
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
namespace BG3RTX {
 public static class SeConsoleProbe {
  [StructLayout(LayoutKind.Sequential)] struct Coord {public short X,Y;}
  [StructLayout(LayoutKind.Sequential)] struct SmallRect {public short L,T,R,B;}
  [StructLayout(LayoutKind.Sequential)] struct Info {public Coord Size,Cursor;public ushort Attributes;public SmallRect Window;public Coord Maximum;}
  [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct Key {public int Down;public ushort Repeat,Vk,Scan;public char Character;public uint State;}
  [StructLayout(LayoutKind.Explicit,Size=20)] struct Input { [FieldOffset(0)]public ushort Type;[FieldOffset(4)]public Key Event; }
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool FreeConsole();
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool AttachConsole(uint pid);
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern uint GetConsoleTitle(StringBuilder title,uint size);
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFile(string path,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool CloseHandle(IntPtr handle);
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool WriteConsoleInput(IntPtr handle,Input[] data,uint count,out uint written);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetConsoleScreenBufferInfo(IntPtr handle,out Info info);
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool ReadConsoleOutputCharacter(IntPtr handle,StringBuilder text,uint count,Coord start,out uint read);
  public static bool Attach(int pid) {
   FreeConsole();if(!AttachConsole((uint)pid))return false;
   var title=new StringBuilder(256);GetConsoleTitle(title,256);
   if(title.ToString()!="BG3 Script Extender Debug Console"){FreeConsole();return false;}return true;
  }
  public static void Detach(){FreeConsole();}
  public static int RecordSize(){return Marshal.SizeOf(typeof(Input));}
  public static string Normalize(string text){
   if(text==null||text.Length>16384)throw new ArgumentException("Fixed probe exceeds console input budget");
   return text.Replace("\r","").Replace("\n","\r");
  }
  public static string SingleLine(string source){
   if(source==null)throw new ArgumentException("Missing fixed Lua payload");
   var lines=source.Replace("\r","").Split('\n');var result=new StringBuilder();
   foreach(string line in lines){
    if(line.TrimStart().StartsWith("--"))continue;
    if(line.Contains("--"))throw new ArgumentException("Inline Lua comments require explicit review");
    if(result.Length>0)result.Append(' ');result.Append(line.Trim());
   }
   string text=result.ToString().Trim();
   if(text.Length==0||text.Length>8190)throw new ArgumentException("Fixed single-line payload outside input budget");
   return text;
  }
  public static int PlannedEvents(string text){return checked(Normalize(text).Length*2);}
  public static void Write(string text){
   text=Normalize(text);
   IntPtr h=CreateFile("CONIN$",0x40000000,3,IntPtr.Zero,3,0,IntPtr.Zero);
   if(h==new IntPtr(-1))throw new Win32Exception();
   try{
    // Exactly one native write per checked command. Any partial result is an
    // uncertain transport: caller aborts the owned run, never attempts repair.
    var records=new Input[text.Length*2];
    for(int j=0;j<text.Length;j++)for(int i=0;i<2;i++){
     char c=text[j];records[j*2+i].Type=1;records[j*2+i].Event=new Key{Down=i==0?1:0,Repeat=1,Vk=(ushort)(c=='\r'?13:0),Character=c};
    }
    uint written;if(!WriteConsoleInput(h,records,(uint)records.Length,out written)||written!=records.Length)throw new Win32Exception("SE console write incomplete; abort owned run");
   }finally{CloseHandle(h);}
  }
  public sealed class Snapshot {
   public int Width,Height,X,Y;public string Line;
  }
  public static string FreshPrompt(Snapshot before,Snapshot after){
   if(before==null||after==null||before.Width<=0||before.Height<=0||before.Width!=after.Width||before.Height!=after.Height)return null;
   if(before.X<0||before.X>=before.Width||before.Y<0||before.Y>=before.Height||after.X<0||after.X>=after.Width||after.Y<0||after.Y>=after.Height)return null;
   long old=(long)before.Y*before.Width+before.X,row=(long)after.Y*after.Width,end=row+after.X;
   // Prompt must begin strictly in the newly produced region. Scroll/resize
   // or an old same-row prompt cannot prove a new command completion.
   if(row<=old||end<=old||after.Line==null||after.Line.Length!=after.X)return null;
   if(after.Line=="S >> ")return "S";if(after.Line=="C >> ")return "C";return null;
  }
  public static Snapshot Capture(){
   IntPtr h=CreateFile("CONOUT$",0x80000000,3,IntPtr.Zero,3,0,IntPtr.Zero);
   if(h==new IntPtr(-1))throw new Win32Exception();
   try {Info info,last;if(!GetConsoleScreenBufferInfo(h,out info))throw new Win32Exception();
    var text=new StringBuilder(info.Cursor.X+1);uint read=0;
    if(info.Cursor.X>0&&(!ReadConsoleOutputCharacter(h,text,(uint)info.Cursor.X,new Coord{X=0,Y=info.Cursor.Y},out read)||read!=info.Cursor.X))throw new Win32Exception();
    if(!GetConsoleScreenBufferInfo(h,out last)||last.Size.X!=info.Size.X||last.Size.Y!=info.Size.Y||last.Cursor.X!=info.Cursor.X||last.Cursor.Y!=info.Cursor.Y)throw new InvalidOperationException("Console changed during snapshot");
    return new Snapshot{Width=info.Size.X,Height=info.Size.Y,X=info.Cursor.X,Y=info.Cursor.Y,Line=text.ToString()};
   }finally{CloseHandle(h);}
  }
 }
}
