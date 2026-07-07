// isolated in its own TU: windows.h and raylib.h can't share a translation unit
#include "client/launch_server.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <cctype>
#include <cstring>
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

bool fetchPublicIP(char* out, int cap) {
    out[0] = 0;
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo("api.ipify.org", "80", &hints, &res) != 0 || !res) return false;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { freeaddrinfo(res); return false; }
    DWORD timeout = 2500;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof timeout);
    bool ok = false;
    if (connect(s, res->ai_addr, (int)res->ai_addrlen) == 0) {
        const char* req = "GET / HTTP/1.1\r\nHost: api.ipify.org\r\nConnection: close\r\n\r\n";
        send(s, req, (int)strlen(req), 0);
        char buf[1024];
        int total = 0, n;
        while ((n = recv(s, buf + total, sizeof buf - 1 - total, 0)) > 0) total += n;
        buf[total] = 0;
        const char* body = strstr(buf, "\r\n\r\n");
        if (body) {
            body += 4;
            int i = 0;
            while (i < cap - 1 && body[i] && (isdigit((unsigned char)body[i]) || body[i] == '.')) {
                out[i] = body[i];
                i++;
            }
            out[i] = 0;
            ok = i >= 7;   // shortest valid dotted quad
        }
    }
    closesocket(s);
    freeaddrinfo(res);
    return ok;
}
