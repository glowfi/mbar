#!/bin/sh

output=$(brightnessctl | head -2 | tail -1 | xargs | cut -d '(' -f2 | cut -d ')' -f1)
printf "☀ %s\n" "$output"
