import argparse
import os
import queue
import subprocess
import sys
import threading
import time
from pathlib import Path

from . import capture


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    ap = argparse.ArgumentParser(prog="python -m scripts.qa")
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="capture and analyse scenes")
    r.add_argument("--scenes", required=True, help="comma-separated ids from docs/TEST_SCENES.md, e.g. S1,S3")
    r.add_argument("--full-res", action="store_true", help="dump previews at native size (3840 wide)")
    r.add_argument("--reference", action="store_true", help="add a high-sample reference trace and a FLIP score")
    r.add_argument("--temporal", type=int, default=3, help="static-camera frames for the flicker metric (on and off)")
    r.add_argument("--temporal-gi", action="store_true",
                   help="add a temporal set with the GI neutral (gi_strength=0 gi_ao=0): flicker of the GI alone")
    r.add_argument("--warmup", type=int, default=2,
                   help="traces taken and ignored before on0 (the first seconds after a load are not steady)")
    r.add_argument("--temporal-sun", action="store_true",
                   help="add a temporal set with the RT sun shadows off: flicker of the RT sun alone")
    r.add_argument("--interval", type=int, default=240, help="presents between traces")
    r.add_argument("--raw", action="store_true", help="also dump_raw=1 (float32 final frame; ~1 GB per trace)")
    r.add_argument("--keep-raw", action="store_true", help="do not prune the raw captures")
    r.add_argument("--capture-only", action="store_true")
    r.add_argument("--out", help="output folder (default captures/qa/<stamp>)")
    r.add_argument("--sharc", action="store_true",
                   help="add the SHaRC radiance cache traces (sharc_on, sharc_refl_off, sharc_roof) and its leak gate")
    r.add_argument("--set", default="", help="extra bg3rtx.ini keys for every trace, e.g. rt_sun_top=0,rt_sun_near=1")
    r.add_argument("--parallel", type=int, default=1, choices=(1, 2),
                   help="scenes captured at once; 2 = shared background games (experimental)")
    r.add_argument("--exclusive", action="store_true",
                   help="one scene at a time, alone on the GPU (the default; overrides --parallel)")
    a = sub.add_parser("analyze", help="analyse existing scene folders (with raw/) again")
    a.add_argument("dirs", nargs="+")
    a.add_argument("--keep-raw", action="store_true")
    args = ap.parse_args()

    if args.cmd == "run":
        out = Path(args.out) if args.out else capture.CAPS / "qa" / time.strftime("%Y%m%d-%H%M%S")
        all_scenes = capture.scenes()
        ids = [s.strip().upper() for s in args.scenes.split(",") if s.strip()]
        for sid in ids:
            if sid not in all_scenes:
                raise SystemExit(f"unknown scene {sid}; known: {', '.join(all_scenes)}")
        dirs = [out / sid for sid in ids]
        t0 = time.time()
        lanes = 1 if args.exclusive else args.parallel
        # One lane per concurrent game (auto-run -Tag a / b); each takes the next scene.
        todo = queue.Queue()
        for sid in ids:
            todo.put(sid)

        def lane(tag):
            while True:
                try:
                    sid = todo.get_nowait()
                except queue.Empty:
                    return
                capture.run_scene(sid, all_scenes[sid], out / sid, full_res=args.full_res,
                                  temporal=max(2, args.temporal), reference=args.reference,
                                  interval=args.interval, raw=args.raw, tag=tag, exclusive=args.exclusive,
                                  shared=lanes > 1, extra=[x.strip() for x in args.set.split(",") if x.strip()],
                                  split_gi=args.temporal_gi, split_sun=args.temporal_sun, warmup=max(0, args.warmup),
                                  sharc=args.sharc)

        if lanes > 1:
            # A holder keeps the exclusive lock for this whole process (scripts/gamelock.ps1),
            # so older auto-run.ps1 copies, which close every bg3.exe, wait for it.
            flag = Path(os.environ["TEMP"]) / f"bg3rtx_session_{os.getpid()}.ready"
            subprocess.Popen(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden",
                              "-File", str(capture.ROOT / "scripts" / "gamelock.ps1"), "-Holder",
                              "-WhilePid", str(os.getpid())], creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
            print("waiting for the exclusive lock for this session...", flush=True)
            while not flag.exists():
                time.sleep(0.5)
            flag.unlink(missing_ok=True)
        threads = [threading.Thread(target=lane, args=(t,)) for t in ("a", "b")[:lanes]]
        for i, th in enumerate(threads):
            th.start()
            if i + 1 < len(threads):
                time.sleep(3)
        for th in threads:
            th.join()
        print(f"captures done in {time.time() - t0:.0f} s, {lanes} at a time"
              f"{' (shared runs: run_s are not exclusive timings)' if lanes > 1 else ''}")
        if args.capture_only:
            return
        from . import analyze
        analyze.analyze_all(dirs, out, keep_raw=args.keep_raw)
    else:
        from . import analyze
        dirs = [Path(d) for d in args.dirs]
        analyze.analyze_all(dirs, dirs[0].parent, keep_raw=args.keep_raw)


if __name__ == "__main__":
    main()
