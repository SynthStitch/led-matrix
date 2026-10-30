#!/usr/bin/env python3
"""Mirror this PC's screen onto the sign, live (Windows, needs ffmpeg). The Pi must be running tdlive.

    python screencast.py                         -> whole desktop, shrunk to fit with black bars
    python screencast.py --crop                  -> fill the square, cutting off the sides
    python screencast.py --region 0,0,1080,1080  -> only this part of the screen (x,y,width,height)
    python screencast.py --screen 2              -> one monitor (numbered left to right); press Enter to go to the next
    python screencast.py --fps 20 --to fe80::...%27

The Pi's address comes from the "Host pi" HostName in ~/.ssh/config, so it follows `ssh pi`.
Ctrl+C stops; the sign goes black 2 s later.
"""
import argparse
import ctypes
import os
import re
import socket
import subprocess
import threading

W = H = 96
FRAME = W * H * 3


def pi_address():
    text = open(os.path.expanduser("~/.ssh/config")).read()
    m = re.search(r"^Host pi\s*$.*?^\s*HostName\s+(\S+)", text, re.M | re.S)
    if not m:
        raise SystemExit("No 'Host pi' HostName in ~/.ssh/config; pass --to ADDRESS")
    return m.group(1).replace("%%", "%")


def target(addr):
    # (host, port, flowinfo, scope_id): link-local addresses need the interface number after the %
    host, _, scope = addr.partition("%")
    return socket.AF_INET6, (host, 5005, 0, int(scope or 0))


def monitors():
    """Monitor rectangles (x, y, w, h), left to right, in Windows' scaled coordinates, which is what gdigrab uses.
    Don't make this process DPI-aware: real-pixel rectangles land outside gdigrab's desktop on scaled screens."""
    from ctypes import wintypes as wt
    found = []
    cb = ctypes.WINFUNCTYPE(ctypes.c_int, wt.HMONITOR, wt.HDC, ctypes.POINTER(wt.RECT), wt.LPARAM)
    ctypes.windll.user32.EnumDisplayMonitors(None, None, cb(lambda h, dc, r, lp: found.append(
        (r[0].left, r[0].top, r[0].right - r[0].left, r[0].bottom - r[0].top)) or 1), 0)
    return sorted(found)


def capture(region, fps, crop):
    grab = ["-f", "gdigrab", "-framerate", str(fps), "-draw_mouse", "1"]
    if region:
        x, y, w, h = region
        grab += ["-offset_x", str(x), "-offset_y", str(y), "-video_size", f"{w}x{h}"]
    fit = (f"scale={W}:{H}:force_original_aspect_ratio=increase:flags=area,crop={W}:{H}" if crop else
           f"scale={W}:{H}:force_original_aspect_ratio=decrease:flags=area,pad={W}:{H}:(ow-iw)/2:(oh-ih)/2")
    return subprocess.Popen(["ffmpeg", "-v", "error", *grab, "-i", "desktop", "-vf", fit,
                             "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], stdout=subprocess.PIPE)


def main():
    ap = argparse.ArgumentParser(description="Mirror the screen onto the LED sign.")
    ap.add_argument("--to", help="Pi address (default: Host pi in ~/.ssh/config)")
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--crop", action="store_true", help="fill the square instead of black bars")
    ap.add_argument("--region", help="x,y,width,height of the screen area to capture")
    ap.add_argument("--screen", type=int, help="capture one monitor, numbered from the left (1, 2, ...)")
    a = ap.parse_args()

    screens = monitors()
    current = [a.screen - 1 if a.screen else None]  # None = whole desktop or --region
    switched = threading.Event()

    def region():
        if current[0] is not None:
            return screens[current[0]]
        return tuple(int(v) for v in a.region.split(",")) if a.region else None

    def keys():  # Enter = next screen
        try:
            for _ in iter(input, None):
                current[0] = 0 if current[0] is None else (current[0] + 1) % len(screens)
                print(f"Screen {current[0] + 1} of {len(screens)}", flush=True)
                switched.set()
        except EOFError:  # no keyboard (run in the background): just keep mirroring
            pass

    threading.Thread(target=keys, daemon=True).start()
    family, dest = target(a.to or pi_address())
    sock = socket.socket(family, socket.SOCK_DGRAM)
    print(f"Mirroring to {dest[0]}: {len(screens)} screens, Enter = next screen, Ctrl+C = stop", flush=True)
    try:
        while True:
            switched.clear()
            ff = capture(region(), a.fps, a.crop)
            while not switched.is_set():
                frame = ff.stdout.read(FRAME)
                if len(frame) < FRAME:
                    return
                sock.sendto(frame, dest)  # one datagram per frame, as tdlive expects
            ff.kill()
    except KeyboardInterrupt:
        pass
    finally:
        ff.kill()


if __name__ == "__main__":
    main()
