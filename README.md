# Bare-Metal Android Display Pipeline

Transforming a legacy Android smartphone into a dedicated hardware-accelerated display.

This project repurposes an old Android device—specifically the **Samsung Galaxy Star**—into a lightweight external monitor by bypassing the Android framework entirely. Components such as **Zygote**, **SurfaceFlinger**, and the standard application stack are removed from the rendering path, allowing raw video frames to be streamed directly into the Linux framebuffer.

Since the Galaxy Star's CPU cannot efficiently decode modern video formats, all decoding is performed on a host machine. The host converts video frames into raw RGB data and streams them to a custom native C++ receiver running on the device.

---

# Requirements

* Rooted Android device with TWRP
* Android NDK
* FFmpeg
* `abootimg`
* `cpio`
* ADB

---

# Pipeline Overview

## v1 (ADB Streaming)

```text
Video File
    ↓
FFmpeg Decode
    ↓
RGB24 Frame Stream
    ↓
ADB USB Transport
    ↓
video_display
    ↓
Framebuffer (/dev/graphics/fb0)
    ↓
LCD Panel
```

## v3 (Ethernet over USB)

```text
Video File
    ↓
FFmpeg Decode
    ↓
RGB24 Frame Stream
    ↓
Netcat
    ↓
TCP Socket
    ↓
USB Ethernet (RNDIS)
    ↓
video_display_v2
    ↓
Framebuffer (/dev/graphics/fb0)
    ↓
LCD Panel
```

---

# Phase 1 — Custom Boot Image

The device boots directly into native services by embedding custom binaries inside the ramdisk and launching them from `init.rc`.

> **Note:** The Galaxy Star boot partition (`mmcblk0p5`) is limited to approximately **10 MB**, so binary size matters.

## Build Service

```bash
'/run/media/goku/54F2BAD1F2BAB718/SDK/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang++' \
main.cpp \
-o temp_display \
-static-libstdc++

rm -I -r ramdisk/sbin/temp_display
cp temp_display ramdisk/sbin/temp_display
chmod 755 ramdisk/sbin/temp_display
```

## Repack Boot Image

```bash
cd ramdisk

find . | cpio -o -H newc | gzip -9 > ../custom_initrd.img

cd ..

abootimg \
--create custom_boot.img \
-f bootimg.cfg \
-k zImage \
-r custom_initrd.img
```

## Flash Through TWRP

```bash
adb reboot recovery

adb push custom_boot.img /tmp/custom_boot.img

adb shell dd \
if=/tmp/custom_boot.img \
of=/dev/block/mmcblk0p5

adb reboot
```

## Restore Stock Boot Image

```bash
adb push boot.img /tmp/boot.img

adb shell dd \
if=/tmp/boot.img \
of=/dev/block/mmcblk0p5
```

---

# Phase 2 — Native Framebuffer Receiver (v1)

`video.cpp` receives RGB frames from `stdin` and writes them directly to:

```text
/dev/graphics/fb0
```

The binary is statically linked to avoid dependency issues on older Android versions.

## Build

```bash
'/run/media/goku/54F2BAD1F2BAB718/SDK/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang++' \
video.cpp \
-o video_display \
-static \
-O3 \
-mcpu=cortex-a9 \
-mfpu=neon \
-funroll-loops \
-Wl,-s

adb push video_display /data/local/bin/video_display

adb shell chmod 755 /data/local/bin/video_display
```

### Receiver Flow

```text
stdin
  ↓
RGB24 Frames
  ↓
RGB24 → RGBA8888
  ↓
Framebuffer
  ↓
LCD Panel
```

---

# Phase 3 — ADB Video Streaming (v1)

Stream video directly into the receiver using FFmpeg and ADB.

```bash
ffmpeg \
-re \
-stream_loop -1 \
-i /run/media/goku/6834FB6534FB3522/final.mov \
-f rawvideo \
-pix_fmt rgb24 \
-vf "transpose=1,scale=240:320" \
- | adb exec-in /data/local/bin/video_display
```

## Key Options

