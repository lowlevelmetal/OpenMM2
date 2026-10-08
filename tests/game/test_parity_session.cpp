// Parity checks of the race rules against MM2's own code (MM2Recomp, build
// 3393): mmWaypoints, mmHUD::ShowSplitTime, the modes' UpdateGame state 0
// and mmSingleStunt's event chaining. See docs/parity/session.md.

#include "TestData.h"
#include "ai/World.h"
#include "city/CityData.h"
#include "game/Profile.h"
#include "game/Strings.h"
#include "game/session/CopsAndRobbers.h"
#include "game/session/RaceSetup.h"
#include "game/session/Session.h"
#include "vfs/GameSource.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string_view>

using namespace mm2;
using namespace mm2::game;
using namespace mm2::game::session;

namespace {

struct ParityRetail {
    city::CityData london, sf;
    Strings strings;
};

ParityRetail* parityRetail() {
    static std::unique_ptr<ParityRetail> r = []() -> std::unique_ptr<ParityRetail> {
        if (!test::gameData())
            return nullptr;
        auto src = vfs::probeGameSource(std::getenv("OPENMM2_GAME_DATA"));
        auto london = city::loadCity(*test::gameData(), "london");
        auto sf = city::loadCity(*test::gameData(), "sf");
        if (!src || !london || !sf)
            return nullptr;
        auto out = std::make_unique<ParityRetail>();
        out->london = std::move(*london);
        out->sf = std::move(*sf);
        out->strings = Strings::load(*src);
        return out;
    }();
    return r.get();
}

std::unique_ptr<Session> paritySession(GameMode mode, int index, const char* city = "london", bool multiplayer = false,
                                       SessionOptions options = {}) {
    RaceConfig cfg;
    cfg.mode = mode;
    cfg.city = city;
    cfg.raceIndex = index;
    cfg.multiplayer = multiplayer;
    options.seed = 11;
    const auto& data = std::string_view(city) == "sf" ? parityRetail()->sf : parityRetail()->london;
    std::string error;
    auto s = Session::create(cfg, data, *test::gameData(), parityRetail()->strings, &error, options);
    EXPECT_TRUE(s) << error;
    return s;
}

Mat34 facing(const Vec3& forward, const Vec3& pos) {
    Mat34 m;
    m.m2 = -forward;
    m.m1 = Vec3::yAxis();
    m.m0 = m.m1.cross(m.m2).normalized();
    m.m3 = pos;
    return m;
}

struct RaceRun {
    Session& s;
    PlayerState player;
    std::vector<OpponentState> opponents;
    std::vector<Event> events;
    float dt = 1.0f / 30.0f;

    explicit RaceRun(Session& session) : s(session) {
        player.transform = s.playerSpawn();
        opponents.resize(s.opponents().size());
        for (std::size_t i = 0; i < opponents.size(); ++i)
            opponents[i].transform = s.opponents()[i].spawn;
    }
    void tick() {
        s.update(dt, player, opponents);
        for (auto& e : s.takeEvents()) {
            if (e.type == EventType::DamageReset)
                player.wrecked = false;
            events.push_back(e);
        }
    }
    void ticks(int n) {
        for (int i = 0; i < n; ++i)
            tick();
    }
    void countdown() {
        s.start();
        for (int i = 0; i < 600 && s.phase() == Phase::Countdown; ++i)
            tick();
    }
    int count(EventType t) const {
        int n = 0;
        for (const auto& e : events)
            n += e.type == t;
        return n;
    }
    // One step of `speed` m/s towards `target`; true once there.
    bool stepTo(const Vec3& target, float speed) {
        const Vec3 p = player.transform.m3;
        Vec3 d = target - p;
        d.y = 0.0f;
        const float len = d.mag();
        const float step = speed * dt;
        const Vec3 dir = len > 1e-4f ? d * (1.0f / len) : -player.transform.m2;
        player.transform = facing(dir, len <= step ? Vec3{target.x, p.y, target.z} : p + dir * step);
        player.speedMph = speed * 2.23694f;
        tick();
        return len <= step;
    }
    void driveTo(const Vec3& target, float speed) {
        while (!stepTo(target, speed) && s.phase() == Phase::Racing) {
        }
    }
};

} // namespace

