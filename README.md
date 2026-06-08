# Bare-Metal Android Display Pipeline

Transforming a legacy Android smartphone into a dedicated hardware-accelerated display.

This project repurposes an old Android device—specifically the **Samsung Galaxy Star**—into a lightweight external monitor by completely bypassing the Android framework. Components such as **Zygote**, **SurfaceFlinger**, and the standard application stack are removed from the rendering path, allowing raw video frames to be streamed directly into the Linux framebuffer.

Because the Galaxy Star's CPU is incapable of decoding modern video streams efficiently, all decoding is performed on a host machine. The host converts video frames into raw RGB data and streams them over USB via **ADB** to a custom native C++ receiver running on the device.

---

# Prerequisites

* **Rooted Android Device with TWRP**

  * Required for flashing custom boot images and bypassing partition protection.

* **Android NDK**

  * Used to cross-compile native ARMv7 binaries.

* **FFmpeg**

  * Handles video decoding, scaling, rotation, and raw frame streaming.

* **abootimg** and **cpio**

  * Required for unpacking and repacking `boot.img`.

---

# Phase 1 — Boot Image Repacking & Flashing

To execute custom native code during boot, the Android boot image must be modified. The custom service is embedded into the ramdisk and launched directly from `init.rc`.

> **Note:** The Galaxy Star's boot partition (`mmcblk0p5`) is limited to approximately **10 MB**, making binary size optimization important.

## 1. Compile the Custom Boot Service

```bash
# Cross-compile for ARMv7 using the Android NDK
'/run/media/goku/54F2BAD1F2BAB718/SDK/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang++' \
main.cpp \
-o temp_display \
-static-libstdc++

# Copy binary into the ramdisk
rm -I -r ramdisk/sbin/temp_display
cp temp_display ramdisk/sbin/temp_display
chmod 755 ramdisk/sbin/temp_display
```

## 2. Repack the Boot Image

Compress the modified ramdisk and rebuild the boot image.

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

## 3. Flash Through TWRP

The boot partition is often protected while Android is running, so flashing is performed from recovery.

```bash
adb reboot recovery

# Push and flash the custom boot image
adb push custom_boot.img /tmp/custom_boot.img

adb shell dd \
  if=/tmp/custom_boot.img \
  of=/dev/block/mmcblk0p5

adb reboot
```

### Emergency Restore

If the device enters a bootloop or encounters a kernel panic:

```bash
adb push boot.img /tmp/boot.img

adb shell dd \
  if=/tmp/boot.img \
  of=/dev/block/mmcblk0p5
```

---

# Phase 2 — Native Video Receiver

The heart of the project is a lightweight C++ application (`video.cpp`) that continuously reads raw RGB frames from `stdin` and writes them directly into:

```text
/dev/graphics/fb0
```

To avoid compatibility issues with Android's aging dynamic linker and system libraries, the receiver is built as a completely static executable.

## Build the Receiver

```bash
# Compile statically and optimize for size
'/run/media/goku/54F2BAD1F2BAB718/SDK/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang++' \
video.cpp \
-o video_display \
-static \
-O3 \
-mcpu=cortex-a9 \
-mfpu=neon \
-funroll-loops \
-Wl,-s

# Deploy to device
adb push video_display /data/local/bin/video_display

adb shell chmod 755 /data/local/bin/video_display
```

### Startup Behavior

The receiver is registered as a service inside `init.rc`, allowing it to launch automatically during boot and wait silently for incoming video data.

---

# Phase 3 — FFmpeg Streaming Pipeline

Once the device boots to a blank screen, it is ready to receive frames.

FFmpeg performs all video processing on the host machine and streams raw RGB data through ADB:

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

## Command Breakdown

### `-re`

Reads the source at its native frame rate, preventing USB buffer saturation.

### `-stream_loop -1`

Loops playback indefinitely.

### `-pix_fmt rgb24`

Converts frames into 24-bit RGB.

The receiver expands these pixels into 32-bit RGBA internally, reducing USB bandwidth requirements by approximately **25%**.

### `-vf "transpose=1,scale=240:320"`

* Rotates the video 90° clockwise.
* Scales it to the Galaxy Star's native resolution.

### `adb exec-in`

Streams raw binary data directly into the receiver process without terminal character translation or buffering issues.

---

# Result

The Android device effectively becomes a minimalist display appliance:

```text
Video File
    ↓
FFmpeg Decode
    ↓
RGB Frame Stream
    ↓
ADB USB Transport
    ↓
Native Receiver
    ↓
Framebuffer (/dev/graphics/fb0)
    ↓
LCD Panel
```

No Android UI.

No SurfaceFlinger.

No MediaCodec.

No Java Runtime.

Just raw pixels pushed directly into the display pipeline.
