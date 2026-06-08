#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <stdint.h> // Required for specific integer sizes

int main() {
    int fbfd = open("/dev/graphics/fb0", O_RDWR);
    if (fbfd == -1) { while (true) sleep(3600); }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1) { while (true) sleep(3600); }
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1) { while (true) sleep(3600); }

    long screensize = vinfo.yres_virtual * finfo.line_length;
    char* fbp = (char*)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);

    if (fbp == MAP_FAILED) { while (true) sleep(3600); }

    // 1. Get the color depth from the kernel
    int bpp = vinfo.bits_per_pixel;

    // 2. Define our RGB hex arrays
    // 32-bit colors (A-R-G-B)
    uint32_t colors32[] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF};
    // 16-bit colors (RGB565)
    uint16_t colors16[] = {0xF800, 0x07E0, 0x001F};

    int color_idx = 0;

    // 3. The Infinite Graphics Loop
    while (true) {
        // Paint the correct color based on screen depth
        if (bpp == 32) {
            uint32_t* ptr = (uint32_t*)fbp;
            long num_pixels = screensize / 4; // 4 bytes per pixel
            for (long i = 0; i < num_pixels; i++) {
                ptr[i] = colors32[color_idx];
            }
        } else if (bpp == 16) {
            uint16_t* ptr = (uint16_t*)fbp;
            long num_pixels = screensize / 2; // 2 bytes per pixel
            for (long i = 0; i < num_pixels; i++) {
                ptr[i] = colors16[color_idx];
            }
        }

        // Force the GPU to refresh
        vinfo.yoffset = 0;
        vinfo.xoffset = 0;
        ioctl(fbfd, FBIOPAN_DISPLAY, &vinfo);

        // Cycle the index (0 -> 1 -> 2 -> 0...)
        color_idx = (color_idx + 1) % 3;

        // Sleep for 1 second before changing color
        sleep(1);
    }

    munmap(fbp, screensize);
    close(fbfd);

    return 0;
}
