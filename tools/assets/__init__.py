"""BG3RTX offline asset pipeline (docs/ASSET_DB.md).

Reads the game's LSPK packages (read-only), decodes LSF/LSX resources and builds the
asset database the layer loads at run time: placed lights, the shader -> material
table, rooms/roofs and lighting presets per level, light probes and effect lights.

  python -m tools.assets build --out data/assetdb
  python -m unittest discover -s tools/assets/tests -t .
"""
__version__ = "1.0.0"
