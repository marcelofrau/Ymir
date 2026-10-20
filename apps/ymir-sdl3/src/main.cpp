#include "app/app.hpp"

#include <util/os_exception_handler.hpp>
#include <ymir/util/thread_name.hpp>

#include <cxxopts.hpp>
#include <fmt/format.h>

#if defined(__APPLE__)
    #include <objc/message.h>
#endif

#if YMIR_PLATFORM_UWP
    #include <platform/uwp/uwp_platform.hpp>

    // SDL3-uwp provides the WinRT entrypoint, which initializes CoreApplication and calls main() (renamed to SDL_main)
    #include <SDL3/SDL_main.h>

    #include <windows.h>

    #include <filesystem>
#endif

#include <memory>

int main(int argc, char **argv) {
#if YMIR_PLATFORM_UWP
    // Temporary boot tracing: OutputDebugStringA does not depend on the stdio redirect, so these markers show up in
    // the Visual Studio Output window even if the log redirection fails or the app dies before the first devlog.
    OutputDebugStringA("[Ymir boot] main() entered\n");

    // The WinRT entrypoint does not pass any command line arguments
    static char uwpProgramName[] = "Ymir";
    static char *uwpArgv[] = {uwpProgramName, nullptr};
    if (argc < 1 || argv == nullptr) {
        argc = 1;
        argv = uwpArgv;
    }

    // There is no console on UWP; send the development log to LocalState\StrikerX3\Ymir\ymir-uwp.log
    const std::filesystem::path logPath = platform::uwp::RedirectStdioToLogFile();

    // Drain the log pipe on every path out of main(), including early returns and handled exceptions, so that the
    // messages explaining an early exit make it to the log file
    struct LogRedirectGuard {
        ~LogRedirectGuard() {
            platform::uwp::ShutdownLogRedirect();
        }
    } logRedirectGuard{};
    OutputDebugStringA(fmt::format("[Ymir boot] log redirection done: {}\n", logPath.string()).c_str());

    // Draw to the whole screen instead of the TV-safe area on Xbox
    platform::uwp::UseFullCoreWindowBounds();
    OutputDebugStringA("[Ymir boot] core window bounds set\n");
#endif

#if defined(_WIN32)
    // NOTE: Setting the main thread name on Linux and macOS replaces the process name displayed on tools like `top`.
    util::SetCurrentThreadName("Main thread");
#endif

    bool showHelp = false;
    bool enableAllExceptions = false;

    app::CommandLineOptions progOpts{};
    cxxopts::Options options("Ymir", "Ymir - Sega Saturn emulator");
    options.add_options()("d,disc", "Path to Saturn disc image (.ccd, .chd, .cue, .iso, .mds)",
                          cxxopts::value(progOpts.gameDiscPath));
    options.add_options()("p,profile", "Path to profile directory", cxxopts::value(progOpts.profilePath));
    options.add_options()("u,user", "Force user profile",
                          cxxopts::value(progOpts.forceUserProfile)->default_value("false"));
    options.add_options()("h,help", "Display help text", cxxopts::value(showHelp)->default_value("false"));
    options.add_options()("f,fullscreen", "Start in fullscreen mode",
                          cxxopts::value(progOpts.fullScreen)->default_value("false"));
    options.add_options()("P,paused", "Start paused", cxxopts::value(progOpts.startPaused)->default_value("false"));
    options.add_options()("F,fast-forward", "Start in fast-forward mode",
                          cxxopts::value(progOpts.startFastForward)->default_value("false"));
    options.add_options()("D,debug", "Start with debug tracing enabled",
                          cxxopts::value(progOpts.enableDebugTracing)->default_value("false"));
    options.add_options()("E,exceptions", "Capture all unhandled exceptions",
                          cxxopts::value(enableAllExceptions)->default_value("false"));
    options.parse_positional({"disc"});
    options.positional_help("path to disc image");
    options.show_positional_help();

#if !Ymir_LOCAL_BUILD
    try {
#endif
        auto result = options.parse(argc, argv);
        if (showHelp) {
            fmt::println("{}", options.help());
            return 0;
        }

        util::RegisterExceptionHandler(enableAllExceptions);

        auto app = std::make_unique<app::App>();
        return app->Run(progOpts);
#if !Ymir_LOCAL_BUILD
    } catch (const cxxopts::exceptions::exception &e) {
        std::string msg = fmt::format("Failed to parse arguments: {}", e.what());
        fmt::println("{}", msg);
        util::ShowFatalErrorDialog(msg.c_str());
        return -1;
    } catch (const std::system_error &e) {
        std::string msg = fmt::format("System error: {}", e.what());
        fmt::println("{}", msg);
        util::ShowFatalErrorDialog(msg.c_str());
        return e.code().value();
    } catch (const std::exception &e) {
        std::string msg = fmt::format("Unhandled exception: {}", e.what());
        fmt::println("{}", msg);
        util::ShowFatalErrorDialog(msg.c_str());
        return -1;
    #if defined(__APPLE__)
    } catch (id e) {
        SEL sel_reason = sel_registerName("reason");
        id reason = ((id (*)(id, SEL))objc_msgSend)(e, sel_reason); // NSString

        SEL sel_UTF8String = sel_registerName("UTF8String");
        const char *failureReason = ((const char *(*)(id, SEL))objc_msgSend)(reason, sel_UTF8String);
        util::ShowFatalErrorDialog(failureReason);
        return -1;
    #endif
    } catch (...) {
        std::string msg = "Unspecified exception";
        fmt::println("{}", msg);
        util::ShowFatalErrorDialog(msg.c_str());
        return -1;
    }
#endif

    return 0;
}
