#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>

int main() {
    int fbfd = open("/dev/graphics/fb0", O_RDWR);
    if (fbfd == -1) return 1;

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo);
    ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo);

    long screensize = vinfo.yres_virtual * finfo.line_length;
    char* fbp = (char*)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);

    if (fbp == MAP_FAILED) return 1;

    // The screen has 76,800 pixels (240x320)
    long num_pixels = vinfo.xres_virtual * vinfo.yres_virtual;

    // We expect 3 bytes per pixel from FFmpeg instead of 4
    long payload_size = num_pixels * 3;
    char* rx_buffer = new char[payload_size];

    uint64_t frames = 0;

    while (true) {

        ssize_t total_read = 0;

        // Read 3-byte RGB chunks from stdin
        while (total_read < payload_size) {
            ssize_t bytes_read = read(STDIN_FILENO, rx_buffer + total_read, payload_size - total_read);
            if (bytes_read <= 0) {
                sleep(1);
                continue;
            }
            total_read += bytes_read;
        }


        uint8_t* __restrict s = (uint8_t*)rx_buffer;
        uint8_t* __restrict d = (uint8_t*)fbp;

        for (long i = 0; i < num_pixels; ++i) {
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = 0xFF;

            s += 3;
            d += 4;
        }

        frames++;

        if ((frames & 1) == 0) {
            ioctl(fbfd, FBIOPAN_DISPLAY, &vinfo);
        }
    }

    munmap(fbp, screensize);
    close(fbfd);
    delete[] rx_buffer;
    return 0;
}
