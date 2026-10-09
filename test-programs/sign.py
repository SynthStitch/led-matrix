#!/usr/bin/env python3
"""Pick what the LED sign shows. Arrows/j/k move, Enter runs, +/- brightness, s stops, q quits (sign keeps running)."""
import curses, subprocess, time

LIB = "/opt/rpi-rgb-led-matrix"
EX = LIB + "/examples-api-use"
CONF = "/opt/signtest/sign.conf"  # this sign's panel flags; see sign.conf.example

def load_flags():
    with open(CONF) as f:
        return " ".join(line.split("#")[0] for line in f).split()

FLAGS = load_flags()
# Matches every sign program by its path (anchored, so this menu itself never matches).
# Names alone fail: pkill -x only sees the first 15 characters ("scrolling-text-").
PATTERN = "^/opt/(rpi-rgb-led-matrix|signtest)/"

def demo(n, *extra): return [EX + "/demo", "-D", str(n), *extra]
ITEMS = [
    ("Game of Life",         demo(7, "-m", "80")),
    ("Colour evolution",     demo(10, "-m", "30")),
    ("Sandpile",             demo(6, "-m", "10")),
    ("Volume bars",          demo(9, "-m", "60")),
    ("Langton\x27s ant",     demo(8, "-m", "5")),
    ("Pulsing colour",       demo(4)),
    ("Brightness pulse",     demo(11)),
    ("Spinning square",      demo(0)),
    ("3D cube",              demo(12)),
    ("Clock",                [EX + "/clock", "-f", LIB + "/fonts/texgyre-27.bdf", "-x", "8", "-y", "14", "-C", "0,200,255"]),
    ("Scrolling text...",    None),   # asks for the text
    ("Panel ID test",        ["/opt/signtest/panel-id"]),
    ("Colour cycle test",    ["/opt/signtest/color-cycle"]),
]

def running():
    return subprocess.run(["pgrep", "-f", PATTERN], stdout=subprocess.DEVNULL).returncode == 0

def stop():
    # Wait for the old program to exit before starting another: two at once fight over the panels.
    subprocess.run(["pkill", "-TERM", "-f", PATTERN])
    for _ in range(30):
        if not running(): return
        time.sleep(0.1)
    subprocess.run(["pkill", "-KILL", "-f", PATTERN])
    time.sleep(0.5)

def start(cmd, brightness):
    stop()
    # New session + no stdin so it outlives this menu and the ssh connection.
    subprocess.Popen(cmd + FLAGS + [f"--led-brightness={brightness}"], stdin=subprocess.DEVNULL,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)

def ask(scr, prompt):
    curses.echo(); curses.curs_set(1)
    h, _ = scr.getmaxyx(); scr.addstr(h - 1, 0, prompt); scr.clrtoeol()
    text = scr.getstr(h - 1, len(prompt), 60).decode(errors="ignore")
    curses.noecho(); curses.curs_set(0)
    return text

def main(scr):
    curses.curs_set(0)
    sel, bright, last, status = 0, 40, None, "Nothing started from this menu yet."
    while True:
        scr.erase()
        scr.addstr(0, 0, "LED sign", curses.A_BOLD)
        scr.addstr(1, 0, "Enter run   +/- brightness   s stop   q quit (sign keeps running)")
        for i, (name, _) in enumerate(ITEMS):
            scr.addstr(3 + i, 2, ("> " if i == sel else "  ") + name, curses.A_REVERSE if i == sel else 0)
        scr.addstr(4 + len(ITEMS), 0, f"Brightness {bright}%   {status}")
        k = scr.getch()
        if k in (ord("q"), 27): return
        if k in (curses.KEY_UP, ord("k")): sel = (sel - 1) % len(ITEMS)
        elif k in (curses.KEY_DOWN, ord("j")): sel = (sel + 1) % len(ITEMS)
        elif k == ord("s"): stop(); last = None; status = "Stopped."
        elif k in (ord("+"), ord("=")) or k == ord("-"):
            bright = max(5, min(100, bright + (10 if k != ord("-") else -10)))
            if last: start(last[1], bright); status = f"Showing {last[0]}."
        elif k in (10, 13, curses.KEY_ENTER):
            name, cmd = ITEMS[sel]
            if cmd is None:
                text = ask(scr, "Text to scroll: ")
                if not text: continue
                cmd = [EX + "/scrolling-text-example", "-f", LIB + "/fonts/9x18B.bdf", "-y", "22", "-s", "6", "-C", "255,80,0", text]
                name = f"\"{text}\""
            last = (name, cmd); start(cmd, bright); status = f"Showing {name}."

if __name__ == "__main__":
    curses.wrapper(main)
