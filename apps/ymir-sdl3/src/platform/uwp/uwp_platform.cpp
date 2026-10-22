#include "uwp_platform.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

// C++/WinRT conflicts with the GetCurrentTime macro from the Windows headers
#ifdef GetCurrentTime
    #undef GetCurrentTime
#endif
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <fmt/format.h>

#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <windows.h>

namespace platform::uwp {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Pickers;

// -----------------------------------------------------------------------------
// Logging

namespace {

/// @brief Size of the pipe that carries the redirected stdout/stderr to the log pump thread.
/// Large enough to absorb a burst of log lines while the pump thread is writing to the file.
constexpr int kPipeBufferSize = 64 * 1024;

/// @brief The thread that drains the redirection pipe into the log file. Joined by ShutdownLogRedirect().
/// Intentionally heap-allocated and never deleted: a joinable std::thread with static storage would call
/// std::terminate() from its destructor if the process exits through a path that skips ShutdownLogRedirect().
std::thread *g_logPumpThread = nullptr;

/// @brief The log file written by the pump thread. Closed by ShutdownLogRedirect().
FILE *g_logFile = nullptr;

/// @brief Formats the current local date and time with millisecond precision (`[YYYY-MM-DD HH:MM:SS.mmm] `).
std::string FormatLogTimestamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    return fmt::format("[{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d}] ", st.wYear, st.wMonth, st.wDay, st.wHour,
                       st.wMinute, st.wSecond, st.wMilliseconds);
}

} // namespace

// -----------------------------------------------------------------------------
// Storage

static bool IsWritableDirectory(const std::filesystem::path &path) {
    std::error_code error{};
    if (!std::filesystem::is_directory(path, error)) {
        return false;
    }

    // Actually write a file: some drives are listed but read-only for the app container (missing ACLs)
    const auto probePath = path / ".ymir-write-probe";
    {
        std::ofstream probe{probePath, std::ios::binary | std::ios::trunc};
        if (!probe) {
            return false;
        }
        probe << "ymir";
        if (!probe) {
            return false;
        }
    }
    std::filesystem::remove(probePath, error);
    return true;
}

std::filesystem::path FindExternalProfilePath() {
    for (char drive = 'D'; drive <= 'Z'; ++drive) {
        const std::filesystem::path candidate = std::string{drive} + ":\\" + kExternalProfileFolderName;
        if (IsWritableDirectory(candidate)) {
            return candidate;
        }
    }
    return {};
}

