#!/bin/sh
# mango layout module: prints "[SYMBOL] name" for the focused monitor,
# e.g. "[T] tile". Requires MANGO_INSTANCE_SIGNATURE (inherited when the
# bar is autostarted by mango).

sym=$(timeout 1 mmsg get all-monitors 2>/dev/null | tr ',' '\n' | awk -F'"' '
	/"name"/          { act = 0 }
	/"active"/        { if ($0 ~ /true/) act = 1 }
	/"layout_symbol"/ { if (act) { print $4; exit } }
')

[ -z "$sym" ] && exit 0 # no compositor ipc: print nothing

# symbol -> name, mirrors mango src/layout/layout.h
case "$sym" in
T) name="tile" ;;
S) name="scroller" ;;
G) name="grid" ;;
M) name="monocle" ;;
K) name="deck" ;;
CT) name="center_tile" ;;
RT) name="right_tile" ;;
VS) name="vertical_scroller" ;;
VT) name="vertical_tile" ;;
VG) name="vertical_grid" ;;
VK) name="vertical_deck" ;;
DW) name="dwindle" ;;
F) name="fair" ;;
VF) name="vertical_fair" ;;
*) name="" ;;
esac

if [ -n "$name" ]; then
	printf "[%s] %s\n" "$sym" "$name"
else
	printf "[%s]\n" "$sym" # unknown symbol: still show it
fi
