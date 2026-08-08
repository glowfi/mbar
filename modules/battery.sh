#!/bin/sh

BAT=$(ls -d /sys/class/power_supply/BAT* 2>/dev/null | head -1)
[ -z "$BAT" ] && {
	echo "🔌 AC"
	exit 0
}
read -r cap <"$BAT/capacity"
read -r status <"$BAT/status"

case "$status" in
Charging) icon=🔌 ;;
*) icon=🔋 ;;
esac

prof=$(powerprofilesctl get 2>/dev/null)
case "$prof" in
power-saver) p="saver" ;;
balanced) p="bal" ;;
performance) p="perf" ;;
*) p="" ;;
esac

printf '%s %s%% %s%s\n' "$icon" "$cap" "$status" "${p:+ [$p]}"
