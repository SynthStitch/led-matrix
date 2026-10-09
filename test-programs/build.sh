#!/bin/sh
# Build the test programs on the Pi against /opt/rpi-rgb-led-matrix.
set -e
L=/opt/rpi-rgb-led-matrix
for p in panel-id color-cycle mux-test row-test pacman; do
  g++ -O2 -I$L/include $p.cc -o $p -L$L/lib -lrgbmatrix -lrt -lm -lpthread
done
