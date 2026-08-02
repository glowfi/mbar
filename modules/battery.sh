#!/bin/sh

BAT=$(ls -d /sys/class/power_supply/BAT* 2>/dev/null | head -1)
[ -z "$BAT" ] && echo "device not on battery"

read -r cap <"$BAT/capacity"
read -r status <"$BAT/status"

case "$status" in
Charging) icon=🔌 ;;
*) icon=🔋 ;;
esac
printf '%s %s%% %s\n' "$icon" "$cap" "$status"
