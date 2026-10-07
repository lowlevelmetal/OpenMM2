// PortMapper worker logic against fake backends: no router needed.
#include "net/PortMapper.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <set>
#include <thread>

using namespace mm2;
using namespace mm2::net;
using namespace std::chrono_literals;

namespace {

struct FakeState {
    std::atomic<int> discovers{0};
    std::atomic<int> maps{0};
    std::atomic<int> unmaps{0};
    std::vector<MappingRecord> unmapped;
    std::mutex mutex;
};

class FakeBackend final : public PortMappingBackend {
public:
    struct Options {
        MappingMethod method = MappingMethod::Upnp;
        bool discoverOk = true;
        std::string discoverError = "no device answered";
        std::string gatewayId = "http://192.168.1.1:5000/rootDesc.xml";
        std::set<std::uint16_t> takenPorts;
        bool mapFails = false;
        std::uint32_t grantedLease = 3600;
        std::string externalIp = "203.0.113.7";
        int failRenewalsAfter = -1; // map() starts failing after this many successes
    };

    FakeBackend(Options o, std::shared_ptr<FakeState> s) : m_o(std::move(o)), m_s(std::move(s)) {}

    MappingMethod method() const override { return m_o.method; }

    bool discover(std::uint32_t, GatewayInfo& out, std::string& error) override {
        ++m_s->discovers;
        if (!m_o.discoverOk) {
            error = m_o.discoverError;
            return false;
        }
        out.id = m_o.gatewayId;
        out.description = "fake router";
        out.internalIp = "192.168.1.20";
        out.externalIp = m_o.externalIp;
        return true;
    }

    MappingResult map(const MappingRequest& req) override {
        const int n = ++m_s->maps;
        MappingResult r;
        if (m_o.mapFails || (m_o.failRenewalsAfter >= 0 && n > m_o.failRenewalsAfter)) {
            r.error = "router said no";
            return r;
        }
        if (m_o.takenPorts.contains(req.externalPort)) {
            r.outcome = MappingResult::Outcome::Conflict;
            r.error = "taken";
            return r;
        }
        r.outcome = MappingResult::Outcome::Ok;
        r.externalPort = req.externalPort;
        r.leaseSeconds = std::min(req.leaseSeconds, m_o.grantedLease);
        r.externalIp = m_o.externalIp;
        return r;
    }

    bool unmap(const MappingRecord& record, std::string&) override {
        ++m_s->unmaps;
        std::lock_guard lock(m_s->mutex);
        m_s->unmapped.push_back(record);
        return true;
    }

    std::string token() const override { return "tok"; }

private:
    Options m_o;
    std::shared_ptr<FakeState> m_s;
};

PortMapper::BackendFactory factory(std::vector<FakeBackend::Options> opts, std::shared_ptr<FakeState> state) {
    return [opts, state](const PortMapperConfig&) {
        std::vector<std::unique_ptr<PortMappingBackend>> v;
        for (const auto& o : opts)
            v.push_back(std::make_unique<FakeBackend>(o, state));
        return v;
    };
}

bool waitForState(const PortMapper& m, PortMappingStatus::State s, std::chrono::milliseconds timeout = 2000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (m.status().state == s)
            return true;
        std::this_thread::sleep_for(2ms);
    }
    return false;
}

std::filesystem::path tempStateFile(const char* name) {
    auto p = std::filesystem::temp_directory_path() / std::format("openmm2-test-{}-{}.ini", name, ::testing::UnitTest::GetInstance()->random_seed());
    std::filesystem::remove(p);
    return p;
}

} // namespace

TEST(PortMapper, ReportsWhyEverythingFailed) {
    auto state = std::make_shared<FakeState>();
    FakeBackend::Options upnp{.method = MappingMethod::Upnp, .discoverOk = false,
                              .discoverError = "no UPnP device answered within 2.5 s"};
    FakeBackend::Options pmp{.method = MappingMethod::NatPmp, .discoverOk = false,
                             .discoverError = "gateway 192.168.1.1 did not answer"};
    PortMapper m(factory({upnp, pmp}, state));
    std::vector<PortMappingStatus> seen;
    std::mutex seenMutex;
    m.setCallback([&](const PortMappingStatus& s) {
        std::lock_guard lock(seenMutex);
        seen.push_back(s);
    });
    PortMapperConfig cfg;
    cfg.internalPort = 2300;
    m.start(cfg);
    ASSERT_TRUE(waitForState(m, PortMappingStatus::State::Failed));
    const auto s = m.status();
    EXPECT_EQ(s.method, MappingMethod::None);
    ASSERT_EQ(s.details.size(), 2u);
    EXPECT_NE(s.details[0].find("UPnP"), std::string::npos);
    EXPECT_NE(s.details[1].find("did not answer"), std::string::npos);
    EXPECT_NE(s.message.find("2300"), std::string::npos);
    EXPECT_EQ(state->discovers, 2);
    EXPECT_EQ(state->maps, 0);
    m.stop();
    EXPECT_EQ(state->unmaps, 0);
    std::lock_guard lock(seenMutex);
    EXPECT_GE(seen.size(), 2u); // progress updates were reported
}

