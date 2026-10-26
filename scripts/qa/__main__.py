import argparse
import sys
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
    a = sub.add_parser("analyze", help="analyse existing scene folders (with raw/) again")
    a.add_argument("dirs", nargs="+")
    a.add_argument("--keep-raw", action="store_true")
    args = ap.parse_args()

    if args.cmd == "run":
        out = Path(args.out) if args.out else capture.CAPS / "qa" / time.strftime("%Y%m%d-%H%M%S")
        all_scenes = capture.scenes()
        ids = [s.strip().upper() for s in args.scenes.split(",") if s.strip()]
        dirs = []
        t0 = time.time()
        for sid in ids:
            if sid not in all_scenes:
                raise SystemExit(f"unknown scene {sid}; known: {', '.join(all_scenes)}")
            d = out / sid
            capture.run_scene(sid, all_scenes[sid], d, full_res=args.full_res, temporal=max(2, args.temporal),
                              reference=args.reference, interval=args.interval, raw=args.raw)
            dirs.append(d)
        print(f"captures done in {time.time() - t0:.0f} s")
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
