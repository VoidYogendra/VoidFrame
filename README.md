# flash via TWRP recovery
  # go to recovery first cuz main boot is read only
  adb reboot recovery

  # original boot from pc
  adb push boot.img /tmp/boot.img
  adb shell dd if=/tmp/boot.img of=/dev/block/mmcblk0p5
  adb reboot
  
  # custom
  adb push custom_boot.img /tmp/custom_boot.img
  adb shell dd if=/tmp/custom_boot.img of=/dev/block/mmcblk0p5
  adb reboot


# build code and build image
  '/run/media/goku/54F2BAD1F2BAB718/SDK/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang++' main.cpp -o temp_display -static-libstdc++

  rm -I -r ramdisk/sbin/temp_display
  cp temp_display ramdisk/sbin/temp_display
  chmod 755 ramdisk/sbin/temp_display

  cd ramdisk
  find . | cpio -o -H newc | gzip > ../custom_initrd.img
  cd ..
  abootimg --create custom_boot.img -f bootimg.cfg -k zImage -r custom_initrd.img
