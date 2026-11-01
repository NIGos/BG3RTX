"""Python twin of scripts/low-priority.ps1: run this process (and every child it starts, e.g. dxc)
at BelowNormal priority so the PC stays usable. Windows children inherit the class.
Set BG3RTX_NORMAL_PRIORITY=1 to keep the normal priority."""
import os
import sys

_done = False


def lower_priority():
    global _done
    if _done or os.environ.get("BG3RTX_NORMAL_PRIORITY"):
        return
    _done = True
    try:
        if sys.platform == "win32":
            import ctypes
            k32 = ctypes.windll.kernel32
            k32.SetPriorityClass(k32.GetCurrentProcess(), 0x4000)  # BELOW_NORMAL_PRIORITY_CLASS
        else:
            os.nice(5)
    except Exception:
        pass
