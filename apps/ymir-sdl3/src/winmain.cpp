// Win32 WinMain entrypoint that redirects to int main(int argc, char **argv)
// UWP builds use the WinRT entrypoint provided by SDL3-uwp instead (see main.cpp)
#if defined(_WIN32) && !YMIR_PLATFORM_UWP

    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
    #include <shellapi.h>

    #include <string>
    #include <vector>

int main(int argc, char **argv);

int WINAPI WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevIns*/, LPSTR /*lpszArgument*/, int /*iShow*/) {
    // Convert argument list
    int w_argc = 0;
    LPWSTR *w_argv = CommandLineToArgvW(GetCommandLineW(), &w_argc);
    if (w_argv) {
        std::vector<std::string> argv_buf;
        argv_buf.reserve(w_argc);

        for (int i = 0; i < w_argc; ++i) {
            int w_len = lstrlenW(w_argv[i]);
            int len = WideCharToMultiByte(CP_ACP, 0, w_argv[i], w_len, NULL, 0, NULL, NULL);
            std::string s;
            s.resize(len);
            WideCharToMultiByte(CP_ACP, 0, w_argv[i], w_len, &s[0], len, NULL, NULL);
            argv_buf.push_back(s);
        }

        std::vector<char *> argv;
        argv.reserve(argv_buf.size());
        for (std::vector<std::string>::iterator i = argv_buf.begin(); i != argv_buf.end(); ++i)
            argv.push_back((char *)i->c_str());

        int code = main(argv.size(), &argv[0]);

        LocalFree(w_argv);
        return code;
    }

    int code = main(0, NULL);

    LocalFree(w_argv);
    return code;
}

#endif
