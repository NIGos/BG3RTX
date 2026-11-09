"""Disposable B2 fixtures + independent ZIP integrity/preview verification."""
import ctypes
import pathlib
import subprocess
import sys
import uuid
import zipfile

ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)
base = pathlib.Path(sys.argv[2]) / uuid.uuid4().hex
base.mkdir(parents=True)
target = base / 'junction-target'
target.mkdir()
link = base / 'fixture-link'
# All paths are controlled fresh fixture paths; no game/user data is inspected.
# PowerShell -Command handles positional args differently; pass paths via env.
import os
environment = dict(os.environ, B2_LINK=str(link), B2_TARGET=str(target))
subprocess.run(['powershell', '-NoProfile', '-Command',
                "$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path $env:B2_LINK -Target $env:B2_TARGET | Out-Null"],
               env=environment, check=True)
try:
    root = base / 'Unicode spaced fixture λ'
    subprocess.run([sys.argv[1], str(root)], check=True)
    with zipfile.ZipFile(root / 'report.zip') as z:
        assert z.testzip() is None, 'ZIP CRC failure'
        expected = root / 'expected'
        assert set(z.namelist()) == {p.relative_to(expected).as_posix() for p in expected.rglob('*') if p.is_file()}
        for name in z.namelist():
            assert z.read(name) == (expected / name).read_bytes(), name
    # Opt-in latest save: exact bytes of the fixture save, archived copy marked read-only.
    save_dir = root / 'profiles' / 'Second' / 'Savegames' / 'Story' / 'Tav-1234__QuickSave_1'
    with zipfile.ZipFile(root / 'report-save.zip') as z:
        assert z.testzip() is None, 'ZIP CRC failure (save)'
        saves = [n for n in z.namelist() if n.startswith('saves/')]
        assert sorted(saves) == ['saves/Tav-1234__QuickSave_1/QuickSave_1.lsv', 'saves/Tav-1234__QuickSave_1/QuickSave_1.webp'], saves
        for name in saves:
            assert z.read(name) == (save_dir / name.rsplit('/', 1)[1]).read_bytes(), name
            assert z.getinfo(name).external_attr & 1, name
    assert not (target / 'escape.zip').exists()
    print('Independent ZIP CRC, exact preview bytes, Unicode destination and junction checks passed.', flush=True)
finally:
    # Unlink ONLY the junction itself. Never recursive cleanup.
    subprocess.run(['powershell', '-NoProfile', '-Command',
                    '[IO.Directory]::Delete($env:B2_LINK,$false)'], env=environment, check=True)
