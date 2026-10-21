#pragma once

#include <stdio.h>

namespace util {

/// @brief Base class for objects that temporarily silence one of the standard streams.
///
/// The redirection is done at the file descriptor level: the descriptor of the stream is pointed at the null device
/// and restored on destruction. The stream object itself is never closed or replaced, which keeps it usable
/// afterwards and keeps the file descriptor of the log redirect (the pipe the log pump thread reads from) alive.
class StdioSuppressor {
public:
    virtual ~StdioSuppressor();

protected:
    StdioSuppressor(FILE *file);

private:
    FILE *const m_file;
    /// @brief Descriptor of the stream being suppressed.
    int m_fd_target = -1;
    /// @brief Copy of the original descriptor, restored on destruction. Negative when nothing was redirected.
    int m_fd_prev = -1;
    /// @brief Descriptor of the null device used for the redirection. Negative when it could not be opened.
    int m_fd_null = -1;
};

/// @brief Temporarily suppresses stdout for the lifetime of the object.
class StdOutSuppressor final : public StdioSuppressor {
public:
    StdOutSuppressor()
        : StdioSuppressor(stdout) {}
};

/// @brief Temporarily suppresses stderr for the lifetime of the object.
class StdErrSuppressor final : public StdioSuppressor {
public:
    StdErrSuppressor()
        : StdioSuppressor(stderr) {}
};

} // namespace util
