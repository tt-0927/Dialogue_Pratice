// server/Session.cpp
#include "server/Session.h"

#include <cerrno>
#include <utility>

#include <sys/socket.h>
#include <unistd.h>

namespace dp {

Session::Session(int fd, uint64_t id, std::string peer)
    : fd_(fd), id_(id), peer_(std::move(peer)), lastActive_(std::chrono::steady_clock::now()) {}

Session::~Session() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

ReadResult Session::readAvailable(std::vector<Frame>& frames) {
    char buf[16 * 1024];

    for (;;) {
        const ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);

        if (n > 0) {
            rxBytes_ += static_cast<uint64_t>(n);
            decoder_.append(buf, static_cast<size_t>(n));

            for (;;) {
                Frame frame;
                const FrameDecoder::Result r = decoder_.next(frame);
                if (r == FrameDecoder::NeedMore) break;
                if (r == FrameDecoder::Fatal) return ReadResult::ProtocolError;
                frames.push_back(std::move(frame));
            }
            continue;   // 边沿触发：必须读到 EAGAIN 为止
        }

        if (n == 0) return ReadResult::PeerClosed;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return ReadResult::Ok;
        return ReadResult::Error;
    }
}

void Session::enqueue(const std::string& frame) {
    out_.append(frame);
}

FlushResult Session::flush() {
    const size_t before = out_.totalSent();
    const FlushResult r = out_.flush(fd_);
    txBytes_ += out_.totalSent() - before;
    return r;
}

}  // namespace dp
