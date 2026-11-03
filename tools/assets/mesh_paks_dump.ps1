# LSLib v1.20.4 native GR2 reader; never exports/reorders through glTF.
# GR2P v1: mesh name, nv, ni, ng, vertices (position3, UV2 float32),
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
  $o.Write([Text.Encoding]::ASCII.GetBytes('GR2P')); $o.Write([uint32]1); $o.Write([uint32]$root.Meshes.Count)
  foreach ($m in $root.Meshes) {
    $verts = $m.PrimaryVertexData.Vertices; $topo = $m.PrimaryTopology
    $idx = if ($topo.Indices -and $topo.Indices.Count) { $topo.Indices } else { $topo.Indices16 }
    if (!$topo.Groups -or !$topo.Groups.Count) { throw "No triangle groups: $($m.Name)" }
    Str $m.Name; $o.Write([uint32]$verts.Count); $o.Write([uint32]$idx.Count); $o.Write([uint32]$topo.Groups.Count)
    foreach ($v in $verts) {
      if ($null -eq $v.Position -or $null -eq $v.TextureCoordinates0) { throw "Missing position/UV: $($m.Name)" }
      $o.Write([single]$v.Position.X); $o.Write([single]$v.Position.Y); $o.Write([single]$v.Position.Z)
      $o.Write([single]$v.TextureCoordinates0.X); $o.Write([single]$v.TextureCoordinates0.Y)
    }
    foreach ($i in $idx) { $o.Write([uint32]$i) }
    foreach ($g in $topo.Groups) {
      if ($null -eq $g.MaterialIndex -or $null -eq $g.TriFirst -or $null -eq $g.TriCount) { throw 'Unsupported triangle group layout' }
      $o.Write([uint32]$g.MaterialIndex); $o.Write([uint32]$g.TriFirst); $o.Write([uint32]$g.TriCount)
    }
  }
} finally { $o.Dispose() }
