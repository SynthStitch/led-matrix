#!/bin/sh
# Flip address line B (GPIO23) once a second for metering; display blanked via OE.
pinctrl set 18 op dh; for g in 22 24 25; do pinctrl set $g op dl; done
end=$(($(date +%s) + ${1:-1800}))
while [ $(date +%s) -lt $end ]; do pinctrl set 23 op dh; sleep 1; pinctrl set 23 op dl; sleep 1; done
