#include "net/Discovery.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace mm2;
using namespace mm2::net;

TEST(Discovery, BroadcastAddressesIncludeLimitedBroadcast) {
    const auto addrs = broadcastAddresses();
    EXPECT_NE(std::ranges::find(addrs, 0xFFFFFFFFu), addrs.end());
}

// Beacon and scanner on loopback (unicast query, no LAN broadcast).
TEST(Discovery, ScannerFindsBeaconOnLoopback) {
    LanBeacon beacon;
    ASSERT_TRUE(beacon.start(0)); // ephemeral port
    ASSERT_NE(beacon.port(), 0);
    beacon.setAnnounceTargets({}); // no unsolicited broadcasts from a unit test
    LanAdvert advert;
    advert.sessionName = "Loopback race";
    advert.hostName = "Tester";
    advert.city = "london";
    advert.players = 2;
    advert.maxPlayers = 8;
    advert.gamePort = 2399;
    beacon.setAdvert(advert);

    LanScanner scanner;
    ASSERT_TRUE(scanner.start(beacon.port(), /*listenForAnnouncements=*/false));
    scanner.setBroadcast(false);
    scanner.addTarget(Address::loopback(0));
    scanner.scan();

    std::vector<DiscoveredSession> found;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (found.empty() && std::chrono::steady_clock::now() < deadline) {
        beacon.update();
        scanner.update();
        found = scanner.sessions();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].advert, advert);
    EXPECT_EQ(found[0].address, (Address{0x7F000001u, 2399}));
    EXPECT_GE(found[0].pingMs, 1u);
    EXPECT_TRUE(found[0].compatible());

    // Entries expire when the host goes away.
    scanner.setExpiry(50);
    beacon.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    scanner.update();
    EXPECT_TRUE(scanner.sessions().empty());
}

TEST(Discovery, IgnoresGarbage) {
    auto sock = UdpSocket::open(Address::loopback(0), false, false);
    ASSERT_TRUE(sock);
    LanScanner scanner;
    ASSERT_TRUE(scanner.start(sock->localAddress().port, false));
    const std::byte garbage[] = {std::byte{'M'}, std::byte{'2'}, std::byte{'L'}, std::byte{'A'}, std::byte{0xFF}};
    scanner.setBroadcast(false);
    scanner.addTarget(Address::loopback(sock->localAddress().port));
    scanner.scan();
    // The fake "host" replies with garbage to whoever queried it.
    std::array<std::byte, 256> buf{};
    Address from;
    for (int i = 0; i < 100 && sock->receiveFrom(from, buf) <= 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ASSERT_NE(from.port, 0);
    sock->sendTo(from, garbage);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    scanner.update();
    EXPECT_TRUE(scanner.sessions().empty());
}
