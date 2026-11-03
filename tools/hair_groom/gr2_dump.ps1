# Dumps the meshes and bones of one GR2 with LSLib's reader (no glTF export: LSLib v1.20.4's glTF exporter fails
# on some BG3 hair meshes, "Accessor[N] memory: Value[i] is out of bounds"). Native vertex and index order is kept:
# the layer joins captured draws by their index order (common/hair_groom_asset.h).
#   pwsh -NoProfile -File tools\hair_groom\gr2_dump.ps1 -Lslib <LSLib Tools dir with granny2.dll> -Gr2 <in.gr2> -Out <out.bin>
# PowerShell 7 (pwsh): LSLib targets .NET 8. Layout (little endian, tools/hair_groom/mesh.py load_gr2_dump):
#   'GR2D', u32 1, u32 meshes; per mesh: str name, u32 vertices, u32 indices, u32 bindings, bindings x str,
#   vertices x (f32 x3 position, f32 x2 uv0, u8 x4 bone indices, u8 x4 bone weights), indices x u32;
#   u32 bones; per bone: str name, f32 x16 inverse world transform (row-major as LSLib stores it).
#   str = u32 byte length + UTF-8.
param([Parameter(Mandatory)][string]$Lslib, [Parameter(Mandatory)][string]$Gr2, [Parameter(Mandatory)][string]$Out)
$ErrorActionPreference = 'Stop'
[System.Runtime.InteropServices.NativeLibrary]::Load((Join-Path $Lslib 'granny2.dll')) | Out-Null
[Reflection.Assembly]::LoadFrom((Join-Path $Lslib 'LSLib.dll')) | Out-Null
$f = [IO.File]::OpenRead($Gr2)
$reader = [LSLib.Granny.GR2.GR2Reader]::new($f)
$root = [LSLib.Granny.Model.Root]::new()
try { $reader.Read($root) } finally { $reader.Dispose(); $f.Dispose() }
$o = [IO.BinaryWriter]::new([IO.File]::Create($Out))
function Str([string]$s) { $b = [Text.Encoding]::UTF8.GetBytes($s); $o.Write([uint32]$b.Length); $o.Write($b) }
try {
  $o.Write([Text.Encoding]::ASCII.GetBytes('GR2D')); $o.Write([uint32]1); $o.Write([uint32]$root.Meshes.Count)
  foreach ($m in $root.Meshes) {
    $verts = $m.PrimaryVertexData.Vertices
    $topo = $m.PrimaryTopology
    $idx = if ($topo.Indices -and $topo.Indices.Count) { $topo.Indices } else { $topo.Indices16 }
    $bind = @(); if ($m.BoneBindings) { $bind = @($m.BoneBindings | ForEach-Object { $_.BoneName }) }
    Str $m.Name; $o.Write([uint32]$verts.Count); $o.Write([uint32]$idx.Count); $o.Write([uint32]$bind.Count)
    foreach ($b in $bind) { Str $b }
    foreach ($v in $verts) {
      $o.Write([single]$v.Position.X); $o.Write([single]$v.Position.Y); $o.Write([single]$v.Position.Z)
      $o.Write([single]$v.TextureCoordinates0.X); $o.Write([single]$v.TextureCoordinates0.Y)
      $bi = $v.BoneIndices; $bw = $v.BoneWeights
      $o.Write([byte]$bi.A); $o.Write([byte]$bi.B); $o.Write([byte]$bi.C); $o.Write([byte]$bi.D)
      $o.Write([byte]$bw.A); $o.Write([byte]$bw.B); $o.Write([byte]$bw.C); $o.Write([byte]$bw.D)
    }
    foreach ($i in $idx) { $o.Write([uint32]$i) }
  }
  $bones = @(); foreach ($s in $root.Skeletons) { $bones += @($s.Bones) }
  $o.Write([uint32]$bones.Count)
  foreach ($b in $bones) { Str $b.Name; foreach ($x in $b.InverseWorldTransform) { $o.Write([single]$x) } }
} finally { $o.Dispose() }
