import argparse
import queue
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
    r.add_argument("--interval", type=int, default=240, help="presents between traces")
    r.add_argument("--raw", action="store_true", help="also dump_raw=1 (float32 final frame; ~1 GB per trace)")
    r.add_argument("--keep-raw", action="store_true", help="do not prune the raw captures")
    r.add_argument("--capture-only", action="store_true")
    r.add_argument("--out", help="output folder (default captures/qa/<stamp>)")
    r.add_argument("--parallel", type=int, default=2, choices=(1, 2),
                   help="scenes captured at once, as shared background games (default 2)")
    r.add_argument("--exclusive", action="store_true",
                   help="one scene at a time, alone on the GPU (needed for timings)")
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
                                  interval=args.interval, raw=args.raw, tag=tag, exclusive=args.exclusive)

        threads = [threading.Thread(target=lane, args=(t,)) for t in ("a", "b")[:lanes]]
        for i, th in enumerate(threads):
            th.start()
            if i + 1 < len(threads):
                time.sleep(3)
        for th in threads:
            th.join()
        print(f"captures done in {time.time() - t0:.0f} s, {lanes} at a time"
              f"{'' if args.exclusive else ' (shared runs: run_s are not exclusive timings)'}")
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
