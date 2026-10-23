#!/usr/bin/env python3
"""Cycle through everything in the sign menu, forever.

Each video plays for one full loop (clamped to 20-90 s); everything else shows for OTHER_SECONDS.
Started from the `sign` menu as "Cycle everything"; the menu's stop/turn-off also stops this.
"""
import importlib.util
import os
import signal
import struct
import subprocess
import sys
import time

OTHER_SECONDS = 45
VIDEO_MIN, VIDEO_MAX = 20, 90
SKIP = {"Cycle everything", "Live: TouchDesigner", "Scrolling text...", "Panel ID test", "Colour cycle test", "Clock", "Shapes: all, cycling"}

_spec = importlib.util.spec_from_file_location("sign", "/opt/signtest/sign.py")
sign = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sign)

brightness = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--led-brightness=")), "40")
# --only WORD: cycle just the entries whose name contains WORD (e.g. "--only akira").
only = sys.argv[sys.argv.index("--only") + 1].lower() if "--only" in sys.argv[1:-1] else None
child = None


def seconds_for(cmd):
    if cmd[0].endswith("/videoplay"):
        try:
            with open(cmd[1], "rb") as f:
                w, h, fps = struct.unpack("<HHH", f.read(10)[4:])
            frames = (os.path.getsize(cmd[1]) - 10) // (w * h * 3)
            return min(VIDEO_MAX, max(VIDEO_MIN, frames / fps))
        except (OSError, struct.error, ZeroDivisionError):
            return VIDEO_MIN
    return OTHER_SECONDS


def end_child():
    if child and child.poll() is None:
        child.terminate()
        try:
            child.wait(3)
        except subprocess.TimeoutExpired:
            child.kill()


def quit_(*_):
    end_child()
    sys.exit(0)


signal.signal(signal.SIGTERM, quit_)
signal.signal(signal.SIGINT, quit_)

while True:
    shown = 0
    for name, cmd in sign.ITEMS:
        if cmd is None or name in SKIP or name.startswith("Cycle") or not os.path.exists(cmd[0]):
            continue
        if only and only not in name.lower():
            continue
        merlin = cmd[0].startswith(sign.AOP)  # Merlin demos take no flags and run from their own folder
        args = cmd if merlin else cmd + sign.FLAGS + [f"--led-brightness={brightness}"]
        child = subprocess.Popen(args, cwd=os.path.dirname(cmd[0]) if merlin else None, stdin=subprocess.DEVNULL,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(seconds_for(cmd))
        end_child()
        shown += 1
    if not shown:  # nothing playable: don't spin
        time.sleep(10)
