#!/bin/sh
# Arguments: $1=event  $2=peer_id  $3=detail

event=$1
peer_id=$2
detail=$3

logger "Airan-Desk: $event from $peer_id - $detail"

if command -v notify-send >/dev/null 2>&1; then
    notify-send "Airan-Desk" "$event: $peer_id - $detail"
elif command -v osascript >/dev/null 2>&1; then
    osascript -e "display notification \"$detail\" with title \"Airan-Desk\""
fi

if [ -e /sys/class/leds/status/brightness ] && [ -w /sys/class/leds/status/brightness ]; then
    echo 1 > /sys/class/leds/status/brightness
fi
