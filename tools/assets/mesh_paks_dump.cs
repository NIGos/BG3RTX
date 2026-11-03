// Native GR2P v2 writer. No conversion, welding, sorting or default colours.
using System;
using System.IO;
using System.Text;
using LSLib.Granny.GR2;
using LSLib.Granny.Model;
public static class MeshPaksDump {
    static void Str(BinaryWriter w, string s) {
        var b = Encoding.UTF8.GetBytes(s); w.Write((uint)b.Length); w.Write(b);
    }
    public static void Export(string source, string output) {
        var root = new Root();
        using (var f = File.OpenRead(source))
        using (var r = new GR2Reader(f)) { r.Read(root); }
        using (var w = new BinaryWriter(new FileStream(output, FileMode.CreateNew))) {
            w.Write(Encoding.ASCII.GetBytes("GR2P")); w.Write((uint)2); w.Write((uint)root.Meshes.Count);
            foreach (var m in root.Meshes) {
                var verts = m.PrimaryVertexData.Vertices; var topo = m.PrimaryTopology;
                bool wide = topo.Indices != null && topo.Indices.Count > 0;
                int count = wide ? topo.Indices.Count : topo.Indices16.Count;
                if (topo.Groups == null || topo.Groups.Count == 0) throw new Exception("No triangle groups: " + m.Name);
                Str(w, m.Name); w.Write((uint)verts.Count); w.Write((uint)count); w.Write((uint)topo.Groups.Count);
                uint layout = 0;
                if (verts.Count > 0 && verts[0].Format.ColorMaps == 1 &&
                    (verts[0].Format.ColorMapType.ToString() == "Float4" || verts[0].Format.ColorMapType.ToString() == "Byte4")) layout = 1;
                foreach (var v in verts) {
                    if (v.Format.ColorMaps != verts[0].Format.ColorMaps || v.Format.ColorMapType != verts[0].Format.ColorMapType) layout = 0;
                }
                w.Write(layout);
                foreach (var v in verts) {
                    w.Write((float)v.Position.X); w.Write((float)v.Position.Y); w.Write((float)v.Position.Z);
                    w.Write((float)v.TextureCoordinates0.X); w.Write((float)v.TextureCoordinates0.Y);
                }
                if (layout == 1) foreach (var v in verts) {
                    w.Write((float)v.Color0.X); w.Write((float)v.Color0.Y); w.Write((float)v.Color0.Z); w.Write((float)v.Color0.W);
                }
                if (wide) { foreach (var i in topo.Indices) w.Write((uint)i); }
                else { foreach (var i in topo.Indices16) w.Write((uint)i); }
                foreach (var g in topo.Groups) {
                    w.Write((uint)g.MaterialIndex); w.Write((uint)g.TriFirst); w.Write((uint)g.TriCount);
                }
            }
        }
    }
}