TEST(PortMapper, FallsBackToNatPmp) {
    auto state = std::make_shared<FakeState>();
    FakeBackend::Options upnp{.method = MappingMethod::Upnp, .discoverOk = false};
    FakeBackend::Options pmp{.method = MappingMethod::NatPmp, .gatewayId = "192.168.1.1"};
    PortMapper m(factory({upnp, pmp}, state));
    m.start({});
    ASSERT_TRUE(waitForState(m, PortMappingStatus::State::Mapped));
    const auto s = m.status();
    EXPECT_EQ(s.method, MappingMethod::NatPmp);
    EXPECT_EQ(s.externalPort, kDefaultGamePort);
    EXPECT_EQ(s.externalIp, "203.0.113.7");
    EXPECT_FALSE(s.doubleNat);
    EXPECT_NE(s.message.find("NAT-PMP"), std::string::npos);
    EXPECT_NE(s.message.find("203.0.113.7"), std::string::npos);
    m.stop();
    EXPECT_EQ(state->unmaps, 1);
    EXPECT_EQ(m.status().state, PortMappingStatus::State::Idle);
}

TEST(PortMapper, ConflictTriesNextExternalPort) {
    auto state = std::make_shared<FakeState>();
    FakeBackend::Options upnp{.takenPorts = {2300, 2301}};
    PortMapper m(factory({upnp}, state));
    PortMapperConfig cfg;
    cfg.internalPort = 2300;
    m.start(cfg);
    ASSERT_TRUE(waitForState(m, PortMappingStatus::State::Mapped));
    const auto s = m.status();
    EXPECT_EQ(s.internalPort, 2300);
    EXPECT_EQ(s.externalPort, 2302);
    EXPECT_NE(s.message.find("2302"), std::string::npos);
    EXPECT_EQ(s.details.size(), 2u);
    m.stop();
    ASSERT_EQ(state->unmapped.size(), 1u);
    EXPECT_EQ(state->unmapped[0].externalPort, 2302);
}

TEST(PortMapper, GivesUpAfterMaxAttempts) {
    auto state = std::make_shared<FakeState>();
    FakeBackend::Options upnp{.takenPorts = {2300, 2301, 2302}};
    PortMapper m(factory({upnp}, state));
    PortMapperConfig cfg;
    cfg.maxPortAttempts = 3;
    m.start(cfg);
    ASSERT_TRUE(waitForState(m, PortMappingStatus::State::Failed));
    EXPECT_EQ(state->maps, 3);
}

TEST(PortMapper, RenewsLeaseAndDetectsLoss) {
    auto state = std::make_shared<FakeState>();
    FakeBackend::Options upnp{.grantedLease = 1, .failRenewalsAfter = 3}; // 1 map + 2 renewals succeed
    PortMapper m(factory({upnp}, state));
    m.setMinimumRenewDelay(20ms);
    PortMapperConfig cfg;
    cfg.leaseSeconds = 1;
    m.start(cfg);
    ASSERT_TRUE(waitForState(m, PortMappingStatus::State::Mapped));
    EXPECT_EQ(m.status().leaseSeconds, 1u);
    // Renewal every 500 ms (half the lease); after the third failure the
    // mapping is reported lost.
    ASSERT_TRUE(waitForState(m, PortMappingStatus::State::Failed, 5000ms));
    EXPECT_GE(state->maps, 6);
    EXPECT_NE(m.status().message.find("Lost"), std::string::npos);
    m.stop();
    EXPECT_EQ(state->unmaps, 1); // still tries to clean up
}

