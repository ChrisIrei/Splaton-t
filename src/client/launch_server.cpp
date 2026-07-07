// isolated in its own TU: windows.h and raylib.h can't share a translation unit
#include "client/launch_server.h"
#include <windows.h>
#include <shellapi.h>
#include <string>

bool launchLocalServer() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string dir(path);
    size_t cut = dir.find_last_of("\\/");
    dir = cut == std::string::npos ? "." : dir.substr(0, cut);
    std::string exe = dir + "\\splatont_server.exe";
    HINSTANCE h = ShellExecuteA(nullptr, "open", exe.c_str(), nullptr, dir.c_str(), SW_SHOWMINNOACTIVE);
    return (INT_PTR)h > 32;
}
