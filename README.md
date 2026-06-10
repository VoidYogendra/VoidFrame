# Bare-Metal Android Display Pipeline

Transforming a legacy Android smartphone into a dedicated hardware-accelerated display.

This project repurposes an old Android device—specifically the Samsung Galaxy Star—into a lightweight external monitor by bypassing the Android framework entirely. Components such as **Zygote**, **SurfaceFlinger**, and the standard application stack are removed from the rendering path, allowing raw video frames to be streamed directly into the Linux framebuffer.

Since the Galaxy Star's CPU cannot efficiently decode modern video formats, all decoding is performed on a host machine. The host converts video frames into either raw RGB data or H.264 streams and sends them to custom native C++ receivers running directly on the device.

---

# Requirements

* Rooted Android device with TWRP
* Android NDK
* FFmpeg
* abootimg
* cpio
* ADB

---

# Architecture Evolution

| Version | Transport            | Video Format      | Notes                            |
| ------- | -------------------- | ----------------- | -------------------------------- |
| v1      | ADB                  | RGB24 Raw Frames  | Simple implementation            |
| v2      | USB Ethernet (RNDIS) | RGB24 Raw Frames  | ~2× throughput improvement       |
| v3      | USB Ethernet (RNDIS) | H.264 + PCM Audio | Hardware decoding via MediaCodec |

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

rm -rf ramdisk/sbin/temp_display
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

`video_display` receives RGB frames from `stdin` and writes directly to:

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

## Receiver Flow

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

Video is decoded on the host machine and streamed through ADB.

## Streaming Command

```bash
ffmpeg \
-re \
-stream_loop -1 \
-i final.mov \
-f rawvideo \
-pix_fmt rgb24 \
-vf "transpose=1,scale=240:320" \
- | adb exec-in /data/local/bin/video_display
```

## Data Path

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

## Key Options

| Option            | Description                 |
| ----------------- | --------------------------- |
| `-re`             | Play at native frame rate   |
| `-stream_loop -1` | Infinite playback           |
| `-pix_fmt rgb24`  | 24-bit RGB output           |
| `transpose=1`     | Rotate 90° clockwise        |
| `scale=240:320`   | Match panel resolution      |
| `adb exec-in`     | Pipe directly into receiver |

---

# Phase 4 — USB Ethernet (RNDIS)

ADB transport introduces noticeable overhead.

Version 2 replaces ADB with a dedicated USB Ethernet (RNDIS) connection and TCP-based transport.

## Device Network Script

```bash
#!/system/bin/sh

setprop sys.usb.config none
sleep 1

setprop sys.usb.config rndis,adb

for i in $(seq 1 10); do
    if [ -d /sys/class/net/rndis0 ] || \
       [ -d /sys/class/net/usb0 ]; then
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
adb shell 'su -c rm -f /data/local/bin/run.sh'

adb push run.sh /data/local/bin/run.sh

adb shell chmod 755 /data/local/bin/run.sh
```

## Host Configuration

```bash
sudo nmcli dev set enp2s0f0u6 managed no

sudo ifconfig enp2s0f0u6 \
192.168.42.1 \
netmask 255.255.255.0 up
```

## Verify Connectivity

```bash
ping -c 4 192.168.42.2
```

---

# Phase 5 — TCP Framebuffer Receiver (v2)

`video_display_v2` replaces stdin transport with a TCP server listening on port `5000`.

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

## Runtime Flow

1. Open framebuffer device.
2. Memory-map framebuffer.
3. Start TCP server on port `5000`.
4. Wait for incoming connection.
5. Receive complete RGB24 frames.
6. Convert to framebuffer format.
7. Render directly to display.
8. Re-listen automatically after disconnect.

---

# Phase 6 — Raw RGB Streaming over Ethernet (v2)

## Host Setup

```bash
sudo nmcli dev set enp2s0f0u6 managed no

sudo ifconfig enp2s0f0u6 \
192.168.42.1 \
netmask 255.255.255.0 up
```

> ⚠️ Wait until the Android device fully mounts the RNDIS interface before starting FFmpeg. Starting too early can significantly reduce FPS.

## Stream Video File

```bash
sudo nmcli dev set enp2s0f0u6 managed no

sudo ifconfig enp2s0f0u6 \
192.168.42.1 \
netmask 255.255.255.0 up
ffmpeg \
-re \
-stream_loop -1 \
-i future-trunks-powering-up-dragon-ball-moewalls-com.mp4 \
-f rawvideo \
-pix_fmt rgb24 \
-vf "transpose=2,scale=240:320" \
- | nc 192.168.42.2 5000
```

## Stream OBS Virtual Camera

```bash
sudo nmcli dev set enp2s0f0u6 managed no

sudo ifconfig enp2s0f0u6 \
192.168.42.1 \
netmask 255.255.255.0 up
ffmpeg \
-re \
-f v4l2 \
-i /dev/video0 \
-f rawvideo \
-pix_fmt rgb24 \
-vf "transpose=2,scale=240:320" \
- | nc 192.168.42.2 5000
```

## Lower Host CPU Usage

Configure OBS canvas directly to the device resolution and orientation.

