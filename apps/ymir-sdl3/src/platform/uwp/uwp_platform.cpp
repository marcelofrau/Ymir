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
#include <vector>

namespace platform::uwp {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Pickers;

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

    FILE *file = nullptr;
    if (_wfreopen_s(&file, logPath.c_str(), L"w", stdout) != 0) {
        return {};
    }
    if (_wfreopen_s(&file, logPath.c_str(), L"a", stderr) != 0) {
        return {};
    }
    // Flush every line so the log survives crashes and can be read while the app is running
    setvbuf(stdout, nullptr, _IOLBF, 4096);
    setvbuf(stderr, nullptr, _IONBF, 0);
    return logPath;
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

fire_and_forget ShowFileDialogAsync(SDL_FileDialogType type, SDL_DialogFileCallback callback, void *userdata,
                                    std::vector<SDL_DialogFileFilter> filters, bool allowMany) {
    // Pickers must be created and shown on the UI thread; the callback can run on any thread
    DialogResult result{};
    try {
        switch (type) {
        case SDL_FILEDIALOG_OPENFILE: {
            FileOpenPicker picker{};
            picker.ViewMode(PickerViewMode::List);
            picker.SuggestedStartLocation(PickerLocationId::ComputerFolder);
            std::vector<hstring> extensions{};
            for (const auto &filter : filters) {
                if (filter.pattern != nullptr && std::string_view{filter.pattern} == "*") {
                    extensions.clear();
                    break;
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
            picker.SuggestedStartLocation(PickerLocationId::ComputerFolder);
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
            picker.SuggestedStartLocation(PickerLocationId::ComputerFolder);
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
