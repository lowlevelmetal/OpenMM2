// Parity checks of the order in which MM2 updates its systems (MM2Recomp,
// build 3393): what still runs while the game is paused (asRoot's pause:
// mmGame::Update's fall and water checks, mmHUD::Update's message timer).
// See docs/parity/round3/order.md.

#include "TestData.h"
#include "city/CityData.h"
#include "game/Strings.h"
#include "game/session/Session.h"
#include "vfs/GameSource.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>

using namespace mm2;
using namespace mm2::game;
using namespace mm2::game::session;

namespace {

struct OrderRetail {
    city::CityData london;
    Strings strings;
};

OrderRetail* orderRetail() {
    static std::unique_ptr<OrderRetail> r = []() -> std::unique_ptr<OrderRetail> {
        if (!test::gameData())
            return nullptr;
        auto src = vfs::probeGameSource(std::getenv("OPENMM2_GAME_DATA"));
        auto london = city::loadCity(*test::gameData(), "london");
        if (!src || !london)
            return nullptr;
        auto out = std::make_unique<OrderRetail>();
        out->london = std::move(*london);
        out->strings = Strings::load(*src);
        return out;
    }();
    return r.get();
}

std::unique_ptr<Session> orderSession(GameMode mode, int index) {
    RaceConfig cfg;
    cfg.mode = mode;
    cfg.city = "london";
    cfg.raceIndex = index;
    SessionOptions options;
    options.seed = 5;
    std::string error;
    auto s = Session::create(cfg, orderRetail()->london, *test::gameData(), orderRetail()->strings, &error,
                             options);
    EXPECT_TRUE(s) << error;
    return s;
}

int countEvents(const std::vector<Event>& events, EventType type) {
    int n = 0;
    for (const auto& e : events)
        n += e.type == type;
    return n;
}

constexpr float kDt = 1.0f / 30.0f;

} // namespace

// mmHUD::Update is a node of the game, which asNode::Update visits whether
// or not asRoot is paused: the message counts down through a pause, while
// the countdown (the mode's UpdateGame) and its clocks (mmTimer) stand still.
TEST(OrderParity, PausedFrameCountsTheMessageDownNotTheRace) {
    MM2_REQUIRE_GAME_DATA();
    if (!orderRetail())
        GTEST_SKIP() << "retail data incomplete";
    auto s = orderSession(GameMode::Blitz, 0);
    ASSERT_TRUE(s);
    PlayerState player;
    player.transform = s->playerSpawn();
    s->start();
    // Run the countdown until it shows a line.
    for (int i = 0; i < 400 && s->message().timeLeft == 0.0f; ++i)
        s->update(kDt, player);
    ASSERT_EQ(s->phase(), Phase::Countdown);
    ASSERT_GT(s->message().timeLeft, 0.0f);
    const float clock = s->timeRemaining();
    const float raceTime = s->raceTime();
    const float shownFor = s->message().timeLeft;
    for (float t = 0.0f; t < shownFor + 0.5f; t += kDt)
        s->updatePaused(kDt, player);
    EXPECT_EQ(s->message().timeLeft, 0.0f);
    EXPECT_TRUE(s->message().text.empty());
    EXPECT_EQ(s->phase(), Phase::Countdown);
    EXPECT_EQ(s->timeRemaining(), clock);
    EXPECT_EQ(s->raceTime(), raceTime);
}

// mmGame::Update runs its water check after the IsPaused test that skips
// UpdateGame: the water timer runs on through a pause and the handler fires
// (cruise: mmSingleRoam::HitWaterHandler restarts the game).
TEST(OrderParity, PausedFrameRunsTheWaterAndFallChecks) {
    MM2_REQUIRE_GAME_DATA();
    if (!orderRetail())
        GTEST_SKIP() << "retail data incomplete";
    auto s = orderSession(GameMode::Cruise, -1);
    ASSERT_TRUE(s);
    PlayerState player;
    player.transform = s->playerSpawn();
    s->start();
    s->update(kDt, player);
    (void)s->takeEvents();
    player.inWater = true;
    std::vector<Event> events;
    float restartAt = -1.0f;
    for (float t = 0.0f; t < 6.0f && restartAt < 0.0f; t += kDt) {
        s->updatePaused(kDt, player);
        for (auto& e : s->takeEvents()) {
            events.push_back(e);
            if (e.type == EventType::Restart)
                restartAt = t;
        }
    }
    EXPECT_EQ(countEvents(events, EventType::HitWater), 1);
    EXPECT_EQ(countEvents(events, EventType::Restart), 1);
    EXPECT_NEAR(restartAt, 5.0f, 0.1f); // five seconds in the water, paused or not
    // Falling through the city (mmGame::DropThruCityHandler) too.
    player.inWater = false;
    player.transform.m3.y = -60.0f;
    s->updatePaused(kDt, player);
    EXPECT_EQ(countEvents(s->takeEvents(), EventType::Restart), 1);
}

// Nothing runs before the session starts (mmGame::Init has not run).
TEST(OrderParity, PausedFrameBeforeTheStartDoesNothing) {
    MM2_REQUIRE_GAME_DATA();
    if (!orderRetail())
        GTEST_SKIP() << "retail data incomplete";
    auto s = orderSession(GameMode::Cruise, -1);
    ASSERT_TRUE(s);
    (void)s->takeEvents();
    PlayerState player;
    player.transform = s->playerSpawn();
    player.transform.m3.y = -60.0f;
    s->updatePaused(kDt, player);
    EXPECT_TRUE(s->takeEvents().empty());
}
