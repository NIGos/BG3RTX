# LSLib v1.20.4 native GR2 reader; never exports/reorders through glTF.
# GR2P v2: mesh name, nv, ni, ng, COLOR_0 layout, position3/UV2 vertices,
# optional COLOR_0 RGBA float32 vertices (native Float4 or decoded normalized Byte4),
# native uint32 indices, groups (material index, first triangle, triangle count).
param([Parameter(Mandatory)][string]$Lslib, [Parameter(Mandatory)][string]$Gr2, [Parameter(Mandatory)][string]$Out)
$ErrorActionPreference = 'Stop'
[System.Runtime.InteropServices.NativeLibrary]::Load((Join-Path $Lslib 'granny2.dll')) | Out-Null
[Reflection.Assembly]::LoadFrom((Join-Path $Lslib 'LSLib.dll')) | Out-Null
$f = [IO.File]::OpenRead($Gr2)
$reader = [LSLib.Granny.GR2.GR2Reader]::new($f)
$root = [LSLib.Granny.Model.Root]::new()
try { $reader.Read($root) } finally { $reader.Dispose(); $f.Dispose() }
$o = [IO.BinaryWriter]::new([IO.File]::Open($Out, [IO.FileMode]::CreateNew))
function Str([string]$s) { $b = [Text.Encoding]::UTF8.GetBytes($s); $o.Write([uint32]$b.Length); $o.Write($b) }
try {
  $o.Write([Text.Encoding]::ASCII.GetBytes('GR2P')); $o.Write([uint32]2); $o.Write([uint32]$root.Meshes.Count)
  foreach ($m in $root.Meshes) {
    $verts = $m.PrimaryVertexData.Vertices; $topo = $m.PrimaryTopology
    $idx = if ($topo.Indices -and $topo.Indices.Count) { $topo.Indices } else { $topo.Indices16 }
    if (!$topo.Groups -or !$topo.Groups.Count) { throw "No triangle groups: $($m.Name)" }
    Str $m.Name; $o.Write([uint32]$verts.Count); $o.Write([uint32]$idx.Count); $o.Write([uint32]$topo.Groups.Count)
    # Color0 exists on every LSLib Vertex, including formats without colors.
    # Trust only the native descriptor, never the default Vector4 field.
    $layout = 0
    if ($verts.Count -and $verts[0].Format.ColorMaps -eq 1 -and
        [string]$verts[0].Format.ColorMapType -in @('Float4', 'Byte4')) { $layout = 1 }
    foreach ($v in $verts) {
      if ($v.Format.ColorMaps -ne $verts[0].Format.ColorMaps -or
          $v.Format.ColorMapType -ne $verts[0].Format.ColorMapType) { $layout = 0 }
    }
    $o.Write([uint32]$layout)
    foreach ($v in $verts) {
      if ($null -eq $v.Position -or $null -eq $v.TextureCoordinates0) { throw "Missing position/UV: $($m.Name)" }
      $o.Write([single]$v.Position.X); $o.Write([single]$v.Position.Y); $o.Write([single]$v.Position.Z)
      $o.Write([single]$v.TextureCoordinates0.X); $o.Write([single]$v.TextureCoordinates0.Y)
    }
    if ($layout -eq 1) {
      foreach ($v in $verts) {
        $o.Write([single]$v.Color0.X); $o.Write([single]$v.Color0.Y)
        $o.Write([single]$v.Color0.Z); $o.Write([single]$v.Color0.W)
      }
    }
    foreach ($i in $idx) { $o.Write([uint32]$i) }
    foreach ($g in $topo.Groups) {
      if ($null -eq $g.MaterialIndex -or $null -eq $g.TriFirst -or $null -eq $g.TriCount) { throw 'Unsupported triangle group layout' }
      $o.Write([uint32]$g.MaterialIndex); $o.Write([uint32]$g.TriFirst); $o.Write([uint32]$g.TriCount)
    }
  }
} finally { $o.Dispose() }
