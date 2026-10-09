#!/bin/sh
# Show each --led-row-addr-type for 8 s, labelled, to find the panel's addressing scheme.
cd /opt/signtest
for t in 0 1 2 3 4 5 0 1 2 3 4 5; do
  echo "addr $t" > /tmp/addr-now
  timeout 15 ./mux-test --led-rows=32 --led-cols=32 --led-chain=1 --led-parallel=3 --led-gpio-mapping=regular --led-slowdown-gpio=4 --led-brightness=20 --led-row-addr-type=$t "A$t" >/dev/null 2>&1
done
echo done > /tmp/addr-now
