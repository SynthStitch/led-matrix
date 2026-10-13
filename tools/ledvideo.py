#!/usr/bin/env python3
"""Convert any video into the sign's .ledv format (run this on a PC with ffmpeg, not on the Pi).

    python ledvideo.py input.mp4                  -> input.ledv, 96x96, 30 fps, black bars to keep the shape
    python ledvideo.py input.mp4 --crop           -> fill the frame instead, cutting off the edges
    python ledvideo.py input.mp4 --stretch        -> squash or stretch the whole picture to fill the frame
    python ledvideo.py input.mp4 --size 96x64 --fps 24 --start 10 --length 30
    python ledvideo.py input.mp4 --speed 0.5      -> plays at half speed (same frames, shown more slowly)
    python ledvideo.py input.mp4 --black 40       -> anything this dark (0-255) turns fully off; LEDs show "near black" as a glow
    python ledvideo.py input.mp4 --white         -> white background becomes black (other colours kept)
    python ledvideo.py input.mp4 --reverse        -> play backwards;  --mirror flips left-right, --vflip upside down
    python ledvideo.py input.mp4 --gamma 2.2 --saturation 1.4   -> tame pale, washed-out videos that blind on LEDs
    python ledvideo.py input.mp4 --level 0.6      -> turn the whole picture down to 60% (for bright, saturated videos)

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


def convert(src, dst, w, h, fps, crop=False, start=None, length=None, stretch=False, speed=1.0, black=0,
            reverse=False, mirror=False, vflip=False, gamma=1.0, saturation=1.0, level=1.0, white=False):
    if stretch:
        fit = f"scale={w}:{h}:flags=area"
    elif crop:
        fit = f"scale={w}:{h}:force_original_aspect_ratio=increase:flags=area,crop={w}:{h}"
    else:
        fit = f"scale={w}:{h}:force_original_aspect_ratio=decrease:flags=area,pad={w}:{h}:(ow-iw)/2:(oh-ih)/2"
    if saturation != 1.0:
        fit = f"eq=saturation={saturation}," + fit
    if white:  # white background -> black; before scaling so edges blend into black, not grey
        fit = "colorkey=white:0.1:0.05,premultiply=inplace=1,format=rgb24," + fit
    if black > 0 or gamma != 1.0 or level != 1.0:
        # gamma > 1 darkens mid-tones (pale colours look near-white on LEDs), then the dark end is cut to off
        v = f"255*pow(val/255,{gamma})"
        cut = f"clip(({v}-{black})*255/{255 - black}*{level},0,255)"  # level scales the whole result down
        fit += f",format=rgb24,lutrgb=r='{cut}':g='{cut}':b='{cut}'"
    if mirror:
        fit += ",hflip"
    if vflip:
        fit += ",vflip"
    if reverse:  # after scaling, so ffmpeg only buffers small frames
        fit += ",reverse"
    cmd = ["ffmpeg", "-v", "error"]
    if start is not None:
        cmd += ["-ss", str(start)]
    cmd += ["-i", src]
    if length is not None:
        cmd += ["-t", str(length)]
    cmd += ["-an", "-vf", fit, "-r", str(fps), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    with open(dst, "wb") as out:
        out.write(MAGIC + struct.pack("<HHH", w, h, max(1, round(fps * speed))))  # playback rate: speed scales it
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
    ap.add_argument("--speed", type=float, default=1.0, help="playback speed, e.g. 0.5 for half speed")
    ap.add_argument("--black", type=int, default=0, help="0-255: this dark and below becomes fully off (try 30-60)")
    ap.add_argument("--gamma", type=float, default=1.0, help="above 1 darkens mid-tones; try 1.8-2.4 for pale videos")
    ap.add_argument("--level", type=float, default=1.0, help="overall brightness 0-1, e.g. 0.6 for 60%%")
    ap.add_argument("--saturation", type=float, default=1.0, help="above 1 makes colours richer; try 1.2-1.6")
    ap.add_argument("--white", action="store_true", help="turn a white background black, keeping other colours")
    ap.add_argument("--reverse", action="store_true", help="play backwards")
    ap.add_argument("--mirror", action="store_true", help="flip left-right")
    ap.add_argument("--vflip", action="store_true", help="flip upside down")
    ap.add_argument("--start", type=float, help="start this many seconds in")
    ap.add_argument("--length", type=float, help="keep only this many seconds")
    a = ap.parse_args()
    w, h = (int(n) for n in a.size.lower().split("x"))
    out = a.output or os.path.splitext(a.input)[0] + ".ledv"
    frames = convert(a.input, out, w, h, a.fps, a.crop, a.start, a.length, a.stretch, a.speed, a.black,
                     a.reverse, a.mirror, a.vflip, a.gamma, a.saturation, a.level, a.white)
    print(f"{out}: {frames} frames, plays for {frames / max(1, round(a.fps * a.speed)):.1f} s, {os.path.getsize(out) / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
