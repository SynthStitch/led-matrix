# LED matrix sign: notes for Claude

This repo sets up a Raspberry Pi to drive HUB75 RGB LED panels with
[hzeller/rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix), and gives the owner a menu (`sign`)
to choose what the panels show. The person you're helping may be new to this. Explain hardware steps plainly
(which pin, which probe) and keep shell work on your side.

## Setting up a new Pi

1. **Reach the Pi.** Ask how it's connected (Wi-Fi, Ethernet, or a cable straight to the PC). Get SSH working first.
   Record the Pi model (`cat /proc/device-tree/model`) and OS (`cat /etc/os-release`).
2. **Get the repo onto the Pi** (`git clone` if it has internet, or copy it over SSH), then run `sudo sh setup.sh`.
   It installs g++, make and git if missing, builds the library into `/opt/rpi-rgb-led-matrix`, builds the test
   programs into `/opt/signtest`, installs the `sign` menu and turns onboard audio off. Reboot once if it says so.
3. **Pick the GPIO mapping for the driver board** and put it in `/opt/signtest/sign.conf`:

   | Board | Flag |
   |---|---|
   | Adafruit Triple LED Matrix Bonnet (3 ports), Seengreat, Electrodragon | `--led-gpio-mapping=regular` |
   | Adafruit RGB Matrix HAT or Bonnet (1 port) | `--led-gpio-mapping=adafruit-hat` |
   | Panels wired straight to the Pi with jumpers | `regular` (works badly: no 5V level shifting) |

   Slowdown: start at `--led-slowdown-gpio=4` on a Pi 4. Raise it if you see random pixels.
4. **Find the panel layout** with `panel-id` (below), then write the chain, parallel and pixel-mapper flags into
   `sign.conf`. `sign.conf.example` is a worked example.
5. **Hand over:** `ssh -t <pi> sign` opens the menu. Whatever it starts keeps running after the menu closes.
   Nothing starts at boot unless you add a systemd service.

## Finding the panel layout

Run `panel-id` with a guess, ask for a photo, and adjust:
```sh
cd /opt/signtest
timeout 600 ./panel-id --led-rows=32 --led-cols=32 --led-chain=<panels per port> --led-parallel=<ports> \
  --led-gpio-mapping=<mapping> --led-slowdown-gpio=4 --led-brightness=20
```
Panels show colours in the order red, green, blue, yellow, cyan, magenta (repeating) with a black number. It counts
along each chain, then port by port (port 1 first). How to read the photo:
- **The same pattern repeats on every panel:** the chain is set shorter than the real chain.
- **Only the last few colours appear:** the chain is set longer than the real chain. Data for the first panels falls off the end.
- **A whole row of panels copies another:** those rows are on different ports, so set `--led-parallel` instead.
- **Numbers read upside down** (a 9 looks like a 6, a 7 like an L): add `--led-pixel-mapper=Rotate:180`.
- Snaking or stacked layouts: see the `U-mapper`, `V-mapper` and `Rotate` mappers in the library README.

## Optional: Marc Merlin's demos (Aurora, matrix rain, TwinkleFOX, fireworks)