TEST(ParitySession, FinishStandsFollowTheWaypointType) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(parityRetail());
    // mmWaypoints::LoadCSV: Blitz (type 3) uses pt_check everywhere,
    // checkpoint races (type 2) put pt_finish on the last waypoint, circuits
    // (type 1) on waypoint 0.
    auto blitz = paritySession(GameMode::Blitz, 0);
    ASSERT_TRUE(blitz);
    for (const auto& cp : blitz->checkpoints())
        EXPECT_FALSE(cp.finish);
    auto race = paritySession(GameMode::Checkpoint, 0);
    ASSERT_TRUE(race);
    EXPECT_TRUE(race->checkpoints().back().finish);
    EXPECT_FALSE(race->checkpoints().front().finish);
    auto circuit = paritySession(GameMode::Circuit, 0);
    ASSERT_TRUE(circuit);
    EXPECT_TRUE(circuit->checkpoints().front().finish);
    EXPECT_FALSE(circuit->checkpoints().back().finish);
}

TEST(ParitySession, EveryClearedCheckpointShowsTheSplitTime) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(parityRetail());
    auto s = paritySession(GameMode::Blitz, 0);
    ASSERT_TRUE(s);
    RaceRun r(*s);
    r.countdown();
    ASSERT_EQ(s->phase(), Phase::Racing);
    r.ticks(45); // the "Go!" line has gone
    r.driveTo(s->checkpoints()[1].position, 30.0f);
    ASSERT_EQ(r.count(EventType::CheckpointCleared), 1);
    // mmHUD::ShowSplitTime: GetLocTime of the race time, 1 s, placement 0.
    EXPECT_EQ(s->message().text.size(), 7u) << s->message().text;
    EXPECT_EQ(s->message().text[1], ':');
    EXPECT_FALSE(s->message().top);
    EXPECT_LE(s->message().timeLeft, 1.0f);
    r.ticks(31);
    EXPECT_TRUE(s->message().text.empty());
}

TEST(ParitySession, CircuitCountdownWaitsForThePreRaceCamera) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(parityRetail());
    auto s = paritySession(GameMode::Circuit, 0);
    ASSERT_TRUE(s);
    RaceRun r(*s);
    s->start();
    s->setPreRaceCamera(true);
    r.ticks(120);
    EXPECT_EQ(r.count(EventType::CountdownReady), 0);
    EXPECT_EQ(s->phase(), Phase::Countdown);
    s->setPreRaceCamera(false);
    r.tick();
    EXPECT_EQ(r.count(EventType::CountdownReady), 1);

    // mmSingleBlitz::UpdateGame does not wait for the camera.
    auto blitz = paritySession(GameMode::Blitz, 0);
    ASSERT_TRUE(blitz);
    RaceRun b(*blitz);
    blitz->start();
    blitz->setPreRaceCamera(true);
    b.tick();
    EXPECT_EQ(b.count(EventType::CountdownReady), 1);
}

