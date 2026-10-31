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
  public static void Write(string text){
   text=Normalize(text);
   IntPtr h=CreateFile("CONIN$",0x40000000,3,IntPtr.Zero,3,0,IntPtr.Zero);
   if(h==new IntPtr(-1))throw new Win32Exception();
   try{
    // One character down/up; CR finishes native std::getline. No global key APIs.
    foreach(char c in text){
     var records=new Input[2];for(int i=0;i<2;i++){records[i].Type=1;records[i].Event=new Key{Down=i==0?1:0,Repeat=1,Vk=(ushort)(c=='\r'?13:0),Character=c};}
     uint written;if(!WriteConsoleInput(h,records,2,out written)||written!=2)throw new Win32Exception();
    }
   }finally{CloseHandle(h);}
  }
  public static string Tail(){
   IntPtr h=CreateFile("CONOUT$",0x80000000,3,IntPtr.Zero,3,0,IntPtr.Zero);
   if(h==new IntPtr(-1))throw new Win32Exception();
   try {Info info;if(!GetConsoleScreenBufferInfo(h,out info))throw new Win32Exception();
    int end=info.Cursor.Y*info.Size.X+info.Cursor.X,count=Math.Min(4096,end),start=end-count;
    var text=new StringBuilder(count+1);uint read;
    if(!ReadConsoleOutputCharacter(h,text,(uint)count,new Coord{X=(short)(start%info.Size.X),Y=(short)(start/info.Size.X)},out read))throw new Win32Exception();
    return text.ToString();
   }finally{CloseHandle(h);}
  }
 }
}