```bash
sudo nmcli dev set enp2s0f0u6 managed no

sudo ifconfig enp2s0f0u6 \
192.168.42.1 \
netmask 255.255.255.0 up
ffmpeg \
-re \
-f v4l2 \
-i /dev/video0 \
-f rawvideo \
-pix_fmt rgb24 \
- | nc 192.168.42.2 5000
```

## Data Path

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

---

# Phase 7 — MediaCodec Receiver (v3)

Raw RGB transport provides excellent latency but consumes significant bandwidth.

`video_display_v3` uses Android's hardware video decoder via MediaCodec and optionally supports PCM audio playback using TinyALSA.

> Works exceptionally well for live streams (OBS Virtual Camera). File playback can exhibit additional latency due to encoding and buffering behavior.

## Build (Video + Audio)

```bash
'/run/media/goku/54F2BAD1F2BAB718/SDK/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang++' \
video_display_v3.cpp \
-o video_display_v3 \
-O3 \
-mcpu=cortex-a9 \
-mfpu=neon \
-funroll-loops \
-static-libstdc++ \
-I./tinyalsa/include \
-L. \
-Wl,-rpath=/system/lib \
-Wl,-s \
-lmediandk \
-ltinyalsa
```

## Deploy

```bash
adb push video_display_v3 \
/data/local/bin/video_display_v3

adb shell chmod 755 \
/data/local/bin/video_display_v3
```

---

# Phase 8 — H.264 + Audio Streaming (v3)

## OBS Live Stream

### Video (TCP 5000)

```bash
sudo nmcli dev set enp2s0f0u6 managed no

sudo ifconfig enp2s0f0u6 \
192.168.42.1 \
netmask 255.255.255.0 up
adb shell tinymix 6 1
ffmpeg \
-f v4l2 -i /dev/video0 \
-f alsa -i default \
-map 0:v \
-c:v libx264 \
-preset ultrafast \
-tune zerolatency \
-pix_fmt yuv420p \
-g 60 \
-crf 18 \
-f h264 \
tcp://192.168.42.2:5000 \
-map 1:a \
-c:a pcm_s16le \
-ar 48000 \
-ac 2 \
-f s16le \
tcp://192.168.42.2:5001
```

## Video File Streaming

```bash
sudo nmcli dev set enp2s0f0u6 managed no

sudo ifconfig enp2s0f0u6 \
192.168.42.1 \
netmask 255.255.255.0 up
adb shell tinymix 6 1
ffmpeg \
-re \
-stream_loop -1 \
-i final.mov \
-vf "transpose=2,scale=240:320:flags=bicubic" \
-map 0:v \
-c:v libx264 \
-preset superfast \
-tune zerolatency \
-profile:v baseline \
-crf 12 \
-pix_fmt yuv420p \
-f h264 \
tcp://192.168.42.2:5000 \
-map 0:a \
-c:a pcm_s16le \
-ar 48000 \
-ac 2 \
-f s16le \
tcp://192.168.42.2:5001
```

## Receiver Architecture

```text
TCP H.264 Stream
        ↓
MediaCodec
        ↓
Hardware Decode
        ↓
Framebuffer / Surface
        ↓
LCD Panel

TCP PCM Audio
        ↓
TinyALSA
        ↓
Audio HAL
        ↓
Speaker
```

---

# Phase 9 — Audio Configuration

On the Galaxy Star, audio outputs are disabled by default.

Enable the speaker using:

```bash
adb shell tinymix 6 1
```

## Mixer Controls

```text
Control 6 : Speaker Function
Default   : Off
Enabled   : On
```

Relevant controls:

| Control | Description               |
| ------- | ------------------------- |
| 6       | Speaker Function          |
| 7       | Earpiece Function         |
| 8       | HeadPhone Function        |
| 0       | Master Playback Volume    |
| 5       | HeadPhone Playback Volume |
| 14      | Inter PA Playback Volume  |

---

# Performance Results

## v1 (ADB)

* Direct framebuffer rendering
* No Android UI
* No SurfaceFlinger
* No MediaCodec
* No Java runtime
* Simple deployment

## v2 (RNDIS + Raw RGB)

* Removes ADB overhead
* Approximately 2× higher throughput
* Stable 61 FPS
* Automatic reconnect support
* Lower CPU usage

## v3 (RNDIS + MediaCodec)

* Hardware-accelerated H.264 decoding
* Significantly lower network bandwidth
* Optional audio playback
* Excellent performance for live streams
* More complex transport and synchronization

---

# Final Architecture

```text
Host PC
 ├─ FFmpeg
 │   ├─ Decode
 │   ├─ Scale
 │   ├─ Rotate
 │   └─ Encode (optional)
 │
 └─ TCP Streaming
          │
          ▼
USB Ethernet (RNDIS)
          │
          ▼
video_display_v2 / video_display_v3
          │
          ▼
Framebuffer / MediaCodec
          │
          ▼
LCD Panel
```

The result is a legacy Android smartphone transformed into a dedicated framebuffer-driven display appliance with virtually none of the standard Android graphics stack in the rendering path.
