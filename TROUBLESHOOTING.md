# LED Matrix Sign: Troubleshooting Log

Bring-up session from 2026-09-29 to 2026-09-30 (overnight). This covers getting a Raspberry Pi 4 and a
Seengreat RGB Matrix Adapter Board driving P4 32×32 HUB75 panels salvaged from an Andamiro arcade unit.

## Status at end of session

| Area | Status |
|---|---|
| Pi 4 access (SSH key, new password) | ✅ Working |
| rpi-rgb-led-matrix built and configured | ✅ Working |
| Pi GPIO output | ✅ Verified with no HAT fitted: all 13 matrix pins follow high and low and both pulls (2026-10-01) |
| Driver board | ✅ **Replaced (2026-10-08)** with an Adafruit Triple LED Matrix Bonnet (product 6358), `--led-gpio-mapping=regular`. A loose panel shows full-screen colour fills on port 3. See [Session 3](#session-3-2026-10-08). |
| Old Seengreat HAT | ❌ **Fault found:** with every Pi pin set low, G1 reads 4.8V (stuck high), and G2 and B read 1.4V (floating). GPIO27 (G1) reads high even while the Pi drives it low. **The bare Pi passes on all 13 pins, so the HAT is faulty.** Replace it. |
| At least one panel shows correct images | ✅ Spinning square and full-panel fills worked |
| Frame panels (`P4-3232-2121-16S`) | ⚠️ Mostly dark, random dots or stripes. Their driver chips are standard (TC7258GN + SM16106SC), so a connection or panel fault is suspected (see [Open issues](#open-issues--next-steps)). |
| Loose 32×32 panels | ℹ️ Same model as the frame panels (`P4-3232-2121-16S-2M-V1.0`, standard 1/16 scan). Their stripes come from the signal fault above, not the panel type. |
| 5V power for the full sign | ❌ Needs an LRS-350-5 (5V 60A) |

---

## Hardware

- **Controller:** Raspberry Pi 4 Model B Rev 1.5, 1 GB of RAM
- **HAT:** Seengreat RGB Matrix Adapter Board, Rev 3.9. It has a DC +5V barrel input, an ON/OFF switch,
  two PWR OUT terminals and one HUB75 output.
- **Panels:** 12 × P4 32×32 HUB75, salvaged from an Andamiro arcade dot-matrix unit
  - Frame panels have the back label `P4-3232-2121-16S-2M-V1.0` (FYF). `2121` is the LED package size and
    `16S` means 1/16 scan.
  - At least some of the loose panels seem to be a different type (see below).
- **Power:**
  - Mean Well LRS-150-12 (12V 12.5A). **Spot LED boards only, never panels.** Its output is
    adjustable only within about 10.2–13.8V, so it can't be set to 5V.
  - Spare Mean Well RS-150-12, also 12V.
  - A temporary 5V brick with a green screw-terminal breakout, used for the panels.
- **Network:** Pi Ethernet to a USB Ethernet adapter on the laptop (`eth1`), with NetworkManager connection sharing turned on.

---

## Pi access

### Problem 1: the laptop couldn't see the Pi over USB
- The cable ran from one of the **Pi's USB-A ports** into the keyboard and then the laptop. A Pi 4's USB-A ports only
  accept devices, so a computer can't see the Pi through them.
- The Pi 4's USB-C port is for power. It only appears on a computer in gadget mode (`dwc2`), which
  wasn't set up.
- **Fix:** use Ethernet instead.

### Problem 2: the Pi wasn't on Wi-Fi
The SD card has **DietPi 10.2.3** (Debian 12) with first-time setup already completed:
- `config.txt` has `dtoverlay=disable-wifi`, DietPi has Wi-Fi disabled, and no network is saved.
- Ethernet is on (DHCP), and SSH (Dropbear) is on.
- `user-data` on the boot partition is left over from an older "adguardpi" install. DietPi ignores it.

**Fix:** Ethernet from the Pi to a USB Ethernet adapter on the laptop. The Pi was found through its automatic IPv6
address (`fe80::…%eth1`), with no DHCP needed.

### Problem 3: unknown login password
- The default `dietpi` password and several guesses were all rejected for `root` and `dietpi`.
- **Fix:** with the SD card in the laptop, the laptop's SSH public key was appended to `/root/.ssh/authorized_keys` on the
  card. After that, the password was changed over SSH with `passwd`.
- Gotcha: a long one-line `pkexec sh -c '…'` command got a line break inserted when pasted, which left `cat` sitting
  there waiting for input. Use short single-line commands.

### Problem 4: the SD card was pulled out while mounted
- `fsck.ext4 -f` came back clean. `fsck.vfat -a` cleared the dirty bit. The boot-sector/backup
  difference at offset 65 is just the dirty flag and is harmless.
- `sudo` through Claude Code's `!` prefix failed because fingerprint auth timed out and there was no terminal for a password.
  Running it in a normal terminal worked.

### Internet for the Pi
```sh
nmcli con modify "Wired connection 2" ipv4.method shared   # laptop side
# Pi gets 10.42.0.101 via DHCP; the laptop is 10.42.0.1
```
To undo it: `nmcli con modify "Wired connection 2" ipv4.method auto`

---

## Pi configuration for rpi-rgb-led-matrix

| Change | File | Why |
|---|---|---|
| `dtparam=audio=on` changed to `off` | `/boot/firmware/config.txt` (backup: `config.txt.bak`) | Onboard audio conflicts with the library |
| `blacklist snd_bcm2835` | `/etc/modprobe.d/blacklist-rgb-matrix.conf` | The driver still loaded with audio off. The library won't run with it loaded. |
| ` isolcpus=3` appended | `/boot/firmware/cmdline.txt` (backup: `cmdline.txt.bak`) | Reserves a CPU core for the display driver to reduce flicker |

Library: `/opt/rpi-rgb-led-matrix` (upstream commit `51d3231`, built with `make -C examples-api-use`).

**Gotcha:** on DietPi, `/boot/config.txt` and `/boot/cmdline.txt` are **links** to `/boot/firmware/`.
`sed -i` on the link replaced it with a plain file, so the first `isolcpus` edit had no effect.
Always edit `/boot/firmware/…` directly. The link has been restored.

**Gotcha:** `pkill -f panel-id` over SSH killed the SSH session itself, because the session's own command line
contained "panel-id". Use `pkill -x <name>`.

Known-good base flags for this HAT:
```
--led-rows=32 --led-cols=32 --led-gpio-mapping=regular --led-slowdown-gpio=4
```

Side note: Tailscale is installed on the Pi, and its package source is listed twice (`dietpi-tailscale.list` and
`tailscale.list`). That's harmless, but apt prints warnings about it.

---

## Power

### Lessons
- **HUB75 panels are 5V.** Never connect the LRS-150-12. With Mean Well, the last number in the model is the output voltage:
  LRS-150-**12** gives 12V, and LRS-350-**5** gives 5V.
- **The Pi can't power panels.** A Pi 4's supply is 5V/3A, of which the Pi uses about 1A, and one 32×32 P4 panel draws up to about 4A on full white.
- **The HAT can power about 1–2 panels at most.** One barrel jack and its circuit traces handle around 5A.
- **Feed each panel its own power lead** from the 5V supply, joined with Wagos. Passing power from panel
  to panel leaves the panels further down short of power. They stay dark but still pass data weakly, powered by a trickle of current from the data lines.
- **Use a single power source.** With the 5V brick in the HAT's DC jack *and* the Pi on laptop USB-C, both supplies were
  connected together through the Pi's 5V rail. The Pi 4 has no protection against power flowing backwards out of its USB-C port. Use one or the other.
- **Laptop USB-C is too weak for the Pi.** It caused the `throttled=0x50000` undervoltage flags.
- **Switch off the panel supply before plugging panels in.** Hot-plugging panels rebooted the Pi several times.

### Measurements
- With the Pi on the same line as the panels, voltage was 4.72V at the panels. That's fine for panels, but the Pi
  flags undervoltage below about 4.63V.
- After rewiring: solid 5V at every panel downstream.

### Plan for the full sign
```
LRS-350-5 ─┬─ branch 1 (14 AWG, ~10A fuse) → panels 1–3
           ├─ branch 2 → panels 4–6
           ├─ branch 3 → panels 7–9
           ├─ branch 4 → panels 10–12
           └─ HAT DC jack (Pi + HAT logic only)
```
The HUB75 ribbons carry data only, and their ground wires also tie the grounds together.

---

## Panel debugging timeline

1. **1 panel, spinning-square demo:** worked. This proved the pin layout (`regular`), panel size and timing.
2. **6 panels as 192×32, Game of Life and volume bars:** only a sharp-edged 1.5-panel area lit.
3. **Panel ID test, 6 panels:** the bottom-left showed magenta "6" (the last in the chain) correctly. #5 showed cyan stripes on its
   left half, and #1–4 were dark.
   - At first this was wrongly put down to "not power". It turned out to be **power**: panels 1–4 weren't wired to 5V.
4. **Single-panel colour tests on several panels:**
   - one showed blue on top and yellow on the bottom when it should have been red
   - one was dark
   - one showed fixed green stripes that **didn't change** whatever was sent. That panel wasn't receiving data. A HUB75 input connector
     was found empty in a photo.
5. **Panels with random dots at a solid 5V:** the frame panels show this no matter which input is used.
6. **Pi GPIO check** (`pinctrl get`) while the display ran: all pins were outputs, row-address pins were switching, and GPIO18 was
   running its hardware pulse. **The Pi side is fine.**
7. **5-panel chain after a new ribbon cable:** every panel showed a 2-rows-lit, 2-rows-dark stripe pattern with wrong colours.
   This was suspected to be a dead "B" address line on the HAT.
8. **HAT meter test** (all 13 signal pins held high with `pinctrl set <gpio> op dh`): pin 3 (B1) read **4V**, and pin 10 (B)
   read about the same. **The dead-B-line theory is not confirmed.** The other pins weren't measured.
9. **Loose panel, 5-panel setting:** solid even green across the whole panel. **With the 1-panel setting: lines.**
   That points to 1/8 scan, where each row expects twice as much data.
10. **Multiplexing sweep 0–24 on that loose panel:** lines on every setting.
11. **Spinning square and 3D cube demos** run on request at the end of the session.

### Session 2 (2026-10-01)

12. **Chain-length fill test (1–5) on a loose panel:** none filled evenly. Yellow and white came out green and cyan, so **red never appeared**.
13. **Meter checks, all pins high:** R1 and R2 read 5V at the HAT and at the far end of the ribbon.
    - Gotcha: looking into the ribbon's loose end, its holes are **mirrored** compared with the connector diagram. The early 1.15V readings came from probing the wrong holes.
    - Gotcha: the pin diagram used numbers pins chip-style (1–8 down the left side, 9–16 up the right), not ribbon-style. Go by the labels.
14. **Signal-speed sweep** (slowdown 4, 5, 6 and 8, yellow fill): identical green stripes every time, so speed isn't the cause.
15. **Back of a loose panel:** the same `P4-3232-2121-16S-2M-V1.0` board as the frame panels.
    - Its connectors are labelled INPUT1 (left, next to the 74HC245 buffers U1 and U2) and INPUT2 (right, the one with the pin labels).
    - Moving the ribbon to INPUT1 didn't change anything.
16. **Seengreat wiki:** the board's Pi pin layout is the standard **"regular"** mapping, so the software settings are correct.
17. **Meter checks, all pins low** (the first time this was done): **G1 = 4.8V, G2 = 1.4V, B = 1.4V**, when all should be 0V.
    - On the Pi, `pinctrl` shows GPIO27 (G1) reading **hi while set to output low**, meaning something is driving that line against the Pi.
    - All matrix pins were then left as inputs to stop the outputs fighting.
    - **This explains the symptoms:** G1 stuck on gives green everywhere, and a floating B address line gives the 2-on, 2-off stripes.
    - The earlier tests missed it because they only ever held every pin high at once.
18. **Next:** run the bare Pi with no HAT and check that GPIO27, 9 and 23 follow what the Pi sets.
    - If they do, the HAT is faulty: replace it.
    - If not, the Pi's GPIO is damaged, possibly from the time the laptop and the 5V adapter were both powering the Pi.

---

### Session 3 (2026-10-08)

New board: Adafruit Triple LED Matrix Bonnet (three HUB75 ports, 74AHCT245 level shifters). Its pinout is the
library's `regular` mapping. A, B, C and D are shared by all three ports.

19. **Fresh install on the AdGuard SD card.** The Pi had no internet, so the compiler packages were downloaded on the
    laptop and installed with `dpkg -i`. The library and these test programs were copied over SSH and built on the Pi.
20. **Pin readback with the Bonnet fitted:** all 13 pins pass, so the Bonnet doesn't drive back into the Pi.
21. **Colour cycle:** red, green, blue and white all correct (the old G1 fault is gone), but only 8 of 32 rows lit.
    `row-test` showed the panel is mounted upside down, and the lit rows were the ones where B and D are both 0.
22. **Ruled out, one at a time:** port (moved to port 3), ribbon (swapped), panel (swapped), Pi GPIO (sampled A–D while
    the display ran: all 16 combinations appear), Bonnet output (B and D read 5V at the port when held high, and B
    flips 0–5V at the port with the ribbon unplugged), E line shorted to GND (driven low, no change), and all six
    `--led-row-addr-type` settings (none fill the panel).
23. Moving the ribbon to the panel's other connector fixed D but not B (2 rows on, 2 off).
24. **Root cause: the Bonnet wasn't fully seated on the Pi header.** A heatsink on the Pi held it up, so some header
    pins (including B, GPIO23, and D, GPIO25) only touched intermittently. **With the Bonnet pressed fully down: full-screen fills.**
    - Lesson: before tracing a panel, check the HAT sits flat on the header. A heatsink, the Pi 4's PoE pins or a
      riser can hold it up. Use a low-profile heatsink or the 2×20 riser header (Adafruit 4079).
    - The 2-rows-on, 2-rows-off pattern in session 1 may have had the same cause.
    - Hot-plugging with the panel powered rebooted the Pi twice more. Switch the panel supply off first.
25. **Six-panel frame working.** It is two rows of three panels, one row per Bonnet port: the top row on port 3 and
    the bottom row on port 2. Both rows are mounted upside down. One 180° rotation fixes the whole image:
    ```
    --led-rows=32 --led-cols=32 --led-chain=3 --led-parallel=3 --led-gpio-mapping=regular --led-slowdown-gpio=4 --led-pixel-mapper=Rotate:180
    ```
    The canvas is 96×96, and only its top 96×64 is visible (port 1 is empty). Moving the ribbons to ports 2 (top)
    and 1 (bottom) would allow `--led-parallel=2` and a 96×64 canvas with nothing drawn off-screen.

## Open issues / next steps

1. **Frame panels (`P4-3232-2121-16S`): the driver chips are standard.** Both were read off a panel:
   - Row driver **TC7258GN** (Fuman): an 8-channel row-select chip that decodes the address lines. It's standard and needs no setup.
   - Colour driver **SM16106SC** (Sunmoon): a 16-channel shift-register-and-latch driver. LCSC's listing points
     to the SM16206S as its equivalent. It's standard and needs no setup.

   So `--led-panel-type=FM6126A` isn't the fix, and the default settings should drive these panels, as the magenta "6"
   panel showed. A frame panel that shows only random dots is most likely **not getting a clean signal**. Check, in order:
   - Test the panel on its own, with the new ribbon straight from the HAT into its **input** connector.
   - Look for bent or recessed pins in the input connector.
   - Try `--led-slowdown-gpio=5`.
   - If it still fails, set it aside as a failed panel.
2. **Loose 1/8-scan panels:** try combinations, not single settings:
   - `--led-chain=2 --led-multiplexing=1..17`. Some 1/8-scan 32×32 panels behave like two chained panels.
   - `--led-row-addr-type=0..5` combined with the multiplexing settings.
3. **Finish the HAT meter test:** measure every signal pin high, then all low (to catch pins stuck high). The HUB75 pinout is in
   [Reference](#reference).
4. **Buy an LRS-350-5** (5V 60A) for the full sign.
5. **Turn Wi-Fi on** (`dietpi-config` → Network Options: Adapters), so the Pi doesn't need the Ethernet cable to the laptop.
6. **Spot LED dimming (optional):**
   - Use a MOSFET rated for 3.3V gate drive (IRLB8721 / IRL3705N), not an IRLZ44N or AO3400 at about 10A.
   - Add a 100Ω gate resistor and a 10kΩ pull-down.
   - Or use a PCA9685 I²C PWM board, which avoids GPIO conflicts with the matrix library.

---

## Test programs

These are in [`test-programs/`](test-programs/) and are built on the Pi with `./build.sh`, which needs the library in `/opt/rpi-rgb-led-matrix`.

| Program | What it shows | Example |
|---|---|---|
| `panel-id` | Each panel a solid colour (1 red, 2 green, 3 blue, 4 yellow, 5 cyan, 6 magenta, then repeating) with its number, counted along the chain and then port by port | `./panel-id --led-chain=6 --led-slowdown-gpio=4 --led-brightness=20` |
| `color-cycle` | The whole display red, then green, blue and white, 4 s each | `./color-cycle --led-chain=1 --led-slowdown-gpio=4 --led-brightness=20` |
| `mux-test` | Red fill, white border and a text label | `./mux-test --led-chain=1 --led-multiplexing=3 "3"` |
| `mux-sweep.sh` | Runs `mux-test` through multiplexing settings 0–24, 5 s each, labelled | `nohup ./mux-sweep.sh &` (progress is in `/tmp/mux-now`) |
| `row-test` | Lights one row (the last argument) in white, on every parallel port. Shows where each address really lands. | `./row-test --led-parallel=3 --led-gpio-mapping=regular 2` |
| `toggle-b.sh` | Blanks the display and flips address line B once a second, for tracing B with a meter | `nohup ./toggle-b.sh 1800 &` |
| `sign.py` | Arrow-key menu for the six-panel frame: demos, clock, scrolling text and tests. What it starts keeps running after you quit. Installed as `sign`. | `ssh -t pi sign` |
| `addr-sweep.sh` | Runs `mux-test` through `--led-row-addr-type` 0–5, 15 s each, twice | `nohup ./addr-sweep.sh &` (progress is in `/tmp/addr-now`) |

Library demos: `examples-api-use/demo -D0` (spinning square), `-D7` (Game of Life), `-D9` (volume bars), `-D12` (3D cube).

---

## Reference

### HUB75 pinout (as on the panel silkscreen) with "regular" GPIO mapping

| Pin | Signal | GPIO | | Pin | Signal | GPIO |
|---|---|---|---|---|---|---|
| 1 | R1 | 11 | | 2 | G1 | 27 |
| 3 | B1 | 7 | | 4 | GND | – |
| 5 | R2 | 8 | | 6 | G2 | 9 |
| 7 | B2 | 10 | | 8 | E / GND | 15 |
| 9 | A | 22 | | 10 | B | 23 |
| 11 | C | 24 | | 12 | D | 25 |
| 13 | CLK | 17 | | 14 | LAT | 4 |
| 15 | OE | 18 | | 16 | GND | – |

### `vcgencmd get_throttled`
- `0x0`: no problems since boot
- `0x50000`: undervoltage **and** throttling have happened at some point since boot. The low bits being 0 means it's not happening now.

### Spot LED boards (Andamiro AZZZ0PCB191)
- 12V, not addressable, 26 white 5050 LEDs: 8 strings of 3 through 68Ω, plus 1 string of 2 through 150Ω.
- About 0.36A (about 4.3W) per board, so the LRS-150-12 can run about 26 boards at an 80% load limit.

### Sources
- [hzeller/rpi-rgb-led-matrix README](https://github.com/hzeller/rpi-rgb-led-matrix/blob/master/README.md)
- [Issue #66: 32×32 panels with 1:8 scan](https://github.com/hzeller/rpi-rgb-led-matrix/issues/66)
- [Issue #910: P4 outdoor panel, lower half not working](https://github.com/hzeller/rpi-rgb-led-matrix/issues/910)
- [Issue #948: P4-2121-64×32-16S-HL1](https://github.com/hzeller/rpi-rgb-led-matrix/issues/948)
- [SmartMatrix: P3-6432-2121-16S-D1.0 panels don't work at all](https://community.pixelmatix.com/t/p3-6432-2121-16s-d1-0-panels-dont-work-at-all/381)

### Result (2026-10-01)

With the **HAT removed**, all 13 matrix GPIOs (4, 7, 8, 9, 10, 11, 17, 18, 22, 23, 24, 25, 27) followed output high and low,
and pull-up and pull-down, correctly. **The Pi's GPIO is fine. The Seengreat HAT drives lines back toward the Pi, so it's faulty.**

- Keep the HAT off the Pi.
- The HAT has **no ON/OFF switch** (an earlier note misread the silkscreen). With the HAT refitted, the pin readback again showed **GPIO27 stuck high** while the other 12 pins followed. **Conclusion: the HAT is faulty.**
- Replacement: Adafruit RGB Matrix Bonnet with `--led-gpio-mapping=adafruit-hat`, or another Seengreat board with `regular`.
- Interim: wire the Pi straight to a panel with jumper wires (the library's "regular" wiring) to confirm the panels work.
  - Use M-F jumpers, from the Pi header into the holes of a ribbon's loose end; the ribbon's other end goes into the panel.
  - Find the GND column with a continuity beep to the panel's GND terminal. If the columns are swapped, Pi outputs end up
    shorted to GND, so check this first.
  - Results so far: the patterns differed from panel to panel with the same wiring. The likely reason: the panel's 74HC245
    inputs need about 3.5V at a 5V supply, and the Pi only gives 3.3V. Trimming the LRS-50-5 down to about 4.5V
    (V ADJ) lowers the threshold to about 3.15V and should give a valid test.

Pin readback used for this test (run on the Pi):
```sh
for g in 4 7 8 9 10 11 17 18 22 23 24 25 27; do
  pinctrl set $g op dh; h=$(pinctrl lev $g); pinctrl set $g op dl; l=$(pinctrl lev $g)
  pinctrl set $g ip pu; u=$(pinctrl lev $g); pinctrl set $g ip pd; d=$(pinctrl lev $g); pinctrl set $g ip pn
  echo "GPIO$g high:$h low:$l pu:$u pd:$d"   # healthy = 1 0 1 0
done
```
