#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <string.h>

void i420_to_rgba8888(uint8_t* yuv, uint8_t* fb, int width, int height, int line_length) {
    int frameSize = width * height;
    uint8_t* y_plane = yuv;
    uint8_t* u_plane = yuv + frameSize;
    uint8_t* v_plane = u_plane + (frameSize / 4);

    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            int yIndex = j * width + i;
            int uvIndex = (j / 2) * (width / 2) + (i / 2);

            int Y = y_plane[yIndex];
            int U = u_plane[uvIndex] - 128;
            int V = v_plane[uvIndex] - 128;

            // Higher precision Full Range (PC) YUV to RGB conversion
            int R = Y + ((V * 359) >> 8);
            int G = Y - ((U * 88 + V * 183) >> 8);
            int B = Y + ((U * 454) >> 8);

            R = R < 0 ? 0 : (R > 255 ? 255 : R);
            G = G < 0 ? 0 : (G > 255 ? 255 : G);
            B = B < 0 ? 0 : (B > 255 ? 255 : B);

            long offset = j * line_length + i * 4;
            fb[offset + 0] = R; // (Or R, depending on your earlier fix)
            fb[offset + 1] = G;
            fb[offset + 2] = B; // (Or B)
            fb[offset + 3] = 255;
        }
    }
}
int main() {
    int fb_fd = open("/dev/graphics/fb0", O_RDWR);
    if (fb_fd < 0) return 1;

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);

    size_t fb_size = vinfo.yres_virtual * finfo.line_length;
    uint8_t* fb_mem = (uint8_t*)mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);

    AMediaCodec* codec = AMediaCodec_createDecoderByType("video/avc");
    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, 240);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, 320);

    AMediaCodec_configure(codec, format, nullptr, nullptr, 0);
    AMediaCodec_start(codec);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(5000);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 1);
    int client_socket = accept(server_fd, nullptr, nullptr);

    uint8_t* stream_buf = (uint8_t*)malloc(1024 * 1024);
    int stream_len = 0;

    while (true) {
        ssize_t bytes_read = recv(client_socket, stream_buf + stream_len, (1024 * 1024) - stream_len, 0);
        if (bytes_read <= 0) break;
        if (stream_len + bytes_read > 1024 * 1024) {
            stream_len = 0;
        }
        stream_len += bytes_read;

        int search_idx = 0;
        while (search_idx < stream_len - 4) {
            if (stream_buf[search_idx] == 0x00 && stream_buf[search_idx+1] == 0x00 &&
                stream_buf[search_idx+2] == 0x00 && stream_buf[search_idx+3] == 0x01) {

                int next_nal = -1;
                for (int i = search_idx + 4; i < stream_len - 3; i++) {
                    if (stream_buf[i] == 0x00 && stream_buf[i+1] == 0x00 &&
                        stream_buf[i+2] == 0x00 && stream_buf[i+3] == 0x01) {
                        next_nal = i;
                        break;
                    }
                }

                if (next_nal != -1) {
                    int nal_size = next_nal - search_idx;

                    ssize_t inIdx = AMediaCodec_dequeueInputBuffer(codec, 2000);
                    if (inIdx >= 0) {
                        size_t bufSize;
                        uint8_t* codecInputBuf = AMediaCodec_getInputBuffer(codec, inIdx, &bufSize);
                        memcpy(codecInputBuf, stream_buf + search_idx, nal_size);
                        AMediaCodec_queueInputBuffer(codec, inIdx, 0, nal_size, 0, 0);
                    }

                    memmove(stream_buf, stream_buf + next_nal, stream_len - next_nal);
                    stream_len -= next_nal;
                    search_idx = 0;
                } else {
                    break;
                }
            } else {
                search_idx++;
            }
        }

        while (true) {
            AMediaCodecBufferInfo info;
            ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(codec, &info, 0);

            if (outIdx >= 0) {
                size_t outSize;
                uint8_t* outBuf = AMediaCodec_getOutputBuffer(codec, outIdx, &outSize);

                i420_to_rgba8888(outBuf, fb_mem, 240, 320, finfo.line_length);
                ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo);

                AMediaCodec_releaseOutputBuffer(codec, outIdx, false);
            } else if (outIdx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                break;
            } else if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                continue;
            } else {
                break;
            }
        }
    }

    return 0;
}
