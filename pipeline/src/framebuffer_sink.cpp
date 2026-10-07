#include "framebuffer_sink.h"

#ifdef __linux__
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

bool FramebufferSink::present(const VideoFrame& frame,
                              const PoseventFrameResult& result,
                              std::string& error) {
#ifndef __linux__
    (void)frame; (void)result; error = "framebuffer sink requires Linux"; return false;
#else
    const int fd = open(device_.c_str(), O_RDWR);
    if (fd < 0) { error = "cannot open framebuffer: " + device_; return false; }
    fb_var_screeninfo info{};
    fb_fix_screeninfo fixed{};
    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) != 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fixed) != 0) {
        close(fd); error = "cannot query framebuffer"; return false;
    }
    const size_t length = fixed.smem_len;
    auto* mapped = static_cast<uint8_t*>(mmap(nullptr, length, PROT_READ | PROT_WRITE,
                                               MAP_SHARED, fd, 0));
    if (mapped == MAP_FAILED) { close(fd); error = "cannot map framebuffer"; return false; }
    const auto rgb = renderRgb(frame, result);
    const uint32_t width = std::min(info.xres, frame.width);
    const uint32_t height = std::min(info.yres, frame.height);
    const uint32_t bytesPerPixel = info.bits_per_pixel / 8;
    if (bytesPerPixel < 3) {
        munmap(mapped, length); close(fd); error = "unsupported framebuffer pixel format"; return false;
    }
    for (uint32_t y = 0; y < height; ++y) {
        auto* row = mapped + static_cast<size_t>(y) * fixed.line_length;
        for (uint32_t x = 0; x < width; ++x) {
            const auto* source = &rgb[(static_cast<size_t>(y) * frame.width + x) * 3];
            auto* target = row + static_cast<size_t>(x) * bytesPerPixel;
            target[info.red.offset / 8] = source[0];
            target[info.green.offset / 8] = source[1];
            target[info.blue.offset / 8] = source[2];
            if (bytesPerPixel >= 4 && info.transp.length) target[info.transp.offset / 8] = 255;
        }
    }
    munmap(mapped, length); close(fd); return true;
#endif
}
