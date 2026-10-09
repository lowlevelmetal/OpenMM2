#include "net/PortMapper.h"

#include "core/Ini.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "net/Discovery.h"

#include <format>

namespace mm2::net {
namespace {

constexpr const char* kSection = "PortMapping";

const char* methodKey(MappingMethod m) {
    switch (m) {
    case MappingMethod::Upnp: return "upnp";
    case MappingMethod::Pcp: return "pcp";
    case MappingMethod::NatPmp: return "natpmp";
    case MappingMethod::None: break;
    }
    return "none";
}

MappingMethod methodFromKey(std::string_view key) {
    for (MappingMethod m : {MappingMethod::Upnp, MappingMethod::Pcp, MappingMethod::NatPmp})
        if (str::iequals(key, methodKey(m)))
            return m;
    return MappingMethod::None;
}

// PCP and NAT-PMP are served by the same backend.
bool sameFamily(MappingMethod a, MappingMethod b) {
    auto family = [](MappingMethod m) { return m == MappingMethod::Pcp ? MappingMethod::NatPmp : m; };
    return family(a) == family(b);
}

std::string mappedMessage(const PortMappingStatus& s) {
    std::string where = s.externalIp.empty() ? std::format("external port {}", s.externalPort)
                                             : std::format("external address {}:{}", s.externalIp, s.externalPort);
    std::string msg = s.externalPort == s.internalPort
                          ? std::format("UDP port {} forwarded via {} ({})", s.internalPort, describe(s.method), where)
                          : std::format("UDP port {} forwarded via {} as {}", s.internalPort, describe(s.method), where);
    if (s.doubleNat)
        msg += ". Warning: the router's own Internet address is private (double NAT or carrier-grade NAT), so "
               "players outside your network may still be unable to connect";
    return msg;
}

} // namespace

const char* describe(MappingMethod method) {
    switch (method) {
    case MappingMethod::None: return "none";
    case MappingMethod::Upnp: return "UPnP";
    case MappingMethod::Pcp: return "PCP";
    case MappingMethod::NatPmp: return "NAT-PMP";
    }
    return "?";
}

bool saveMappingRecord(const std::filesystem::path& file, const MappingRecord& r) {
    IniFile ini;
    ini.set(kSection, "Method", methodKey(r.method));
    ini.set(kSection, "Gateway", r.gatewayId);
    ini.set(kSection, "InternalIp", r.internalIp);
    ini.setInt(kSection, "InternalPort", r.internalPort);
    ini.setInt(kSection, "ExternalPort", r.externalPort);
    ini.set(kSection, "Token", r.token);
    ini.set(kSection, "Description", r.description);
    return ini.save(file);
}

std::optional<MappingRecord> loadMappingRecord(const std::filesystem::path& file) {
    IniFile ini;
    if (!ini.load(file))
        return std::nullopt;
    MappingRecord r;
    r.method = methodFromKey(ini.getString(kSection, "Method"));
    r.gatewayId = ini.getString(kSection, "Gateway");
    r.internalIp = ini.getString(kSection, "InternalIp");
    // A damaged file must not wrap a port around, nor name internal port 0:
    // to PCP and NAT-PMP a deletion for internal port 0 removes every mapping
    // this machine has.
    const long long internalPort = ini.getInt(kSection, "InternalPort", 0);
    const long long externalPort = ini.getInt(kSection, "ExternalPort", 0);
    if (internalPort < 1 || internalPort > 65535 || externalPort < 1 || externalPort > 65535)
        return std::nullopt;
    r.internalPort = static_cast<std::uint16_t>(internalPort);
    r.externalPort = static_cast<std::uint16_t>(externalPort);
    r.token = ini.getString(kSection, "Token");
    r.description = ini.getString(kSection, "Description");
    if (!r.valid())
        return std::nullopt;
    return r;
}

PortMapper::PortMapper()
    : m_factory([](const PortMapperConfig& cfg) {
          std::vector<std::unique_ptr<PortMappingBackend>> backends;
          if (cfg.enableUpnp)
              backends.push_back(makeUpnpBackend());
          if (cfg.enableNatPmp)
              backends.push_back(makeNatPmpBackend());
          return backends;
      }) {}

PortMapper::PortMapper(BackendFactory factory) : m_factory(std::move(factory)) {}

PortMapper::~PortMapper() { stop(); }

void PortMapper::start(const PortMapperConfig& config) {
    stop();
    {
        std::lock_guard lock(m_mutex);
        m_stopRequested = false;
        m_status = {};
        m_status.state = PortMappingStatus::State::Working;
        m_status.internalPort = config.internalPort;
        m_status.message = "Setting up automatic port forwarding...";
    }
    m_thread = std::thread([this, config] { run(config); });
}

void PortMapper::stop() {
    {
        std::lock_guard lock(m_mutex);
        m_stopRequested = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

bool PortMapper::running() const { return m_thread.joinable(); }

PortMappingStatus PortMapper::status() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}

void PortMapper::setCallback(std::function<void(const PortMappingStatus&)> callback) {
    std::lock_guard lock(m_mutex);
    m_callback = std::move(callback);
}

void PortMapper::setStatus(PortMappingStatus status) {
    std::function<void(const PortMappingStatus&)> cb;
    {
        std::lock_guard lock(m_mutex);
        m_status = status;
        cb = m_callback;
    }
    log::info("net: port mapping: {}", status.message);
    if (cb)
        cb(status);
}

bool PortMapper::waitFor(std::chrono::milliseconds delay) {
    std::unique_lock lock(m_mutex);
    return !m_cv.wait_for(lock, delay, [this] { return m_stopRequested; });
}

void PortMapper::run(PortMapperConfig cfg) {
    auto backends = m_factory(cfg);
    PortMappingStatus st;
    st.state = PortMappingStatus::State::Working;
    st.internalPort = cfg.internalPort;
    auto stopping = [this] {
        std::lock_guard lock(m_mutex);
        return m_stopRequested;
    };

    // 1. Remove a mapping left behind by a run that did not shut down cleanly.
    if (!cfg.stateFile.empty()) {
        if (const auto stale = loadMappingRecord(cfg.stateFile)) {
            for (auto& b : backends) {
                if (!sameFamily(b->method(), stale->method))
                    continue;
                GatewayInfo gw;
                std::string err;
                if (!b->discover(cfg.discoveryTimeoutMs, gw, err)) {
                    log::info("net: cannot clean up stale {} mapping: {}", describe(stale->method), err);
                } else if (gw.id != stale->gatewayId) {
                    log::info("net: stale {} mapping belongs to another gateway; leaving it to expire",
                              describe(stale->method));
                } else if (b->unmap(*stale, err)) {
                    log::info("net: removed stale {} mapping of external port {}", describe(stale->method),
                              stale->externalPort);
                } else {
                    log::info("net: stale mapping not removed: {}", err);
                }
                break;
            }
            std::error_code ec;
            std::filesystem::remove(cfg.stateFile, ec);
        }
    }

    // 2. Try each method in turn.
    for (auto& backend : backends) {
        if (stopping())
            break;
        const char* name = backend->method() == MappingMethod::Upnp ? "UPnP" : "PCP/NAT-PMP";
        st.message = std::format("Looking for a router that supports {}...", name);
        setStatus(st);

        GatewayInfo gw;
        std::string err;
        if (!backend->discover(cfg.discoveryTimeoutMs, gw, err)) {
            st.details.push_back(std::format("{}: {}", name, err));
            continue;
        }
        st.gateway = gw.description;
        st.internalIp = gw.internalIp;
        st.externalIp = gw.externalIp;
        st.doubleNat = gw.doubleNat;
        st.method = backend->method();

        MappingResult res;
        std::uint32_t port = cfg.externalPort ? cfg.externalPort : cfg.internalPort;
        for (int attempt = 0; attempt < std::max(1, cfg.maxPortAttempts) && port <= 65535 && !stopping(); ++attempt) {
            st.message = std::format("Requesting UDP port {} from {}...", port, gw.description);
            setStatus(st);
            res = backend->map({cfg.internalPort, static_cast<std::uint16_t>(port), cfg.leaseSeconds, cfg.description});
            if (res.outcome != MappingResult::Outcome::Conflict)
                break;
            st.details.push_back(std::format("{}: port {} unavailable: {}", describe(st.method), port, res.error));
            ++port;
        }
        if (res.outcome != MappingResult::Outcome::Ok) {
            if (res.outcome == MappingResult::Outcome::Failed)
                st.details.push_back(std::format("{}: {}", describe(st.method), res.error));
            st.method = MappingMethod::None;
            continue;
        }

        // Mapped.
        MappingRecord record;
        record.method = backend->method();
        record.gatewayId = gw.id;
        record.internalIp = gw.internalIp;
        record.internalPort = cfg.internalPort;
        record.externalPort = res.externalPort;
        record.token = backend->token();
        record.description = cfg.description;
        if (!cfg.stateFile.empty() && !saveMappingRecord(cfg.stateFile, record))
            log::warn("net: cannot write {}", str::fromPath(cfg.stateFile));

        st.state = PortMappingStatus::State::Mapped;
        st.method = record.method;
        st.externalPort = res.externalPort;
        st.leaseSeconds = res.leaseSeconds;
        if (!res.externalIp.empty())
            st.externalIp = res.externalIp;
        if (const auto ext = Address::parse(st.externalIp); ext && ext->isPrivate())
            st.doubleNat = true;
        st.message = mappedMessage(st);
        setStatus(st);

        // 3. Keep the lease alive until asked to stop.
        int failures = 0;
        while (true) {
            std::chrono::milliseconds delay =
                st.leaseSeconds == 0 ? std::chrono::hours(24 * 365)
                                     : std::max(m_minRenewDelay, std::chrono::milliseconds(st.leaseSeconds * 500ull));
            if (failures > 0)
                delay = std::min(delay, std::max(m_minRenewDelay / 2, std::chrono::milliseconds(1)));
            if (!waitFor(delay))
                break;
            if (st.leaseSeconds == 0)
                continue;
            const auto renew = backend->map({cfg.internalPort, record.externalPort, cfg.leaseSeconds, cfg.description});
            if (renew.outcome == MappingResult::Outcome::Ok && renew.externalPort == record.externalPort) {
                failures = 0;
                if (st.state != PortMappingStatus::State::Mapped || renew.leaseSeconds != st.leaseSeconds) {
                    st.state = PortMappingStatus::State::Mapped;
                    st.leaseSeconds = renew.leaseSeconds;
                    st.message = mappedMessage(st);
                    setStatus(st);
                }
                continue;
            }
            if (++failures >= 3 && st.state == PortMappingStatus::State::Mapped) {
                st.state = PortMappingStatus::State::Failed;
                st.message = std::format("Lost the port mapping: renewing via {} failed ({})", describe(st.method),
                                         renew.error.empty() ? "external port changed" : renew.error);
                st.details.push_back(st.message);
                setStatus(st);
            }
        }

        // Stopping: remove the mapping.
        std::string err2;
        if (!backend->unmap(record, err2))
            log::info("net: could not remove {} mapping: {}", describe(record.method), err2);
        if (!cfg.stateFile.empty()) {
            std::error_code ec;
            std::filesystem::remove(cfg.stateFile, ec);
        }
        st.state = PortMappingStatus::State::Idle;
        st.message = std::format("Port forwarding for UDP {} removed", cfg.internalPort);
        setStatus(st);
        return;
    }

    if (stopping()) {
        st.state = PortMappingStatus::State::Idle;
        st.message = "Port forwarding cancelled";
        setStatus(st);
        return;
    }

    st.state = PortMappingStatus::State::Failed;
    st.method = MappingMethod::None;
    if (backends.empty()) {
        st.message = "Automatic port forwarding is disabled";
    } else {
        if (st.internalIp.empty())
            if (const std::uint32_t lan = primaryLocalAddress())
                st.internalIp = Address{lan, 0}.ipString();
        st.message = std::format("Automatic port forwarding failed. Forward UDP port {} to {} on your router, or "
                                 "play with people on the same network",
                                 cfg.internalPort, st.internalIp.empty() ? "this computer" : st.internalIp);
    }
    setStatus(st);
    while (waitFor(std::chrono::hours(24))) {
    }
}

} // namespace mm2::net
