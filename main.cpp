#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>

int main() {
    int fbfd = open("/dev/graphics/fb0", O_RDWR);
    if (fbfd == -1) {
        while (true) sleep(3600);
    }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        while (true) sleep(3600);
    }
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        while (true) sleep(3600);
    }

    long screensize = vinfo.yres_virtual * finfo.line_length;
    char* fbp = (char*)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);

    if (fbp == MAP_FAILED) {
        while (true) sleep(3600);
    }

    // Paint the buffer solid white
    memset(fbp, 0xFF, screensize);

    // FORCE THE GPU TO REFRESH AND SHOW OUR BUFFER
    vinfo.yoffset = 0;
    vinfo.xoffset = 0;
    if (ioctl(fbfd, FBIOPAN_DISPLAY, &vinfo) == -1) {
        // If pan fails, we still sleep to keep ADB alive
        while (true) sleep(3600);
    }

    // Keep the process alive so init doesn't restart us
    while (true) {
        sleep(10);
    }

    munmap(fbp, screensize);
    close(fbfd);

    return 0;
}
