#!/usr/bin/env python3
"""Convert any video into the sign's .ledv format (run this on a PC with ffmpeg, not on the Pi).

    python ledvideo.py input.mp4                  -> input.ledv, 96x96, 30 fps, black bars to keep the shape
    python ledvideo.py input.mp4 --crop           -> fill the frame instead, cutting off the edges
    python ledvideo.py input.mp4 --stretch        -> squash or stretch the whole picture to fill the frame
    python ledvideo.py input.mp4 --size 96x64 --fps 24 --start 10 --length 30

Then copy it to the Pi; the `sign` menu lists everything in that folder as "Video: <name>":
    ssh pi "mkdir -p /opt/signtest/videos && cat > /opt/signtest/videos/NAME.ledv" < NAME.ledv

.ledv is a 10-byte header (b"LEDV", width, height, fps as little-endian uint16) followed by raw RGB frames.
About 0.8 MB per second at 96x96, 30 fps.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

MAGIC = b"LEDV"


def convert(src, dst, w, h, fps, crop=False, start=None, length=None, stretch=False):
    if stretch:
        fit = f"scale={w}:{h}:flags=area"
    elif crop:
        fit = f"scale={w}:{h}:force_original_aspect_ratio=increase:flags=area,crop={w}:{h}"
    else:
        fit = f"scale={w}:{h}:force_original_aspect_ratio=decrease:flags=area,pad={w}:{h}:(ow-iw)/2:(oh-ih)/2"
    cmd = ["ffmpeg", "-v", "error"]
    if start is not None:
        cmd += ["-ss", str(start)]
    cmd += ["-i", src]
    if length is not None:
        cmd += ["-t", str(length)]
    cmd += ["-an", "-vf", fit, "-r", str(fps), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    with open(dst, "wb") as out:
        out.write(MAGIC + struct.pack("<HHH", w, h, fps))
        out.flush()
        subprocess.run(cmd, stdout=out, check=True)
    return (os.path.getsize(dst) - 10) // (w * h * 3)


def selftest():
    with tempfile.TemporaryDirectory() as d:
        src, dst = os.path.join(d, "test.mp4"), os.path.join(d, "test.ledv")
        subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "testsrc=duration=2:size=320x240:rate=30",
                        "-pix_fmt", "yuv420p", src], check=True)
        frames = convert(src, dst, 96, 96, 30)
        size = os.path.getsize(dst)
        with open(dst, "rb") as f:
            head = f.read(10)
        assert head[:4] == MAGIC and struct.unpack("<HHH", head[4:]) == (96, 96, 30), head
        assert frames == 60 and size == 10 + 60 * 96 * 96 * 3, (frames, size)
    print("selftest OK: 2 s test clip -> 60 frames of 96x96 with a valid header")


def main():
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    ap = argparse.ArgumentParser(description="Convert a video for the LED sign.")
    ap.add_argument("input")
    ap.add_argument("output", nargs="?")
    ap.add_argument("--size", default="96x96", help="WIDTHxHEIGHT of the frame (default 96x96)")
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--crop", action="store_true", help="fill the frame, cutting off edges, instead of black bars")
    ap.add_argument("--stretch", action="store_true", help="squash or stretch the whole picture to fill the frame")
    ap.add_argument("--start", type=float, help="start this many seconds in")
    ap.add_argument("--length", type=float, help="keep only this many seconds")
    a = ap.parse_args()
    w, h = (int(n) for n in a.size.lower().split("x"))
    out = a.output or os.path.splitext(a.input)[0] + ".ledv"
    frames = convert(a.input, out, w, h, a.fps, a.crop, a.start, a.length, a.stretch)
    print(f"{out}: {frames} frames, {frames / a.fps:.1f} s, {os.path.getsize(out) / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
