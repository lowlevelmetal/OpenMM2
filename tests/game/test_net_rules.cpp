// The rules a network game's host decides and tells the others (OpenMM2's
// host authority; mmMultiCR::UpdateLimit / SendLimitReached for Cops and
// Robbers' limits, sync review S6).
#include "game/session/CopsAndRobbers.h"

#include <gtest/gtest.h>

using namespace mm2;
using namespace mm2::game::session;

namespace {

CrLocations places() {
    CrLocations loc;
    for (int i = 0; i < 5; ++i)
        loc.points.push_back({100.0f * static_cast<float>(i), 0, 0});
    return loc;
}

} // namespace

TEST(NetRules, AClientCountsTheTimeDownButTheHostEndsTheGame) {
    CrSettings settings;
    settings.timeLimitSeconds = 120.0f;
    settings.limitsFromHost = true;
    CopsAndRobbers client(settings, places());
    client.addCar(1, CrTeam::Robber);
    const std::vector<CopsAndRobbers::Car> cars{{1, CrTeam::Robber, {5000, 0, 0}, false, false}};
    // Past the limit (and through the 1 minute warning) without the host's word.
    for (int i = 0; i < 1300; ++i)
        client.updateNetwork(0.1f, 1, false, cars, {});
    EXPECT_FALSE(client.over());
    EXPECT_LT(client.timeRemaining(), 0.1f);
    bool warned = false;
    for (const auto& e : client.takeEvents()) {
        EXPECT_NE(e.type, CopsAndRobbers::EventType::TimeUp);
        warned = warned || (e.type == CopsAndRobbers::EventType::TimeWarning && e.value == 1);
    }
    EXPECT_TRUE(warned);
    // The host's message ends it, once.
    client.limitReached(CopsAndRobbers::EventType::TimeUp, -1, 0);
    client.limitReached(CopsAndRobbers::EventType::TimeUp, -1, 0);
    EXPECT_TRUE(client.over());
    const auto events = client.takeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, CopsAndRobbers::EventType::TimeUp);
    // Nothing but a limit ends a game.
    CopsAndRobbers other(settings, places());
    other.limitReached(CopsAndRobbers::EventType::GoldTaken, 1, 0);
    EXPECT_FALSE(other.over());
}

TEST(NetRules, AClientLeavesThePointLimitToTheHost) {
    CrSettings settings;
    settings.pointLimit = 100;
    settings.limitsFromHost = true;
    CopsAndRobbers client(settings, places());
    client.addCar(1, CrTeam::Robber);
    client.addCar(2, CrTeam::Robber);
    // Player 2 scores 125 by the host's messages: the client does not end.
    client.receive({CopsAndRobbers::Message::Type::GoldTaken, 2, {}, {}}, 0, false);
    client.receive({CopsAndRobbers::Message::Type::GoldDelivered, 2, {}, {}}, 0, false);
    const std::vector<CopsAndRobbers::Car> cars{{1, CrTeam::Robber, {5000, 0, 0}, false, false}};
    client.updateNetwork(0.1f, 1, false, cars, {});
    EXPECT_EQ(client.playerScore(2), 125);
    EXPECT_FALSE(client.over());
    client.limitReached(CopsAndRobbers::EventType::PointLimit, 2, 125);
    EXPECT_TRUE(client.over());

    // The host itself still ends at the limit.
    settings.limitsFromHost = false;
    CopsAndRobbers host(settings, places());
    host.addCar(1, CrTeam::Robber);
    host.addCar(2, CrTeam::Robber);
    host.receive({CopsAndRobbers::Message::Type::GoldTaken, 2, {}, {}}, 2, true);
    host.receive({CopsAndRobbers::Message::Type::GoldDelivered, 2, {}, {}}, 2, true);
    host.updateNetwork(0.1f, 1, true, cars, {});
    EXPECT_TRUE(host.over());
}
