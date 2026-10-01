#!/bin/sh
# Show each multiplexing setting for 5 s, labelled with its own number.
cd /opt/signtest
for mx in $(seq 0 24); do
  echo "mux $mx" > /tmp/mux-now
  timeout 5 ./mux-test --led-chain=1 --led-slowdown-gpio=4 --led-brightness=20 --led-multiplexing=$mx "$mx" >/dev/null 2>&1
done
echo done > /tmp/mux-now
