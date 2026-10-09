# LED Matrix Sign

A sign built from 12 P4 32×32 HUB75 panels salvaged from an Andamiro arcade unit, driven by a Raspberry Pi 4
through a Seengreat RGB Matrix Adapter Board using [rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix).

- [TROUBLESHOOTING.md](TROUBLESHOOTING.md): bring-up log, current status, open issues and reference notes
- [test-programs/](test-programs/): panel diagnostic programs (build on the Pi with `./build.sh`)

## Set up your own sign

1. Fit a driver board (Adafruit Triple LED Matrix Bonnet recommended) **fully seated** on a Raspberry Pi, give the
   panels their own 5V supply, and connect the ribbons with the power off.
2. On the Pi: `git clone https://github.com/SynthStitch/led-matrix && cd led-matrix && sudo sh setup.sh`
3. Set your panel layout in `/opt/signtest/sign.conf` (see [CLAUDE.md](CLAUDE.md)), then run `sign`.

Using Claude Code? Open this repo with it and ask it to set up your Pi. [CLAUDE.md](CLAUDE.md) tells it how.
