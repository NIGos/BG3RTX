# LSLib bridge for tools/assets/vt_alpha_masks.py (pwsh 7: LSLib 1.20 is .NET 8). Offline only.
#   pwsh -NoProfile -File tools/assets/vt_alpha.ps1 -Tools <lslib Tools dir> -Request <json> -Output <json>
# Request: {"mode":"resolve"|"extract", "gts":<path>, "rects":[{"p0":[u,v,du,dv]}] | "textures":[{"name","out","levels"}]}
# resolve: each record uv transform p0 (AlphaRec p0: tile-set uv = frac(uv) * p0.zw + p0.xy) -> its level-0 texel
#          rectangle -> the unique GTS texture with exactly that rectangle, and the page
#          files every level of it needs (GTP names, to be placed next to the GTS before extract).
# extract: every level of each texture, layer 0 (albedo; its .w is the alpha the RT test reads), as DDS
#          <out>.<level>.dds via VirtualTileSet.ExtractTexture.
param([string]$Tools, [Parameter(Mandatory)][string]$Request, [Parameter(Mandatory)][string]$Output)
$ErrorActionPreference = 'Stop'
if (-not $Tools -or -not (Test-Path -LiteralPath (Join-Path $Tools 'LSLib.dll'))) {
  throw "LSLib not found ('$Tools'): pass --lslib <LSLib Tools dir>, set BG3RTX_LSLIB, or unpack LSLib into external/lslib-<version>/ (tools/assets/locate.py)"
}
$Tools = (Resolve-Path -LiteralPath $Tools).Path
$env:PATH = "$Tools;" + $env:PATH   # child process only
[Reflection.Assembly]::LoadFrom((Join-Path $Tools 'LSLib.dll')) | Out-Null
$req = Get-Content -LiteralPath $Request -Raw | ConvertFrom-Json
$v = [LSLib.VirtualTextures.VirtualTileSet]::new((Resolve-Path -LiteralPath $req.gts).Path)
try {
  $meta = @($v.FourCCMetadata.ExtractTextureMetadata())
  $tw = $v.Header.TileWidth - 2 * $v.Header.TileBorder; $th = $v.Header.TileHeight - 2 * $v.Header.TileBorder
  $levels = @($v.TileSetLevels).Count
  $out = @()
  if ($req.mode -eq 'resolve') {
    $vw = [double]$v.TileSetLevels[0].Width * $tw; $vh = [double]$v.TileSetLevels[0].Height * $th
    foreach ($q in $req.rects) {
      $r = @{ x = [long][math]::Round($q.p0[0] * $vw); y = [long][math]::Round($q.p0[1] * $vh)
              w = [long][math]::Round($q.p0[2] * $vw); h = [long][math]::Round($q.p0[3] * $vh); p0 = $q.p0 }
      $hit = @($meta | Where-Object { $_.X -eq $r.x -and $_.Y -eq $r.y -and $_.Width -eq $r.w -and $_.Height -eq $r.h })
      if ($hit.Count -ne 1) { $out += @{ rect = $r; matches = $hit.Count }; continue }
      $p = $hit[0]; $pages = [System.Collections.Generic.HashSet[string]]::new(); $used = 0
      for ($l = 0; $l -lt $levels; $l++) {
        $x0 = [math]::Floor(($p.X -shr $l) / $tw); $x1 = [math]::Floor((($p.X + $p.Width - 1) -shr $l) / $tw)
        $y0 = [math]::Floor(($p.Y -shr $l) / $th); $y1 = [math]::Floor((($p.Y + $p.Height - 1) -shr $l) / $th)
        $any = $false
        for ($y = $y0; $y -le $y1; $y++) { for ($x = $x0; $x -le $x1; $x++) {
          $t = [LSLib.VirtualTextures.GTSFlatTileInfo]::new()
          if ($v.GetTileInfo($l, 0, $x, $y, [ref]$t)) { $null = $pages.Add($v.PageFileInfos[$t.PageFileIndex].FileName); $any = $true }
        } }
        if ($any) { $used = $l + 1 }
      }
      $out += @{ rect = $r; matches = 1; name = $p.Name; levels = $used; pages = @($pages) }
    }
  } elseif ($req.mode -eq 'extract') {
    foreach ($t in $req.textures) {
      $p = @($meta | Where-Object Name -eq $t.name)[0]; $written = @()
      for ($l = 0; $l -lt $t.levels; $l++) {
        $img = $v.ExtractTexture($l, 0, $p)
        if ($null -eq $img) { break }
        $f = "$($t.out).$l.dds"; $img.SaveDDS($f); $written += $f
      }
      $out += @{ name = $t.name; files = $written }
    }
  } else { throw "unknown mode $($req.mode)" }
  ConvertTo-Json -InputObject @($out) -Depth 8 | Set-Content -LiteralPath $Output -Encoding utf8
} finally { $v.Dispose() }
