#!/usr/bin/env bash

exec 9>/tmp/voidframe_audio.lock
flock -n 9 || exit 0

cleanup() {
    pkill -f "tcp://192.168.42.2:5001"
}
trap cleanup EXIT INT TERM

cleanup

pkexec /home/goku/auto_connect.sh >/dev/null 2>&1 &

adb -s 19761202 wait-for-device
sleep 2
adb -s 19761202 shell tinymix 6 1
adb -s 19761202 shell tinymix 7 1
adb -s 19761202 shell tinymix 14 15

while true; do
    (sleep 2 && adb -s 19761202 shell tinymix 6 1 && adb -s 19761202 shell tinymix 14 11) &
    ffmpeg -f pulse -i "$(pactl get-default-sink).monitor" -af "pan=mono|c0=0.5*c0+0.5*c1,highpass=f=180,acompressor=threshold=-18dB:ratio=4:attack=5:release=50,alimiter=limit=0.85" -acodec pcm_s16le -ar 48000 -ac 2 -f s16le tcp://192.168.42.2:5001
    sleep 1
done
