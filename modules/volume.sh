#!/bin/sh

VOL=$(pamixer --get-volume 2>/dev/null)
STATE=$(pamixer --get-mute 2>/dev/null)

# no audio server / pamixer missing -> say so and get out
case "$VOL" in
'' | *[!0-9]*)
	printf "🔇 --\n"
	return
	;;
esac

MVOL1=$(amixer -D pulse sget Capture 2>/dev/null | grep 'Left:' | awk -F'[][]' '{ print $2 }' | xargs)
MVOL2=$(amixer -D pulse sget Capture 2>/dev/null | grep 'Mono:' | awk -F'[][]' '{ print $2 }' | xargs)
if [ -n "$MVOL1" ]; then
	MVOL="$MVOL1"
elif [ -n "$MVOL2" ]; then
	MVOL="$MVOL2"
fi
MSTATE=$(amixer -D pulse get Capture 2>/dev/null | sed 5q | tail -1 | awk '{print $NF}' | xargs)

# output volume
if [ "$STATE" = "true" ] || [ "$VOL" -eq 0 ]; then
	printf "🔇"
elif [ "$VOL" -le 33 ]; then
	printf "🔈 %s%%" "$VOL"
elif [ "$VOL" -le 66 ]; then
	printf "🔉 %s%%" "$VOL"
else
	printf "🔊 %s%%" "$VOL"
fi
# mic
if [ "$MSTATE" = "[off]" ] || [ -z "$MVOL" ]; then
	printf "  🎤🔇"
else
	printf "  🎤 %s" "$MVOL"
fi
