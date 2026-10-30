#!/usr/bin/env python3
"""Mirror this PC's screen onto the sign, live (Windows, needs ffmpeg). The Pi must be running tdlive.

    python screencast.py                         -> whole desktop, shrunk to fit with black bars
    python screencast.py --crop                  -> fill the square, cutting off the sides
    python screencast.py --region 0,0,1080,1080  -> only this part of the screen (x,y,width,height)
    python screencast.py --fps 20 --to fe80::...%27

The Pi's address comes from the "Host pi" HostName in ~/.ssh/config, so it follows `ssh pi`.
Ctrl+C stops; the sign goes black 2 s later.
"""
import argparse
import os
import re
import socket
import subprocess

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


def main():
    ap = argparse.ArgumentParser(description="Mirror the screen onto the LED sign.")
    ap.add_argument("--to", help="Pi address (default: Host pi in ~/.ssh/config)")
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--crop", action="store_true", help="fill the square instead of black bars")
    ap.add_argument("--region", help="x,y,width,height of the screen area to capture")
    a = ap.parse_args()

    grab = ["-f", "gdigrab", "-framerate", str(a.fps), "-draw_mouse", "1"]
    if a.region:
        x, y, w, h = a.region.split(",")
        grab += ["-offset_x", x, "-offset_y", y, "-video_size", f"{w}x{h}"]
    fit = (f"scale={W}:{H}:force_original_aspect_ratio=increase:flags=area,crop={W}:{H}" if a.crop else
           f"scale={W}:{H}:force_original_aspect_ratio=decrease:flags=area,pad={W}:{H}:(ow-iw)/2:(oh-ih)/2")
    ff = subprocess.Popen(["ffmpeg", "-v", "error", *grab, "-i", "desktop", "-vf", fit,
                           "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], stdout=subprocess.PIPE)

    family, dest = target(a.to or pi_address())
    sock = socket.socket(family, socket.SOCK_DGRAM)
    print(f"Mirroring to {dest[0]} (Ctrl+C to stop)")
    try:
        while True:
            frame = ff.stdout.read(FRAME)
            if len(frame) < FRAME:
                break
            sock.sendto(frame, dest)  # one datagram per frame, as tdlive expects
    except KeyboardInterrupt:
        pass
    finally:
        ff.kill()


if __name__ == "__main__":
    main()