std::filesystem::path RedirectStdioToLogFile() {
    char *prefPath = SDL_GetPrefPath(Ymir_ORGANIZATION_NAME, Ymir_APP_NAME);
    if (prefPath == nullptr) {
        return {};
    }
    std::string prefPathStr = prefPath;
    SDL_free(prefPath);

    const std::filesystem::path logPath = std::filesystem::path{std::u8string{prefPathStr.begin(), prefPathStr.end()}} /
                                          "ymir-uwp.log";

    FILE *logFile = nullptr;
    if (_wfopen_s(&logFile, logPath.c_str(), L"w") != 0 || logFile == nullptr) {
        OutputDebugStringA("[Ymir boot] RedirectStdioToLogFile: _wfopen_s FAILED\n");
        return {};
    }

    // Temporary diagnostics: prove each step of the redirection independently, so a failure is never silent again.
    std::fputs("[Ymir boot] direct file write OK (before redirect)\n", logFile);
    std::fflush(logFile);
    OutputDebugStringA("[Ymir boot] direct file write OK (before redirect)\n");

    // Redirect stdout and stderr into a pipe instead of straight into the file so that a pump thread can mirror
    // every line to the debugger, which is the only way to read the log from the Visual Studio Output window on
    // UWP. Without this, everything written after the redirection is visible only in the log file.
    //
    // The CRT's _pipe is not available to UWP apps, so the pipe comes from the Win32 API and its ends are
    // wrapped in file descriptors for use with the CRT.
    SECURITY_ATTRIBUTES securityAttributes{.nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr,
                                           .bInheritHandle = FALSE};
    HANDLE pipeReadHandle = nullptr;
    HANDLE pipeWriteHandle = nullptr;
    if (!CreatePipe(&pipeReadHandle, &pipeWriteHandle, &securityAttributes, kPipeBufferSize)) {
        OutputDebugStringA("[Ymir boot] RedirectStdioToLogFile: CreatePipe FAILED\n");
        std::fclose(logFile);
        return {};
    }

    const int pipeRead = _open_osfhandle(reinterpret_cast<intptr_t>(pipeReadHandle), _O_RDONLY | _O_BINARY);
    const int pipeWrite = _open_osfhandle(reinterpret_cast<intptr_t>(pipeWriteHandle), _O_WRONLY | _O_BINARY);
    if (pipeRead == -1 || pipeWrite == -1) {
        OutputDebugStringA("[Ymir boot] RedirectStdioToLogFile: _open_osfhandle FAILED\n");
        if (pipeRead != -1) {
            _close(pipeRead);
        } else {
            CloseHandle(pipeReadHandle);
        }
        if (pipeWrite != -1) {
            _close(pipeWrite);
        } else {
            CloseHandle(pipeWriteHandle);
        }
        std::fclose(logFile);
        return {};
    }

    OutputDebugStringA(fmt::format("[Ymir boot] RedirectStdioToLogFile: fds read={} write={} stdout={} stderr={}\n",
                                   pipeRead, pipeWrite, _fileno(stdout), _fileno(stderr))
                           .c_str());

    if (_dup2(pipeWrite, _fileno(stdout)) != 0 || _dup2(pipeWrite, _fileno(stderr)) != 0) {
        OutputDebugStringA("[Ymir boot] RedirectStdioToLogFile: _dup2 FAILED\n");
        _close(pipeRead);
        _close(pipeWrite);
        std::fclose(logFile);
        return {};
    }

    OutputDebugStringA("[Ymir boot] RedirectStdioToLogFile: _dup2 OK\n");

    // The pipe handles are inherited by the redirect; the originals are no longer needed by this process
    _close(pipeWrite);

    // Disable buffering so every line reaches the pipe as soon as it is printed, which keeps the log useful when the
    // app crashes or bails out early. Note that the MSVC CRT does not implement line buffering: _IOLBF behaves like
    // _IOFBF, which kept whole blocks of the log stuck in the buffer and lost them on early exits.
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    // The pump thread runs until every write end of the pipe is closed, which ShutdownLogRedirect() does right
    // before main() returns. It must be joined there instead of detached: a detached thread is killed at process
    // exit before it drains the pipe, which loses the last lines of the log -- usually the ones explaining why the
    // app is exiting.
    g_logFile = logFile;
    g_logPumpThread = new std::thread{[pipeRead, logFile] {
        bool debugging = IsDebuggerPresent();
        OutputDebugStringA("[Ymir boot] log pump thread started\n");

        std::string line{};
        line.reserve(512);

        auto flushLine = [&](std::string_view text) {
            const std::string stamped = FormatLogTimestamp() + std::string{text};
            std::fwrite(stamped.data(), 1, stamped.size(), logFile);
            std::fflush(logFile);
            if (debugging) {
                OutputDebugStringA(stamped.c_str());
            }
        };

        char buffer[1024];
        int count = 0;
        while ((count = _read(pipeRead, buffer, sizeof(buffer))) > 0) {
            for (int i = 0; i < count; ++i) {
                const char ch = buffer[i];
                line.push_back(ch);

                // Mirror the message once a full line is available so that the debugger sees it promptly.
                // Very long lines are flushed in chunks to keep memory bounded.
                if (ch == '\n' || line.size() >= 4096) {
                    flushLine(line);
                    line.clear();
                }
            }
        }

        if (!line.empty()) {
            flushLine(line);
        }
        _close(pipeRead);
    }};

    return logPath;
}

void Trace(std::string_view message) {
    // OutputDebugStringA needs a null-terminated string; the copy also lets the newline be appended
    std::string line = FormatLogTimestamp();
    line.append(message);
    if (line.back() != '\n') {
        line.push_back('\n');
    }
    OutputDebugStringA(line.c_str());

    // g_logFile is only written by RedirectStdioToLogFile(), before any other thread exists, and by
    // ShutdownLogRedirect(), right before main() returns, so reading it here does not race in practice. The CRT locks
    // the stream for each fwrite, so these lines never interleave with the ones written by the log pump thread.
    if (FILE *logFile = g_logFile; logFile != nullptr) {
        std::fwrite(line.data(), 1, line.size(), logFile);
        std::fflush(logFile);
    }
}

void ShutdownLogRedirect() {
    if (g_logPumpThread == nullptr || !g_logPumpThread->joinable()) {
        return;
    }

    // Closing the descriptors behind stdout and stderr closes the last write ends of the pipe, so the pump thread
    // reads end-of-file after draining whatever is still buffered in the pipe and then exits
    std::fflush(stdout);
    std::fflush(stderr);
    _close(_fileno(stdout));
    _close(_fileno(stderr));
    g_logPumpThread->join();

    if (g_logFile != nullptr) {
        std::fclose(g_logFile);
        g_logFile = nullptr;
    }
}

// -----------------------------------------------------------------------------
// Window

void UseFullCoreWindowBounds() {
    try {
        using namespace winrt::Windows::UI::ViewManagement;
        ApplicationView::GetForCurrentView().SetDesiredBoundsMode(ApplicationViewBoundsMode::UseCoreWindow);
    } catch (...) {
        // Not fatal: the app still works within the TV-safe area
    }
}

// -----------------------------------------------------------------------------
// File dialogs