TEST(ParitySession, MultiplayerRaceWaitsForTheStartAndShowsTheName) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(parityRetail());
    SessionOptions options;
    options.playerName = "Tester";
    auto s = paritySession(GameMode::Checkpoint, 0, "london", true, options);
    ASSERT_TRUE(s);
    RaceRun r(*s);
    s->start();
    s->setStartSignal(false);
    r.ticks(60);
    EXPECT_EQ(r.count(EventType::CountdownReady), 0);
    s->setStartSignal(true);
    r.ticks(80);
    ASSERT_EQ(s->phase(), Phase::Racing);
    // Ready 1.25 s, Set 1.25 s (mmMultiRace::UpdateGame states 1 and 2).
    EXPECT_EQ(r.count(EventType::CountdownGo), 1);
    const auto& cps = s->checkpoints();
    for (std::size_t i = 1; i < cps.size() && s->phase() == Phase::Racing; ++i)
        r.driveTo(cps[i].position, 30.0f);
    r.ticks(3);
    ASSERT_EQ(r.count(EventType::PlayerFinished), 1);
    // The player's name over "finished in M:SS:HH".
    EXPECT_EQ(s->message().text, "Tester");
    EXPECT_EQ(s->message2().text.rfind("finished in ", 0), 0u) << s->message2().text;
}

TEST(ParitySession, LaterExamEventSaysGoOneUpdateLater) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(parityRetail());
    // London Midterm 2 (crash7): a course, then follow the cab.
    auto s = paritySession(GameMode::CrashCourse, 7);
    ASSERT_TRUE(s);
    RaceRun r(*s);
    r.countdown();
    const auto first = s->checkpoints();
    for (std::size_t i = 1; i < first.size() && s->lessonEvent() == 0; ++i)
        r.driveTo(first[i].position, 25.0f);
    while (s->lessonEvent() == 0 && s->phase() == Phase::Racing)
        r.tick();
    ASSERT_EQ(s->lessonEvent(), 1);
    // mmSingleStunt::UpdateChase state 0 for a later event: the car is
    // enabled, then state 2 runs out at once and says "Go".
    EXPECT_EQ(s->phase(), Phase::Countdown);
    r.tick();
    EXPECT_EQ(s->phase(), Phase::Countdown);
    EXPECT_TRUE(s->opponentActive(0));
    r.tick();
    EXPECT_EQ(s->phase(), Phase::Racing);
    EXPECT_EQ(s->message().text, parityRetail()->strings.get(220));
}

TEST(ParitySession, RaceOverFlagCarriesIntoTheNextExamEvent) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(parityRetail());
    // San Francisco Midterm 2 (crash7): destroy the car, then evade. UpdateStop
    // sets the race-over flag before moving on, so the water no longer fails
    // the lesson (mmSingleStunt::HitWaterHandler).
    auto s = paritySession(GameMode::CrashCourse, 7, "sf");
    ASSERT_TRUE(s);
    ASSERT_EQ(s->setup().lessonEvents.size(), 2u);
    ASSERT_EQ(s->setup().lessonEvents[0].type, LessonType::Destroy);
    RaceRun r(*s);
    r.countdown();
    ASSERT_FALSE(r.opponents.empty());
    r.opponents[0].currentDamage = 150000.0f;
    r.ticks(3);
    ASSERT_EQ(s->lessonEvent(), 1);
    r.player.inWater = true;
    r.ticks(200);
    EXPECT_EQ(r.count(EventType::LessonFailed), 0);
    EXPECT_FALSE(s->finished());

    // London final (crash12): follow, then a course. UpdateChase moves on
    // before setting the flag: the water still fails the second event.
    auto f = paritySession(GameMode::CrashCourse, 12);
    ASSERT_TRUE(f);
    ASSERT_EQ(f->setup().lessonEvents[0].type, LessonType::Follow);
    RaceRun g(*f);
    g.countdown();
    ASSERT_FALSE(g.opponents.empty());
    g.opponents[0].finished = true;
    g.opponents[0].transform = facing({0, 0, -1}, g.player.transform.m3 + Vec3{3, 0, 0});
    g.ticks(4);
    ASSERT_EQ(f->lessonEvent(), 1);
    g.ticks(3);
    g.player.inWater = true;
    g.ticks(200);
    EXPECT_EQ(g.count(EventType::LessonFailed), 1);
}

