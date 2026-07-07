// Splaton't engine — networking wrapper (hides ENet/winsock headers so game
// code can include this next to raylib without windows.h symbol clashes)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace net {

bool init();
void shutdown();

struct Event {
    enum Type { CONNECT, DISCONNECT, DATA } type;
    void* peer = nullptr;
    uint8_t channel = 0;
    std::vector<uint8_t> data;
};

struct Host {
    void* h = nullptr;              // ENetHost*
    void* clientPeer = nullptr;     // set in client mode

    bool serve(uint16_t port, int maxClients);
    bool connectTo(const std::string& ip, uint16_t port);
    // pumps ENet; fills out with events. timeoutMs 0 = non-blocking poll
    void poll(std::vector<Event>& out, int timeoutMs = 0);
    // reliable -> channel 0, unreliable -> channel 1
    void send(void* peer, const std::vector<uint8_t>& data, bool reliable);
    void disconnect(void* peer);
    void close();
    bool connected() const { return clientPeer != nullptr; }
};

std::string peerAddress(void* peer);
uint32_t peerHost(void* peer);   // raw IPv4 for rate limiting

} // namespace net