namespace {

/// @brief Splits an SDL filter pattern ("iso;cue;bin" or "*") into WinRT file type filters (".iso", ".cue", ".bin").
/// SDL patterns use ';' as the separator and don't include the leading dot.
void AppendExtensions(std::string_view pattern, std::vector<hstring> &out) {
    while (!pattern.empty()) {
        const size_t sep = pattern.find(';');
        std::string_view ext = pattern.substr(0, sep);
        pattern = sep == std::string_view::npos ? std::string_view{} : pattern.substr(sep + 1);

        while (!ext.empty() && (ext.front() == '*' || ext.front() == '.')) {
            ext.remove_prefix(1);
        }
        if (!ext.empty()) {
            out.push_back(to_hstring(std::string{"."} + std::string{ext}));
        }
    }
}

/// @brief Owns the UTF-8 strings handed to an SDL dialog callback.
struct DialogResult {
    std::vector<std::string> paths;
    std::vector<const char *> pointers;

    void Add(const hstring &path) {
        paths.push_back(to_string(path));
    }

    const char *const *Finish() {
        pointers.clear();
        for (const auto &path : paths) {
            pointers.push_back(path.c_str());
        }
        pointers.push_back(nullptr);
        return pointers.data();
    }
};

/// @brief Sets the picker's start location, ignoring failures.
///
/// The start location is only a hint: some location ids are not available on every platform (the
/// Xbox console in particular), and setting them throws. Failing to set the hint must not prevent
/// the picker from being shown, so the error is swallowed and the picker opens wherever it wants.
template <typename TPicker>
void TrySetStartLocation(TPicker &picker) {
    try {
        picker.SuggestedStartLocation(PickerLocationId::ComputerFolder);
    } catch (...) {
    }
}

fire_and_forget ShowFileDialogAsync(SDL_FileDialogType type, SDL_DialogFileCallback callback, void *userdata,
                                    std::vector<SDL_DialogFileFilter> filters, bool allowMany) {
    // Pickers must be created and shown on the UI thread; the callback can run on any thread
    DialogResult result{};
    try {
        switch (type) {
        case SDL_FILEDIALOG_OPENFILE: {
            FileOpenPicker picker{};
            picker.ViewMode(PickerViewMode::List);
            TrySetStartLocation(picker);
            std::vector<hstring> extensions{};
            for (const auto &filter : filters) {
                if (filter.pattern != nullptr && std::string_view{filter.pattern} == "*") {
                    continue;
                }
                if (filter.pattern != nullptr) {
                    AppendExtensions(filter.pattern, extensions);
                }
            }
            if (extensions.empty()) {
                picker.FileTypeFilter().Append(L"*");
            } else {
                for (const auto &ext : extensions) {
                    picker.FileTypeFilter().Append(ext);
                }
            }

            if (allowMany) {
                auto files = co_await picker.PickMultipleFilesAsync();
                for (const auto &file : files) {
                    result.Add(file.Path());
                }
            } else if (auto file = co_await picker.PickSingleFileAsync()) {
                result.Add(file.Path());
            }
            break;
        }
        case SDL_FILEDIALOG_SAVEFILE: {
            FileSavePicker picker{};
            TrySetStartLocation(picker);
            for (const auto &filter : filters) {
                std::vector<hstring> extensions{};
                if (filter.pattern != nullptr) {
                    AppendExtensions(filter.pattern, extensions);
                }
                if (extensions.empty()) {
                    continue;
                }
                auto choice = single_threaded_vector<hstring>(std::move(extensions));
                picker.FileTypeChoices().Insert(to_hstring(filter.name != nullptr ? filter.name : "Files"), choice);
            }
            if (picker.FileTypeChoices().Size() == 0) {
                // FileSavePicker requires at least one concrete file type
                picker.FileTypeChoices().Insert(L"Binary file", single_threaded_vector<hstring>({L".bin"}));
            }

            if (auto file = co_await picker.PickSaveFileAsync()) {
                result.Add(file.Path());
            }
            break;
        }
        case SDL_FILEDIALOG_OPENFOLDER: {
            FolderPicker picker{};
            TrySetStartLocation(picker);
            picker.FileTypeFilter().Append(L"*");

            if (auto folder = co_await picker.PickSingleFolderAsync()) {
                result.Add(folder.Path());
            }
            break;
        }
        default: callback(userdata, nullptr, -1); co_return;
        }
    } catch (...) {
        callback(userdata, nullptr, -1);
        co_return;
    }

    callback(userdata, result.Finish(), -1);
}

} // namespace

void ShowFileDialog(SDL_FileDialogType type, SDL_DialogFileCallback callback, void *userdata,
                    const SDL_DialogFileFilter *filters, int numFilters, bool allowMany) {
    // Copy the filters since the coroutine outlives the caller's arguments.
    // The strings themselves are owned by the caller's static filter tables.
    std::vector<SDL_DialogFileFilter> filterList{};
    if (filters != nullptr && numFilters > 0) {
        filterList.assign(filters, filters + numFilters);
    }
    ShowFileDialogAsync(type, callback, userdata, std::move(filterList), allowMany);
}

} // namespace platform::uwp
