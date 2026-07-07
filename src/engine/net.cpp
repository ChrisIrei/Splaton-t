#include "engine/net.h"
#include <enet/enet.h>
#include <cstdio>

namespace net {

bool init() { return enet_initialize() == 0; }
void shutdown() { enet_deinitialize(); }

bool Host::serve(uint16_t port, int maxClients) {
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = port;
    h = enet_host_create(&addr, maxClients, 2, 0, 0);
    return h != nullptr;
}

bool Host::connectTo(const std::string& ip, uint16_t port) {
    h = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!h) return false;
    ENetAddress addr;
    if (enet_address_set_host(&addr, ip.c_str()) != 0) { close(); return false; }
    addr.port = port;
    ENetPeer* p = enet_host_connect((ENetHost*)h, &addr, 2, 0);
    if (!p) { close(); return false; }
    // wait up to 3s for the connect handshake
    ENetEvent ev;
    if (enet_host_service((ENetHost*)h, &ev, 3000) > 0 && ev.type == ENET_EVENT_TYPE_CONNECT) {
        clientPeer = p;
        return true;
    }
    enet_peer_reset(p);
    close();
    return false;
}

void Host::poll(std::vector<Event>& out, int timeoutMs) {
    if (!h) return;
    ENetEvent ev;
    int first = 1;
    while (enet_host_service((ENetHost*)h, &ev, first ? timeoutMs : 0) > 0) {
        first = 0;
        Event e;
        e.peer = ev.peer;
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT:
            e.type = Event::CONNECT;
            out.push_back(e);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            e.type = Event::DISCONNECT;
            if (ev.peer == clientPeer) clientPeer = nullptr;
            out.push_back(e);
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            e.type = Event::DATA;
            e.channel = ev.channelID;
            e.data.assign(ev.packet->data, ev.packet->data + ev.packet->dataLength);
            enet_packet_destroy(ev.packet);
            out.push_back(e);
            break;
        default: break;
        }
    }
}

void Host::send(void* peer, const std::vector<uint8_t>& data, bool reliable) {
    if (!peer || data.empty()) return;
    ENetPacket* pkt = enet_packet_create(data.data(), data.size(),
        reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED);
    enet_peer_send((ENetPeer*)peer, reliable ? 0 : 1, pkt);
}

void Host::disconnect(void* peer) {
    if (peer) enet_peer_disconnect((ENetPeer*)peer, 0);
}

void Host::close() {
    if (h) { enet_host_flush((ENetHost*)h); enet_host_destroy((ENetHost*)h); }
    h = nullptr;
    clientPeer = nullptr;
}

std::string peerAddress(void* peer) {
    if (!peer) return "?";
    ENetAddress a = ((ENetPeer*)peer)->address;
    char buf[64];
    snprintf(buf, sizeof buf, "%u.%u.%u.%u:%u",
        a.host & 0xff, (a.host >> 8) & 0xff, (a.host >> 16) & 0xff, (a.host >> 24) & 0xff, a.port);
    return buf;
}

} // namespace net
