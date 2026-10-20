#pragma once

/**
@file
@brief UWP (Xbox Dev Mode / Windows) platform integration for the SDL3 frontend.

Only compiled when YMIR_PLATFORM_UWP is set. Implemented with C++/WinRT; callers only need standard and SDL types.
*/

#include <SDL3/SDL_dialog.h>

#include <filesystem>

namespace platform::uwp {

/// @brief Name of the folder that users create at the root of a drive to store the Ymir profile there.
inline constexpr const char *kExternalProfileFolderName = "Ymir";

/// @brief Looks for a writable `<drive>:\Ymir` folder on drives D: to Z: (e.g. a USB drive on Xbox).
/// The folder must already exist; creating it is how users opt into storing the profile outside of LocalState.
/// @return the path to the external profile folder, or an empty path if none was found
std::filesystem::path FindExternalProfilePath();

/// @brief Redirects stdout and stderr (where Ymir's development log is printed) to `ymir-uwp.log` in the
/// app's LocalState folder, since UWP apps have no console.
///
/// The output is also mirrored to the debugger through `OutputDebugStringA`, so that the log shows up in the
/// Visual Studio Output window while debugging and can be copied straight out of there. Mirroring is skipped
/// when no debugger is attached, in which case the writes only go to the file.
///
/// @return the path to the log file, or an empty path if the redirection failed
std::filesystem::path RedirectStdioToLogFile();

/// @brief Makes the app use the whole CoreWindow instead of the TV-safe area on Xbox, which would otherwise leave
/// black borders around the screen. Must be called from the UI thread after the CoreWindow is created (any time
/// inside `main()` on UWP).
void UseFullCoreWindowBounds();

/// @brief Shows a WinRT file or folder picker. Replaces `SDL_ShowFileDialogWithProperties`, which SDL3-uwp does not
/// implement in its CMake build.
///
/// Must be called from the UI thread (the thread that runs `main()` on UWP). Returns immediately; `callback` is
/// invoked later with the same semantics as SDL's dialog callbacks: a null-terminated list of UTF-8 paths, an empty
/// list if the user cancelled, or `nullptr` on error.
///
/// @param[in] type the dialog type
/// @param[in] callback the function to invoke with the selected paths
/// @param[in] userdata the user data pointer to pass to `callback`
/// @param[in] filters the file type filters (ignored for folder dialogs)
/// @param[in] numFilters the number of entries in `filters`
/// @param[in] allowMany whether multiple files can be selected (open file dialogs only)
void ShowFileDialog(SDL_FileDialogType type, SDL_DialogFileCallback callback, void *userdata,
                    const SDL_DialogFileFilter *filters, int numFilters, bool allowMany);

} // namespace platform::uwp
