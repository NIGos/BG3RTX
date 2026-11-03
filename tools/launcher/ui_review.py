"""Reproducible offline A2 layout review; no game, installer or shader tool."""
from pathlib import Path
import hashlib
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "captures/gui-product-review/ux-a2-launcher"
EXE = ROOT / "bin/BG3RTX.exe"


def configuration_identity():
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None
            for p in (ROOT / "bg3rtx_user.ini", ROOT / "bg3rtx_launcher.ini", ROOT / "bg3rtx_ui.ini")}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    before = configuration_identity()
    items = [("graphics", s, 944, False) for s in (1, 1.25, 1.5, 2)]
    items += [(v, 1, 944, False) for v in
              ("experimental", "missing", "probe", "error", "contrast", "first-run", "advanced", "support")]
    items += [("graphics", 1, 720, False), ("error", 1, 720, False), ("experimental", 1, 720, True)]
    for view, scale, width, bottom in items:
        name = f"{view}-{scale}" + ("-720" if width == 720 else "") + ("-bottom" if bottom else "")
        bmp = OUT / (name + ".bmp")
        cmd = [str(EXE), "--no-game", "--screenshot", str(bmp), "--screenshot-view", view,
               "--screenshot-scale", str(scale), "--screenshot-width", str(width)]
        if bottom:
            cmd.append("--screenshot-bottom")
        subprocess.run(cmd, check=True, timeout=30)
        with Image.open(bmp) as image:
            image.convert("RGB").save(bmp.with_suffix(".png"))
        bmp.unlink()  # Only this invocation's exact temporary bitmap, never a directory.
        print(name, flush=True)
    for args in (["--no-game", "--launch"], ["--no-game", "--preset", "ultra"],
                 ["--screenshot", str(OUT / "invalid.bmp"), "--launch"]):
        result = subprocess.run([str(EXE)] + args, timeout=5)
        if result.returncode != 2:
            raise AssertionError(f"Unsafe mixed mode was accepted: {args}")
    if before != configuration_identity():
        raise AssertionError("Offline review changed configuration")
    print(f"{len(items)} layout captures; 3 mixed-mode guards; configuration unchanged", flush=True)


if __name__ == "__main__":
    main()
