param([Parameter(Mandatory)][string]$Tools,[Parameter(Mandatory)][string]$Gts,
      [Parameter(Mandatory)][string]$Texture,[Parameter(Mandatory)][string]$Output,[switch]$Decode)
$ErrorActionPreference='Stop'
$Tools=(Resolve-Path -LiteralPath $Tools).Path
$env:PATH="$Tools;"+$env:PATH # Child process only; no installation or user preferences.
[Reflection.Assembly]::LoadFrom((Join-Path $Tools 'LSLib.dll')) | Out-Null
$v=[LSLib.VirtualTextures.VirtualTileSet]::new((Resolve-Path -LiteralPath $Gts).Path)
try {
    $matches=@($v.FourCCMetadata.ExtractTextureMetadata() | Where-Object Name -eq $Texture)
    if($matches.Count -ne 1){throw 'Selected material texture is not unique in this GTS'}
    $p=$matches[0];$tw=$v.Header.TileWidth-2*$v.Header.TileBorder;$th=$v.Header.TileHeight-2*$v.Header.TileBorder
    if($p.Width -gt 4096 -or $p.Height -gt 4096 -or $p.X%$tw -or $p.Y%$th -or $p.Width%$tw -or $p.Height%$th){throw 'Outside this bounded, tile-aligned probe'}
    $pages=[System.Collections.Generic.HashSet[int]]::new();$tiles=0
    for($y=$p.Y/$th;$y -lt ($p.Y+$p.Height)/$th;$y++) {
        for($x=$p.X/$tw;$x -lt ($p.X+$p.Width)/$tw;$x++) {
            $tile=[LSLib.VirtualTextures.GTSFlatTileInfo]::new()
            if(!$v.GetTileInfo(0,0,$x,$y,[ref]$tile)){throw 'Missing selected level-0/layer-0 tile'}
            $null=$pages.Add($tile.PageFileIndex);$tiles++
        }
    }
    if($Decode) {
        $image=$v.ExtractTexture(0,0,$p)
        if($null -eq $image){throw 'Missing selected texture data'}
        $image.SaveDDS($Output)
    } else {
        @{texture=$p;level=0;layer=0;tileWidth=$tw;tileHeight=$th;tiles=$tiles;layers=$v.TileSetLayers;
          pages=@($pages | ForEach-Object {$v.PageFileInfos[$_].FileName})} |
            ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Output -Encoding utf8
    }
} finally {$v.Dispose()}