TEST(PortMapper, PersistsRecordAndCleansUpStaleMapping) {
    const auto file = tempStateFile("stale");
    auto state = std::make_shared<FakeState>();
    FakeBackend::Options upnp;
    {
        PortMapper m(factory({upnp}, state));
        PortMapperConfig cfg;
        cfg.stateFile = file;
        m.start(cfg);
        ASSERT_TRUE(waitForState(m, PortMappingStatus::State::Mapped));
        // While mapped, the record exists on disk.
        const auto rec = loadMappingRecord(file);
        ASSERT_TRUE(rec);
        EXPECT_EQ(rec->method, MappingMethod::Upnp);
        EXPECT_EQ(rec->externalPort, kDefaultGamePort);
        EXPECT_EQ(rec->internalIp, "192.168.1.20");
        EXPECT_EQ(rec->token, "tok");
        m.stop();
        EXPECT_FALSE(std::filesystem::exists(file)); // clean shutdown removes it
    }

    // Simulate a crash: a record left behind from an earlier run.
    MappingRecord stale;
    stale.method = MappingMethod::Upnp;
    stale.gatewayId = upnp.gatewayId;
    stale.internalIp = "192.168.1.20";
    stale.internalPort = 2300;
    stale.externalPort = 2305;
    stale.description = "OpenMM2";
    ASSERT_TRUE(saveMappingRecord(file, stale));
    state->unmapped.clear();
    state->unmaps = 0;

    PortMapper m(factory({upnp}, state));
    PortMapperConfig cfg;
    cfg.stateFile = file;
    m.start(cfg);
    ASSERT_TRUE(waitForState(m, PortMappingStatus::State::Mapped));
    {
        std::lock_guard lock(state->mutex);
        ASSERT_EQ(state->unmapped.size(), 1u);
        EXPECT_EQ(state->unmapped[0].externalPort, 2305);
    }
    m.stop();
    std::filesystem::remove(file);
}

TEST(PortMapper, StaleRecordFromOtherGatewayIsLeftAlone) {
    const auto file = tempStateFile("othergw");
    MappingRecord stale;
    stale.method = MappingMethod::Pcp;
    stale.gatewayId = "10.0.0.1";
    stale.internalPort = 2300;
    stale.externalPort = 2300;
    ASSERT_TRUE(saveMappingRecord(file, stale));

    auto state = std::make_shared<FakeState>();
    FakeBackend::Options pmp{.method = MappingMethod::NatPmp, .gatewayId = "192.168.1.1"};
    PortMapper m(factory({pmp}, state));
    PortMapperConfig cfg;
    cfg.stateFile = file;
    m.start(cfg);
    ASSERT_TRUE(waitForState(m, PortMappingStatus::State::Mapped));
    EXPECT_EQ(state->unmaps, 0); // different router: nothing removed
    m.stop();
    std::filesystem::remove(file);
}

TEST(PortMapper, DoubleNatIsFlagged) {
    auto state = std::make_shared<FakeState>();
    FakeBackend::Options upnp{.externalIp = "100.64.12.34"}; // carrier-grade NAT range
    PortMapper m(factory({upnp}, state));
    m.start({});
    ASSERT_TRUE(waitForState(m, PortMappingStatus::State::Mapped));
    EXPECT_TRUE(m.status().doubleNat);
    EXPECT_NE(m.status().message.find("double NAT"), std::string::npos);
}

TEST(PortMapper, StopWhileIdleAndRestart) {
    auto state = std::make_shared<FakeState>();
    PortMapper m(factory({FakeBackend::Options{}}, state));
    m.stop(); // never started: no-op
    m.start({});
    ASSERT_TRUE(waitForState(m, PortMappingStatus::State::Mapped));
    m.start({}); // restart: old mapping removed first
    ASSERT_TRUE(waitForState(m, PortMappingStatus::State::Mapped));
    EXPECT_EQ(state->unmaps, 1);
    m.stop();
    EXPECT_EQ(state->unmaps, 2);
    EXPECT_FALSE(m.running());
}

TEST(PortMapper, RecordRoundTrip) {
    const auto file = tempStateFile("record");
    MappingRecord r;
    r.method = MappingMethod::Pcp;
    r.gatewayId = "192.168.0.1";
    r.internalIp = "192.168.0.50";
    r.internalPort = 2300;
    r.externalPort = 40000;
    r.token = "00112233445566778899aabb";
    r.description = "OpenMM2";
    ASSERT_TRUE(saveMappingRecord(file, r));
    const auto back = loadMappingRecord(file);
    ASSERT_TRUE(back);
    EXPECT_EQ(back->method, r.method);
    EXPECT_EQ(back->gatewayId, r.gatewayId);
    EXPECT_EQ(back->internalIp, r.internalIp);
    EXPECT_EQ(back->externalPort, r.externalPort);
    EXPECT_EQ(back->token, r.token);
    std::filesystem::remove(file);
    EXPECT_FALSE(loadMappingRecord(file));
}