TEST(ParitySession, CleanLessonWreckedAtTheFinishFails) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(parityRetail());
    // San Francisco "River Dancing" (crash8, frogger): UpdateFrogger checks
    // the damage after the finish in the same frame.
    auto s = paritySession(GameMode::CrashCourse, 8, "sf");
    ASSERT_TRUE(s);
    RaceRun r(*s);
    r.countdown();
    ASSERT_EQ(s->phase(), Phase::Racing);
    const auto cps = s->checkpoints();
    // 1.6 km in a 36 s limit.
    for (std::size_t i = 1; i < cps.size(); ++i) {
        while (s->checkpointsCleared() < s->checkpointsTotal() && s->phase() == Phase::Racing &&
               !r.stepTo(cps[i].position, 80.0f)) {
        }
        if (s->checkpointsCleared() == s->checkpointsTotal())
            break;
    }
    ASSERT_EQ(s->checkpointsCleared(), s->checkpointsTotal());
    ASSERT_EQ(s->phase(), Phase::Racing); // the rules see it next frame
    r.player.wrecked = true;
    r.tick();
    EXPECT_EQ(r.count(EventType::LessonPassed), 1);
    EXPECT_EQ(r.count(EventType::LessonFailed), 1);
    EXPECT_EQ(s->message().text, parityRetail()->strings.get(230));
    r.ticks(160);
    EXPECT_TRUE(s->finished());
    EXPECT_FALSE(s->result().won);
}

TEST(ParitySession, EvadeTurnsTheMapOnDuringItsFirstLine) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(parityRetail());
    auto s = paritySession(GameMode::CrashCourse, 10); // London "The Heat Is On"
    ASSERT_TRUE(s);
    ASSERT_EQ(s->currentLesson()->type, LessonType::Evade);
    RaceRun r(*s);
    s->start();
    bool sawIt = false;
    for (int i = 0; i < 400 && s->phase() == Phase::Countdown; ++i) {
        r.tick();
        sawIt |= s->wantsMap();
    }
    EXPECT_TRUE(sawIt);
    EXPECT_FALSE(s->wantsMap());

    auto blitz = paritySession(GameMode::Blitz, 0);
    ASSERT_TRUE(blitz);
    RaceRun b(*blitz);
    blitz->start();
    for (int i = 0; i < 100; ++i) {
        b.tick();
        EXPECT_FALSE(blitz->wantsMap());
    }
}

TEST(ParitySession, ModesPlayTheirSounds) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(parityRetail());
    auto s = paritySession(GameMode::Blitz, 0);
    ASSERT_TRUE(s);
    RaceRun r(*s);
    r.countdown();
    auto sounds = [&](GameSound g) {
        int n = 0;
        for (const auto& e : r.events)
            n += e.type == EventType::Sound && e.index == static_cast<int>(g) && e.value == 0.0f;
        return n;
    };
    // mmSingleBlitz::UpdateGame: "Startracelow" with each countdown line,
    // "Startracehigh" with "Go!".
    EXPECT_EQ(sounds(GameSound::StartRaceLow), 2);
    EXPECT_EQ(sounds(GameSound::StartRaceHigh), 1);
    const auto& cps = s->checkpoints();
    for (std::size_t i = 1; i < cps.size() && s->phase() == Phase::Racing; ++i)
        r.driveTo(cps[i].position, 30.0f);
    r.ticks(3);
    // mmWaypoints::DisplayHUDMessage plays "Waypoint" for every Blitz checkpoint;
    // the win plays "Endofracetag".
    EXPECT_EQ(sounds(GameSound::Waypoint), static_cast<int>(cps.size()) - 1);
    EXPECT_EQ(sounds(GameSound::EndOfRaceTag), 1);
    EXPECT_STREQ(gameSoundName(GameSound::DamageLose), "Damgelose");
}