The `sign` menu lists these first. (Its Pac-Man is this repo's own `pacman`, not Merlin's, which is a few sprites circling the edge.) They come from
[ArduinoOnPc-FastLED-GFX-LEDMatrix](https://github.com/marcmerlin/ArduinoOnPc-FastLED-GFX-LEDMatrix), installed at
`/opt/aop`. The menu runs `/opt/aop/examples/<Name>/<Name>` from that folder and passes no flags, because the panel
layout is compiled in. To install on a new sign:

1. `git clone --recurse-submodules` it into `/opt/aop`. The `NeoMatrix_Demos_Private` submodule fails (it's private);
   that's fine. If other submodule folders come out empty, run `git reset --hard` inside each. The demos are
   symlinks, so clone on the Pi or copy with symlinks intact (a Windows checkout turns them into text files).
2. `ln -s /opt/rpi-rgb-led-matrix /opt/aop/rpi-rgb-led-matrix`
3. Add the sign's layout to `examples/FastLED_NeoMatrix_SmartMatrix_LEDMatrix_GFX_Demos/neomatrix_config.h`: a
   `GFXDISPLAY_M<W>BY<H>` size entry next to the others, and a matching `defaults.*` block (rows, cols,
   chain_length, parallel, pixel_mapper_config, plus `pwm_bits = 7`, `pwm_lsb_nanoseconds = 100`,
   `pwm_dither_bits = 1` or it flickers). Then `mkdir -p /root/NM && echo M<W>BY<H> > /root/NM/gfxdisplay`.
   SynthStitch's frame is `M96BY96`: rows 32, cols 32, chain 3, parallel 3, `Rotate:180`. After changing the size,
   delete each demo's `build/opt/aop/src/main.o` before `make`: the size is a `-D` flag, which make does not track.
4. Patches needed on the Pi:
   - `src/cores/arduino/SerialConsole.cpp`, in `loadData()`: read into an `int` and `break` on `EOF`. Without it, a
     demo started with no terminal (as the menu does) queues EOF forever and hangs before drawing anything.
   - `makeNativeArduino.mk`: comment out `-lX11`, and on ARM filter `XWindow.cpp` and `Touch_LinuxWrapper.cpp` out of
     `SRC_CXX`. `examples/Makefile`: comment out `FastLED_TFTWrapper_GFX`. Then no X11 packages are needed.
5. Build each demo with `make -j3` in its folder (the first one takes a few minutes for FastLED).

## When the panels misbehave, check in this order

The full history is in [TROUBLESHOOTING.md](TROUBLESHOOTING.md). Most of a three-day hunt came down to item 1.

1. **Is the HAT fully seated on the Pi's header?** A heatsink, the Pi 4's PoE pins or a riser can hold it up so some
   pins only touch now and then. Symptoms: some rows never light, rows show in a 2-on-2-off pattern, or a single row
   smears across several rows. Press it down before anything else.
2. **Power.** Panels need their own 5V supply (about 4A per 32×32 panel at full white). Never 12V. One power lead per
   panel. Check `vcgencmd get_throttled` on the Pi (`0x0` is good).
3. **Ribbon into the panel's input connector,** with the ribbon's other end in the HAT port you configured.
4. **Mapping and slowdown flags** (table above).
5. **Rows wrong:** run `row-test` (one row lit on every port) to see where each address really lands. Lit rows in a
   fill test that share a bit pattern point at one address line (A=1, B=2, C=4, D=8 in the row number).
6. **Tracing an address line with a meter:** `toggle-b.sh` blanks the display and flips address line B once a second.
   Ask the person to follow it with a meter from the HAT port to the panel's input buffer chip.
7. **Last resort:** `addr-sweep.sh` (row-addressing schemes) and `mux-sweep.sh` (multiplexing). Standard 1/16-scan
   32×32 panels need neither.

## Gotchas

- **Switch the panel supply off before plugging or unplugging ribbons.** Hot-plugging rebooted the Pi repeatedly.
- **One bad or unpowered panel can garble every port.** The ports share clock, latch and address lines, so a faulty
  panel on one port scrambled the other two. Unplug ports one at a time to find it.
- **Pac-Man, Tetris and the shapes take the visible height as an argument** (96 here). Use the full canvas height
  unless a port is left empty.
- **Only one display program at a time.** Two at once fight over the panels and look dead. `sign` stops the old one
  before starting the next. For manual runs, stop with `pkill -f "^/opt/(rpi-rgb-led-matrix|signtest)/"`.
- **Never `pkill -f <word>` over SSH** if the word is in your own command line. It kills your SSH session.
  `pkill -x` only matches the first 15 characters of a name.
- **On DietPi,** `/boot/config.txt` and `/boot/cmdline.txt` are symlinks. Edit `/boot/firmware/...` or use `sed --follow-symlinks`.
- **A Pi with no internet** keeps a stale clock, so the clock design shows the wrong time until it's set.
- **A Windows checkout** of this repo turns the scripts' line endings into CRLF. `setup.sh` strips them on the Pi.
