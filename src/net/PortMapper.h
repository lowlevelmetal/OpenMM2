#pragma once

// Automatic port forwarding for hosts behind a home router.
//
// PortMapper runs on its own worker thread (router requests can take
// seconds). It tries, in order, UPnP IGD (miniupnpc), then PCP / NAT-PMP, and
// keeps the mapping alive by renewing the lease at half its lifetime. On stop
// the mapping is removed. The active mapping is recorded in a small state file
// so that a run that crashed while holding a mapping cleans it up on the next
// start (leases also expire on their own, 1 hour by default).
//
// status() may be called from any thread. The optional callback is invoked on
// the worker thread whenever the status changes; marshal to the UI thread.

#include "net/Net.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace mm2::net {

enum class MappingMethod { None, Upnp, Pcp, NatPmp };
const char* describe(MappingMethod method);

struct PortMapperConfig {
    std::uint16_t internalPort = kDefaultGamePort;
    std::uint16_t externalPort = 0; // preferred external port, 0 = same as internal
    std::string description = "OpenMM2";
    std::uint32_t leaseSeconds = 3600; // 0 = permanent (only if the router insists)
    bool enableUpnp = true;
    bool enableNatPmp = true; // PCP, falling back to NAT-PMP
    std::uint32_t discoveryTimeoutMs = 2500;
    int maxPortAttempts = 8; // external ports tried when the preferred one is taken
    std::filesystem::path stateFile; // crash-safe cleanup record; empty disables it
};

struct PortMappingStatus {
    enum class State { Idle, Working, Mapped, Failed };
    State state = State::Idle;
    MappingMethod method = MappingMethod::None;
    std::string externalIp;
    std::uint16_t externalPort = 0;
    std::string internalIp;
    std::uint16_t internalPort = 0;
    std::string gateway;          // router description or address
    std::uint32_t leaseSeconds = 0; // 0 = permanent
    // The router's own Internet address is private: double NAT or carrier-grade
    // NAT. The mapping works but may not make the host reachable from outside.
    bool doubleNat = false;
    std::string message; // one-line summary for the UI
    std::vector<std::string> details; // per-method outcome, for a "details" view / logs
};

// --- Backend interface (exposed so tests can substitute fakes) ------------------

struct GatewayInfo {
    std::string id;          // stable identity for the state file (UPnP root URL / gateway IP)
    std::string description; // for the UI
    std::string internalIp;  // our LAN address as seen by the gateway
    std::string externalIp;  // may be empty until a mapping exists
    bool doubleNat = false;
};

struct MappingRequest {
    std::uint16_t internalPort = 0;
    std::uint16_t externalPort = 0;
    std::uint32_t leaseSeconds = 0;
    std::string description;
};

struct MappingResult {
    enum class Outcome { Ok, Conflict, Failed };
    Outcome outcome = Outcome::Failed;
    std::uint16_t externalPort = 0;  // as granted (may differ from the request)
    std::uint32_t leaseSeconds = 0;  // as granted
    std::string externalIp;          // if learned
    std::string error;
};

// Everything needed to remove a mapping later, possibly from another process.
struct MappingRecord {
    MappingMethod method = MappingMethod::None;
    std::string gatewayId;
    std::string internalIp;
    std::uint16_t internalPort = 0;
    std::uint16_t externalPort = 0;
    std::string token; // backend-specific (PCP mapping nonce, hex)
    std::string description;

    bool valid() const { return method != MappingMethod::None && internalPort != 0 && externalPort != 0; }
};

class PortMappingBackend {
public:
    virtual ~PortMappingBackend() = default;
    // Which method this backend implements (may be refined by discover(),
    // e.g. PCP vs NAT-PMP).
    virtual MappingMethod method() const = 0;
    // Finds the gateway. Called once before map/unmap.
    virtual bool discover(std::uint32_t timeoutMs, GatewayInfo& out, std::string& error) = 0;
    virtual MappingResult map(const MappingRequest& request) = 0;
    // Removes a mapping (current or recorded by an earlier run). Must not
    // remove mappings that belong to another host.
    virtual bool unmap(const MappingRecord& record, std::string& error) = 0;
    // Backend-specific data to persist with the mapping (see MappingRecord::token).
    virtual std::string token() const { return {}; }
};

std::unique_ptr<PortMappingBackend> makeUpnpBackend();
std::unique_ptr<PortMappingBackend> makeNatPmpBackend();

// State file helpers (exposed for tests).
bool saveMappingRecord(const std::filesystem::path& file, const MappingRecord& record);
std::optional<MappingRecord> loadMappingRecord(const std::filesystem::path& file);

class PortMapper {
public:
    using BackendFactory = std::function<std::vector<std::unique_ptr<PortMappingBackend>>(const PortMapperConfig&)>;

    PortMapper();
    // Uses `factory` instead of the real UPnP/NAT-PMP backends (tests).
    explicit PortMapper(BackendFactory factory);
    ~PortMapper();
    PortMapper(const PortMapper&) = delete;
    PortMapper& operator=(const PortMapper&) = delete;

    // Starts (or restarts) mapping in the background.
    void start(const PortMapperConfig& config);
    // Removes the mapping and stops the worker. Blocks until the router has
    // answered or timed out (a few seconds at most).
    void stop();
    bool running() const;

    PortMappingStatus status() const;
    void setCallback(std::function<void(const PortMappingStatus&)> callback);

    // Smallest delay before renewing; tests shorten it.
    void setMinimumRenewDelay(std::chrono::milliseconds d) { m_minRenewDelay = d; }

private:
    void run(PortMapperConfig config);
    void setStatus(PortMappingStatus status);
    bool waitFor(std::chrono::milliseconds delay); // false when stopping

    BackendFactory m_factory;
    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stopRequested = false;
    PortMappingStatus m_status;
    std::function<void(const PortMappingStatus&)> m_callback;
    std::chrono::milliseconds m_minRenewDelay{60000};
};

} // namespace mm2::net
