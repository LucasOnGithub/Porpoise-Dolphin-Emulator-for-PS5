/* Real ENet UDP traffic through the PS5 socket bridge, using host sockets.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <enet/enet.h>

void test_slippi_ps5_enet(void)
{
    assert(enet_initialize() == 0);
    ENetAddress address = {0};
    assert(enet_address_set_host_ip(&address, "127.0.0.1") == 0);
    ENetHost *server = enet_host_create(&address, 1, 1, 0, 0);
    ENetHost *client = enet_host_create(NULL, 1, 1, 0, 0);
    assert(server && client);
    ENetPeer *peer = enet_host_connect(client, &server->address, 1, 0);
    assert(peer);
    unsigned connected = 0;
    enet_uint32 started = enet_time_get();
    while (connected < 2 && enet_time_get() - started < 3000) {
        ENetEvent event;
        if (enet_host_service(client, &event, 2) > 0 && event.type == ENET_EVENT_TYPE_CONNECT) ++connected;
        if (enet_host_service(server, &event, 2) > 0 && event.type == ENET_EVENT_TYPE_CONNECT) ++connected;
    }
    assert(connected == 2);
    const char payload[] = "Slippi PS5 socket bridge";
    ENetPacket *packet = enet_packet_create(payload, sizeof payload, ENET_PACKET_FLAG_RELIABLE);
    assert(packet && enet_peer_send(peer, 0, packet) == 0);
    enet_host_flush(client);
    int received = 0;
    started = enet_time_get();
    while (!received && enet_time_get() - started < 3000) {
        ENetEvent event;
        if (enet_host_service(server, &event, 2) > 0 && event.type == ENET_EVENT_TYPE_RECEIVE) {
            assert(event.packet->dataLength == sizeof payload);
            assert(memcmp(event.packet->data, payload, sizeof payload) == 0);
            enet_packet_destroy(event.packet);
            received = 1;
        }
        enet_host_service(client, &event, 2);
    }
    assert(received);
    enet_host_destroy(client);
    enet_host_destroy(server);
    enet_deinitialize();
    puts("Real ENet loopback connection and reliable packet passed through PS5 socket bridge");
}