TEST(ParitySession, OpponentsFinishingLaterGetTheRunningTime) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(parityRetail());
    RaceConfig cfg;
    cfg.mode = GameMode::Checkpoint;
    cfg.city = "london";
    cfg.raceIndex = 0;
    cfg.opponents = 3;
    std::string error;
    auto s = Session::create(cfg, parityRetail()->london, *test::gameData(), parityRetail()->strings, &error);
    ASSERT_TRUE(s) << error;
    ASSERT_FALSE(s->opponents().empty());
    RaceRun r(*s);
    r.countdown();
    EXPECT_EQ(s->playerHold(), PlayerHold::None);
    const auto& cps = s->checkpoints();
    for (std::size_t i = 1; i < cps.size() && s->phase() == Phase::Racing; ++i)
        r.driveTo(cps[i].position, 40.0f);
    ASSERT_EQ(s->phase(), Phase::PostRace);
    // mmSingleRace::UpdateGame: the finish sets mmPlayer +0x2258.
    EXPECT_EQ(s->playerHold(), PlayerHold::FinishBrake);
    const float playerTime = s->result().timeSeconds;
    // UpdateOpponentStatus goes on after the player's finish, and the time
    // is mmHUD's other timer, which the finish does not stop.
    r.ticks(60);
    r.opponents[0].finished = true;
    r.tick();
    const auto res = s->result();
    ASSERT_EQ(res.standings.size(), 2u);
    EXPECT_EQ(res.standings[0].opponent, -1);
    EXPECT_EQ(res.standings[1].opponent, 0);
    EXPECT_EQ(res.standings[1].place, 2);
    EXPECT_NEAR(res.standings[1].timeSeconds, playerTime + 61.0f * r.dt, 0.05f);
    EXPECT_FLOAT_EQ(res.timeSeconds, playerTime);
}

TEST(ParitySession, EndingsHoldThePlayersCarAsTheModesDo) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(parityRetail());
    {
        // DisableRacers before "Go!"; a wreck ends the race with
        // vehCar::SetDrivable(0, 1) (mmSingleRace::UpdateGame state 3).
        auto s = paritySession(GameMode::Checkpoint, 0);
        ASSERT_TRUE(s);
        RaceRun r(*s);
        s->start();
        EXPECT_EQ(s->playerHold(), PlayerHold::Undrivable);
        r.countdown();
        EXPECT_EQ(s->playerHold(), PlayerHold::None);
        r.player.wrecked = true;
        r.tick();
        ASSERT_EQ(s->phase(), Phase::PostRace);
        EXPECT_EQ(s->playerHold(), PlayerHold::Undrivable);
        EXPECT_TRUE(s->playerHeld());
        // ... and silences the engine (vehCarAudioContainer::SilenceEngine).
        EXPECT_TRUE(s->damagedOut());
        EXPECT_TRUE(s->engineSilenced());
        s->restart();
        EXPECT_FALSE(s->engineSilenced());
    }
    {
        // mmSingleRace::HitWaterHandler only switches the state.
        auto s = paritySession(GameMode::Checkpoint, 0);
        ASSERT_TRUE(s);
        RaceRun r(*s);
        r.countdown();
        r.player.inWater = true;
        for (int i = 0; i < 400 && s->phase() == Phase::Racing; ++i)
            r.tick();
        ASSERT_EQ(s->phase(), Phase::PostRace);
        EXPECT_EQ(s->playerHold(), PlayerHold::None);
        EXPECT_FALSE(s->damagedOut());
        EXPECT_TRUE(s->engineSilenced());
    }
}

