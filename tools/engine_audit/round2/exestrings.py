import mmap, re, sys
p = r"D:\SteamLibrary\steamapps\common\Baldurs Gate 3\bin\bg3.exe"
with open(p, "rb") as f:
    m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    out = open("exe_strings.txt", "w", encoding="utf-8")
    for x in re.finditer(rb"[\x20-\x7e]{5,}", m):
        out.write(f"{x.start():x} {x.group().decode()}\n")
    # utf16
    out2 = open("exe_wstrings.txt", "w", encoding="utf-8")
    for x in re.finditer(rb"(?:[\x20-\x7e]\x00){5,}", m):
        out2.write(f"{x.start():x} {x.group().decode('utf-16le')}\n")
