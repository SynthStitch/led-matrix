#!/bin/sh
# Set up a Raspberry Pi to drive HUB75 panels: hzeller's library, the test programs and the `sign` menu.
# Run on the Pi from the repo folder: sudo sh setup.sh
# Safe to run again; it keeps an existing /opt/signtest/sign.conf.
set -e
[ "$(id -u)" = 0 ] || { echo "Run as root: sudo sh setup.sh"; exit 1; }
LIB=/opt/rpi-rgb-led-matrix
OUT=/opt/signtest
HERE=$(cd "$(dirname "$0")" && pwd)

need=""
for t in g++ make python3; do command -v $t >/dev/null || need="$need $t"; done
[ -d $LIB ] || command -v git >/dev/null || need="$need git"
[ -z "$need" ] || { apt-get update && apt-get install -y $need; }

[ -d $LIB ] || git clone --depth 1 https://github.com/hzeller/rpi-rgb-led-matrix.git $LIB
make -C $LIB/lib
make -C $LIB/examples-api-use

mkdir -p $OUT
cp "$HERE"/test-programs/* $OUT/
sed -i 's/\r$//' $OUT/*.sh $OUT/*.py $OUT/*.cc   # a Windows checkout adds CRLF, which breaks sh
(cd $OUT && sh build.sh)
chmod +x $OUT/*.sh $OUT/sign.py
ln -sf $OUT/sign.py /usr/local/bin/sign
[ -f $OUT/sign.conf ] || cp "$HERE"/sign.conf.example $OUT/sign.conf

# The library shares a hardware timer with onboard audio and refuses to run while the sound driver is loaded.
CFG=/boot/firmware/config.txt; [ -f $CFG ] || CFG=/boot/config.txt
if grep -q '^dtparam=audio=on' $CFG; then
  sed -i --follow-symlinks 's/^dtparam=audio=on/dtparam=audio=off/' $CFG   # DietPi's /boot/config.txt is a symlink
  echo "Turned onboard audio off in $CFG. Reboot once before using the panels."
fi
echo 'blacklist snd_bcm2835' > /etc/modprobe.d/blacklist-rgb-matrix.conf

echo "Done. Set your panel layout in $OUT/sign.conf (see CLAUDE.md), then run: sign"