TEST(ParitySession, ModesCueTheAnnouncer) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(parityRetail());
    auto s = paritySession(GameMode::Blitz, 0);
    ASSERT_TRUE(s);
    RaceRun r(*s);
    auto cues = [&](SpeechCue c) {
        int n = 0;
        for (const auto& e : r.events)
            n += e.type == EventType::Speech && e.index == static_cast<int>(c);
        return n;
    };
    // mmGame::Reset: mmRaceSpeech::PlayPreRace.
    r.countdown();
    EXPECT_EQ(cues(SpeechCue::PreRace), 1);
    const auto& cps = s->checkpoints();
    for (std::size_t i = 1; i < cps.size() && s->phase() == Phase::Racing; ++i)
        r.driveTo(cps[i].position, 30.0f);
    // RegisterFinish -> mmGameSingle::UpdateRewards: PlayResults(1, ...).
    ASSERT_EQ(cues(SpeechCue::Results), 1);
    for (const auto& e : r.events) {
        if (e.type == EventType::Speech && e.index == static_cast<int>(SpeechCue::Results)) {
            EXPECT_EQ(e.value, 1.0f);
        }
    }
    s->restart();
    r.tick();
    EXPECT_EQ(cues(SpeechCue::PreRace), 2);
}

// aiMap::Update runs the light sets after the racers and police: the race
// defers them, and they then advance by the steps the update took.
TEST(ParitySession, DeferredLightsCatchUp) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(parityRetail());
    const auto& city = parityRetail()->london;
    ai::Settings settings;
    auto a = ai::World::create(city, *test::gameData(), settings);
    auto b = ai::World::create(city, *test::gameData(), settings);
    ASSERT_TRUE(a && b);
    b->setLightsDeferred(true);
    const Vec3 pos = city.aiMap->intersections[1].center;
    for (int i = 0; i < 90; ++i) {
        a->update(1.0f / 30.0f, pos, {});
        b->update(1.0f / 30.0f, pos, {});
        b->updateLights();
    }
    ASSERT_EQ(a->signals().size(), b->signals().size());
    for (std::size_t i = 0; i < a->signals().size(); ++i)
        EXPECT_EQ(a->signals()[i].state, b->signals()[i].state) << i;
}

// mmGame::RespawnXYZ never starts a cruise in a room whose level flags
// (CityData::levelRoomFlags, lvlRoomInfo) are subterranean, covered, water of
// death or a terrain instance's (0x2E).
TEST(ParitySession, CruiseStartsAvoidFlaggedRooms) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(parityRetail());
    const auto& city = parityRetail()->london;
    ASSERT_TRUE(city.aiMap);
    ASSERT_EQ(city.levelRoomFlags.size(), city.psdl.rooms.size());
    std::uint32_t rng = 1;
    for (int i = 0; i < 200; ++i) {
        const auto start = randomIntersectionStart(city, rng);
        ASSERT_TRUE(start);
        const city::AiIntersection* found = nullptr;
        for (const auto& x : city.aiMap->intersections)
            if (x.center.x == start->x && x.center.z == start->z && x.center.y + 2.0f == start->y)
                found = &x;
        ASSERT_NE(found, nullptr) << i;
        ASSERT_LT(found->room, city.levelRoomFlags.size());
        EXPECT_EQ(city.levelRoomFlags[found->room] & 0x2E, 0) << found->room;
    }
}

// RegisterFinish registers a finish only under the race table's settings
// (applyRaceTableDefaults, shared by the menus and the race).
TEST(ParitySession, FinishesRegisterUnderTheTableSettings) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(parityRetail());
    auto s = paritySession(GameMode::Circuit, 0);
    ASSERT_TRUE(s);
    RaceConfig played = s->setup().config;
    applyRaceTableDefaults(played, s->setup().race);
    RaceConfig defaults = played;
    EXPECT_TRUE(Progress::recordable(played, defaults));
    EXPECT_EQ(played.laps, std::max(1, s->setup().settings.numLaps));
    played.weather = played.weather == Weather::Rain ? Weather::Clear : Weather::Rain;
    EXPECT_FALSE(Progress::recordable(played, defaults));
}

