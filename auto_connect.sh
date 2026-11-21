#!/bin/bash

if [ "$EUID" -ne 0 ]; then
    exec sudo "/home/goku/auto_connect.sh"
fi

ADB_BIN="/run/media/goku/WORK/SDK/platform-tools/adb"

while true; do
    if $ADB_BIN devices | grep -q "19761202" && [ -e "/dev/video0" ]; then
        USB_PORT=$($ADB_BIN devices -l | grep "19761202" | grep -o 'usb:[^ ]*' | cut -d':' -f2)

        TARGET_IFACE=""
        if [ -n "$USB_PORT" ]; then
            for IFACE_PATH in /sys/class/net/*; do
                if [ -L "$IFACE_PATH/device" ]; then
                    if readlink "$IFACE_PATH/device" | grep -q "$USB_PORT"; then
                        TARGET_IFACE=$(basename "$IFACE_PATH")
                        break
                    fi
                fi
            done
        fi

        if [ -z "$TARGET_IFACE" ] || ! ip link show "$TARGET_IFACE" > /dev/null 2>&1; then
            sleep 5
            continue
        fi

        nmcli dev set "$TARGET_IFACE" managed no

        ifconfig "$TARGET_IFACE" 192.168.42.1 netmask 255.255.255.0 up

        NETWORK_UP=false
        for i in {1..15}; do
            if ping -c 1 -W 1 192.168.42.2 >/dev/null 2>&1; then
                NETWORK_UP=true
                break
            fi
            sleep 1
        done

        if [ "$NETWORK_UP" = true ]; then
            $ADB_BIN -s 19761202 shell tinymix 6 0
            $ADB_BIN -s 19761202 shell tinymix 14 14

            ffmpeg \
                -nostdin \
                -f v4l2 -i /dev/video0 \
                -map 0:v \
                -c:v libx264 \
                -preset ultrafast \
                -tune zerolatency \
                -pix_fmt yuv420p \
                -g 60 \
                -crf 18 \
                -f h264 \
                tcp://192.168.42.2:5000

            if [ $? -ne 0 ]; then
                $ADB_BIN -s 19761202 reboot
            fi
        else
            $ADB_BIN -s 19761202 reboot
        fi
    fi

    sleep 30
done
