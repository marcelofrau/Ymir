#include "stdio_suppress.hpp"

#include <ymir/util/dev_log.hpp>

#if defined(_WIN32)
    #include <fcntl.h>
    #include <io.h>

    #define dup _dup
    #define dup2 _dup2
    #define fileno _fileno
    #define close _close
    #define open _open

    constexpr auto kNullFile = "NUL";
    constexpr int kOpenFlags = O_WRONLY | O_BINARY;
#else
    #include <fcntl.h>
    #include <unistd.h>

    constexpr auto kNullFile = "/dev/null";
    constexpr int kOpenFlags = O_WRONLY;
#endif

#include <errno.h>
#include <stdio.h>
#include <string_view>

namespace util {

namespace grp {
    struct stdio_suppress {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "StdioSuppress";
    };
} // namespace grp

StdioSuppressor::StdioSuppressor(FILE *file)
    : m_file(file) {

    m_fd_target = fileno(file);
    if (m_fd_target < 0) {
        return;
    }

    fflush(file);

    // Keep a copy of the current descriptor so it can be restored on destruction.
    m_fd_prev = dup(m_fd_target);
    if (m_fd_prev < 0) {
        return;
    }

    // Point the descriptor at the null device.
    //
    // freopen() is deliberately not used here. It closes the stream when it fails, which leaves fileno() invalid and
    // makes the restore in the destructor assert. That is exactly what happened on the Xbox, where the CRT refused
    // to reopen the stream: the app died in the destructor's _dup2() with an invalid target descriptor.
    m_fd_null = open(kNullFile, kOpenFlags);
    if (m_fd_null < 0) {
        devlog::warn<grp::stdio_suppress>("Cannot open '{}' to suppress output: errno {}", kNullFile, errno);
        close(m_fd_prev);
        m_fd_prev = -1;
        return;
    }

    dup2(m_fd_null, m_fd_target);
}

StdioSuppressor::~StdioSuppressor() {
    if (m_fd_prev >= 0) {
        fflush(m_file);
        dup2(m_fd_prev, m_fd_target);
        close(m_fd_prev);
        m_fd_prev = -1;
    }
    if (m_fd_null >= 0) {
        close(m_fd_null);
        m_fd_null = -1;
    }
}

} // namespace util
