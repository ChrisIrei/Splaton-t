#pragma once
// spawns splatont_server.exe from the client's directory in its own console
bool launchLocalServer();
// blocking HTTP query of the machine's public IPv4 (api.ipify.org); ~1s
bool fetchPublicIP(char* out, int cap);
