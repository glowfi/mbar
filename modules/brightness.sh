#!/bin/sh

pct=$(($(brightnessctl get) * 100 / $(brightnessctl max)))
printf '☀ %s%%\n' "$pct"
