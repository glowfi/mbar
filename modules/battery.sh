#!/bin/sh

CHARGE=$(upower -i /org/freedesktop/UPower/devices/battery_BAT0 | grep "percentage" | xargs | awk -F":" '{print $2}' | xargs)
STATUS=$(upower -i /org/freedesktop/UPower/devices/battery_BAT0 | grep "state" | xargs | awk -F":" '{print $2}' | xargs)

if [ "$STATUS" = "Charging" ]; then
	printf "🔌 %s %s" "$CHARGE" "$STATUS"
else
	printf "🔋 %s %s" "$CHARGE" "$STATUS"
fi