// mmGame::SendChatMessage's "/blubber" sets bCheating, which only the game's
// start clears; a race result then registers nothing.
TEST(ParitySession, CheatingMarksTheResult) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(parityRetail());
    auto s = paritySession(GameMode::Blitz, 0);
    ASSERT_TRUE(s);
    EXPECT_FALSE(s->result().cheated);
    setCheating(true);
    EXPECT_TRUE(s->result().cheated);
    setCheating(false);
}

// mmMultiCR across two machines: the client asks for the gold (0x25e), the
// host grants it (0x25a), the client delivers (600), the host draws the next
// set (0x261); both keep the same scores and places.
TEST(ParitySession, CopsAndRobbersAcrossMachines) {
    CrLocations loc;
    for (int i = 0; i < 6; ++i)
        loc.points.push_back({100.0f * static_cast<float>(i), 0, 0});
    CrSettings settings;
    settings.mode = CopsAndRobbersMode::CopsVsRobbers;
    settings.seed = 7;
    CopsAndRobbers host(settings, loc), client(settings, loc);
    for (auto* g : {&host, &client}) {
        g->addCar(0, CrTeam::Robber);
        g->addCar(1, CrTeam::Cop);
    }
    ASSERT_EQ(host.set().gold, client.set().gold);
    using Type = CopsAndRobbers::Message::Type;
    std::vector<CopsAndRobbers::Car> cars{{0, CrTeam::Robber, {5000, 0, 5000}, false, false},
                                          {1, CrTeam::Cop, client.set().gold, false, false}};
    auto sent = client.updateNetwork(0.1f, 1, false, cars, {});
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].type, Type::PickupRequest);
    EXPECT_FALSE(client.goldActive());
    auto acks = host.receive(sent[0], 1, true);
    ASSERT_EQ(acks.size(), 1u);
    EXPECT_EQ(acks[0].type, Type::GoldTaken);
    EXPECT_EQ(host.goldCarrier(), 1);
    client.receive(acks[0], 0, false);
    EXPECT_EQ(client.goldCarrier(), 1);
    EXPECT_EQ(client.playerScore(1), 25);
    // The cop drives to the bank.
    cars[1].position = client.deliveryTarget(CrTeam::Cop);
    sent = client.updateNetwork(0.1f, 1, false, cars, {});
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].type, Type::GoldDelivered);
    auto sets = host.receive(sent[0], 1, true);
    ASSERT_EQ(sets.size(), 1u);
    EXPECT_EQ(sets[0].type, Type::NewSet);
    client.receive(sets[0], 0, false);
    EXPECT_EQ(client.set().gold, host.set().gold);
    EXPECT_EQ(client.set().bank, host.set().bank);
    EXPECT_EQ(host.playerScore(1), 125);
    EXPECT_EQ(client.playerScore(1), 125);
    EXPECT_TRUE(client.goldActive());
    // A hard hit from another player knocks a local carrier's gold loose.
    cars[1].position = client.set().gold;
    sent = client.updateNetwork(0.1f, 1, false, cars, {});
    host.receive(sent.at(0), 1, true);
    client.receive({Type::GoldTaken, 1}, 0, false);
    sent = client.updateNetwork(0.1f, 1, false, cars, {{1, 0, 300.0f}});
    ASSERT_FALSE(sent.empty());
    EXPECT_EQ(sent[0].type, Type::GoldDropped);
    EXPECT_EQ(client.goldCarrier(), -1);
}

// mmGameMulti::StartXYZ's grid: 6 m rows, or 16 / 34 m back for long cars.
TEST(ParitySession, MultiplayerGridSlots) {
    EXPECT_EQ(multiplayerGridOffset(0, false), (Vec3{2.25f, 0, 6}));
    EXPECT_EQ(multiplayerGridOffset(4, false), (Vec3{-4.5f, 0, 0}));
    EXPECT_EQ(multiplayerGridOffset(7, true), (Vec3{5.5f, 0, 34}));
    EXPECT_EQ(multiplayerGridOffset(9, true), (Vec3{}));
}
