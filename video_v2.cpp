#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdint.h>
#include <cstring>

int main() {
    int fbfd = open("/dev/graphics/fb0", O_RDWR);
    if (fbfd == -1) return 1;

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo);
    ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo);

    long screensize = vinfo.yres_virtual * finfo.line_length;
    char* fbp = (char*)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);

    if (fbp == MAP_FAILED) {
        close(fbfd);
        return 1;
    }

    long num_pixels = vinfo.xres_virtual * vinfo.yres_virtual;
    long payload_size = num_pixels * 3;
    char* rx_buffer = new char[payload_size];

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        munmap(fbp, screensize);
        close(fbfd);
        delete[] rx_buffer;
        return 1;
    }

    int rcvbuf = 1024 * 1024; // 1MB buffer
    setsockopt(server_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(5000);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        close(server_fd);
        munmap(fbp, screensize);
        close(fbfd);
        delete[] rx_buffer;
        return 1;
    }

    if (listen(server_fd, 1) < 0) {
        close(server_fd);
        munmap(fbp, screensize);
        close(fbfd);
        delete[] rx_buffer;
        return 1;
    }

    uint64_t frames = 0;

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            // Prevent high-frequency spinning if accept fails
            usleep(100000);
            continue;
        }

        while (true) {
            ssize_t total_read = 0;
            bool connection_lost = false;

            while (total_read < payload_size) {
                ssize_t bytes_read = read(client_fd, rx_buffer + total_read, payload_size - total_read);
                if (bytes_read <= 0) {
                    connection_lost = true;
                    break;
                }
                total_read += bytes_read;
            }

            if (connection_lost) {
                break;
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

        close(client_fd);
    }

    close(server_fd);
    munmap(fbp, screensize);
    close(fbfd);
    delete[] rx_buffer;
    return 0;
}
