"""Index BG3 save games: location, play time and thumbnail for each save.

Reads the LSPK v18 package (.lsv) of every save, lists its files and pulls
SaveInfo.json (current level, party, game time) when present. Read-only:
saves are never modified.

  python scripts/save_index.py [--json out.json] [--thumbs out_dir]
"""
import argparse, json, os, struct, sys
import lz4.block

ROOT = os.path.expandvars(r"%LOCALAPPDATA%\Larian Studios\Baldur's Gate 3\PlayerProfiles\Public\Savegames\Story")


def read_lspk(path):
    with open(path, "rb") as f:
        hdr = f.read(40)
        magic, ver, list_off, list_size = struct.unpack_from("<4sIQI", hdr, 0)
        if magic != b"LSPK" or ver < 15:
            raise ValueError(f"not LSPK v15+: {magic} {ver}")
        f.seek(list_off)
        n, csize = struct.unpack("<II", f.read(8))
        raw = lz4.block.decompress(f.read(csize), uncompressed_size=n * 272)
        files = {}
        for i in range(n):
            e = raw[i * 272:(i + 1) * 272]
            name = e[:256].split(b"\0", 1)[0].decode("utf-8", "replace")
            off_lo, off_hi, part, flags, disk, usize = struct.unpack_from("<IHBBII", e, 256)
            files[name] = (off_lo | (off_hi << 32), flags, disk, usize)
        return files


def read_file(path, entry):
    off, flags, disk, usize = entry
    with open(path, "rb") as f:
        f.seek(off)
        data = f.read(disk)
    method = flags & 0x0F
    if method == 0:
        return data
    if method == 1:
        import zlib
        return zlib.decompress(data)
    if method == 2:
        return lz4.block.decompress(data, uncompressed_size=usize)
    if method == 3:
        import zstandard
        return zstandard.ZstdDecompressor().decompress(data, max_output_size=usize)
    raise ValueError(f"compression {method}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json")
    ap.add_argument("--thumbs")
    a = ap.parse_args()
    out = []
    for d in sorted(os.listdir(ROOT)):
        full = os.path.join(ROOT, d)
        lsv = [x for x in os.listdir(full) if x.lower().endswith(".lsv")]
        if not lsv:
            continue
        p = os.path.join(full, lsv[0])
        rec = {"folder": d, "lsv": lsv[0], "mtime": os.path.getmtime(p)}
        try:
            files = read_lspk(p)
            if "SaveInfo.json" in files:
                rec["info"] = json.loads(read_file(p, files["SaveInfo.json"]).decode("utf-8", "replace"))
            else:
                rec["files"] = sorted(files)[:20]
        except Exception as e:
            rec["error"] = str(e)
        if a.thumbs:
            webp = [x for x in os.listdir(full) if x.lower().endswith(".webp")]
            if webp:
                from PIL import Image
                os.makedirs(a.thumbs, exist_ok=True)
                Image.open(os.path.join(full, webp[0])).convert("RGB").save(
                    os.path.join(a.thumbs, d.split("__")[0] + ".jpg"), quality=80)
        out.append(rec)
    if a.json:
        json.dump(out, open(a.json, "w", encoding="utf-8"), indent=1)
    for r in out:
        i = r.get("info", {})
        print(r["folder"][:60], "|", json.dumps(i)[:200] if i else r.get("error") or r.get("files"))


if __name__ == "__main__":
    main()