| Option            | Description                          |
| ----------------- | ------------------------------------ |
| `-re`             | Play at native frame rate            |
| `-stream_loop -1` | Infinite playback                    |
| `-pix_fmt rgb24`  | 24-bit RGB output                    |
| `transpose=1`     | Rotate 90° clockwise                 |
| `scale=240:320`   | Match display resolution             |
| `adb exec-in`     | Pipe raw data directly into receiver |

---

# Phase 4 — USB Ethernet (RNDIS)

ADB transport works, but introduces additional overhead.

Version 3 replaces ADB with a USB Ethernet (RNDIS) connection and a TCP-based receiver. In testing this provided approximately **2× higher throughput** and maintained a stable **61 FPS** stream.

## Device Network Script

```bash
#!/system/bin/sh

setprop sys.usb.config none
sleep 1

setprop sys.usb.config rndis,adb

for i in $(seq 1 10); do
if [ -d /sys/class/net/rndis0 ] || [ -d /sys/class/net/usb0 ]; then
break
fi
sleep 1
done

sleep 1
ifconfig usb0 192.168.42.2 netmask 255.255.255.0 up 2>/dev/null

sleep 1
ifconfig rndis0 192.168.42.2 netmask 255.255.255.0 up 2>/dev/null
```

## Deploy Script

```bash
adb shell 'su -c rm -r /data/local/bin/run.sh'

adb push run.sh /data/local/bin/run.sh

adb shell chmod 755 /data/local/bin/run.sh
```

## Host Configuration

```bash
sudo ifconfig enp2s0f0u9 \
192.168.42.1 \
netmask 255.255.255.0 up
```

Verify connectivity:

```bash
ping -c 4 192.168.42.2
```

---

# Phase 5 — Network Receiver (v3)

`video_display_v2` replaces stdin-based transport with a TCP server running on port `5000`.

## Build

```bash
'/run/media/goku/54F2BAD1F2BAB718/SDK/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang++' \
video_v2.cpp \
-o video_display_v2 \
-static \
-O3 \
-mcpu=cortex-a9 \
-mfpu=neon \
-funroll-loops \
-Wl,-s

adb push video_display_v2 \
/data/local/bin/video_display_v2

adb shell chmod 755 \
/data/local/bin/video_display_v2
```

## Receiver Architecture

```text
TCP Socket (:5000)
    ↓
RGB24 Frame Buffer
    ↓
RGB24 → RGBA8888
    ↓
Framebuffer Mapping
    ↓
/dev/graphics/fb0
    ↓
LCD Panel
```

### Runtime Behavior

1. Open and map framebuffer memory.
2. Create TCP server on port `5000`.
3. Wait for incoming connection.
4. Receive complete RGB24 frames.
5. Convert to framebuffer format.
6. Render directly to display.
7. Return to listening mode when disconnected.

---

# Phase 6 — Ethernet Video Streaming

Once `video_display_v2` is running and the RNDIS link is active:

```bash
ffmpeg \
-re \
-stream_loop -1 \
-i future-trunks-powering-up-dragon-ball-moewalls-com.mp4 \
-f rawvideo \
-pix_fmt rgb24 \
-vf "transpose=2,scale=240:320" \
- | nc 192.168.42.2 5000
```

## Key Options

| Option                 | Description                |
| ---------------------- | -------------------------- |
| `-pix_fmt rgb24`       | 24-bit RGB transport       |
| `transpose=2`          | Rotate display orientation |
| `scale=240:320`        | Match panel resolution     |
| `nc 192.168.42.2 5000` | Stream directly over TCP   |

---

# Results

### v1 (ADB)

* Direct framebuffer rendering
* No Android UI
* No SurfaceFlinger
* No MediaCodec
* No Java Runtime

### v3 (RNDIS + TCP)

* Removes ADB transport overhead
* ~2× higher throughput
* Stable 61 FPS
* Automatic reconnect support
* Lower CPU usage

---

# Final Architecture

```text
Host PC
 ├─ FFmpeg Decode
 ├─ RGB24 Conversion
 └─ TCP Streaming
          │
          ▼
USB Ethernet (RNDIS)
          │
          ▼
video_display_v2
          │
          ▼
Framebuffer (/dev/graphics/fb0)
          │
          ▼
LCD Panel
```

A legacy Android smartphone is transformed into a dedicated framebuffer-driven display appliance with no Android graphics stack in the rendering path.
