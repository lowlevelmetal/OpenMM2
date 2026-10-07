// UPnP Internet Gateway Device port mapping via miniupnpc.
#include "net/PortMapper.h"

#include "core/Log.h"

#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>

#include <algorithm>
#include <cstdlib>
#include <format>

namespace mm2::net {
namespace {

// UPnP error codes (WANIPConnection).
constexpr int kNoSuchEntry = 714;
constexpr int kConflictInMappingEntry = 718;
constexpr int kSamePortValuesRequired = 724;
constexpr int kOnlyPermanentLeasesSupported = 725;

std::string upnpError(int rc) {
    const char* text = strupnperror(rc);
    return text ? std::format("{} ({})", text, rc) : std::format("UPnP error {}", rc);
}

// "http://192.168.1.1:5000/rootDesc.xml" -> "192.168.1.1"
std::string hostOf(const char* url) {
    std::string u = url ? url : "";
    if (const auto p = u.find("://"); p != std::string::npos)
        u.erase(0, p + 3);
    if (const auto p = u.find_first_of(":/"); p != std::string::npos)
        u.erase(p);
    return u;
}

class UpnpBackend final : public PortMappingBackend {
public:
    ~UpnpBackend() override { release(); }

    MappingMethod method() const override { return MappingMethod::Upnp; }

    bool discover(std::uint32_t timeoutMs, GatewayInfo& out, std::string& error) override {
        release();
        if (!m_lib.ok()) {
            error = "network initialization failed";
            return false;
        }
        // upnpDiscover() waits at least a second per search type (four of
        // them) one after another. With searchalltypes set, miniupnpc sends
        // every M-SEARCH first and then waits once, so `timeoutMs` (minimum
        // 1 s, the smallest SSDP MX) is the total wait.
        static const char* const kSearchTypes[] = {
            "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
            "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
            "urn:schemas-upnp-org:service:WANIPConnection:1",
            "urn:schemas-upnp-org:service:WANPPPConnection:1",
            nullptr,
        };
        int err = 0;
        UPNPDev* devices = upnpDiscoverDevices(kSearchTypes, static_cast<int>(std::max<std::uint32_t>(timeoutMs, 1000)),
                                               nullptr, nullptr, UPNP_LOCAL_PORT_ANY, 0, 2, &err, 1);
        if (!devices) {
            error = err == UPNPDISCOVER_SOCKET_ERROR
                        ? std::string("cannot send UPnP discovery (socket error)")
                        : std::format("no UPnP device answered within {:.1f} s (UPnP disabled on the router?)",
                                      timeoutMs / 1000.0);
            return false;
        }
        int count = 0;
        for (UPNPDev* d = devices; d; d = d->pNext)
            ++count;
        char lan[64] = {}, wan[64] = {};
        const int r = UPNP_GetValidIGD(devices, &m_urls, &m_data, lan, sizeof lan, wan, sizeof wan);
        freeUPNPDevlist(devices);
        if (r != UPNP_NO_IGD)
            m_haveUrls = true;
        switch (r) {
        case UPNP_NO_IGD:
            error = std::format("{} UPnP device(s) answered, but none is an Internet gateway", count);
            return false;
        case UPNP_UNKNOWN_DEVICE:
            release();
            error = "a UPnP device answered, but it is not a recognised Internet gateway";
            return false;
        default: break;
        }
        m_lan = lan;
        out.id = m_urls.rootdescURL ? m_urls.rootdescURL : "";
        out.description = std::format("UPnP router {}", hostOf(m_urls.rootdescURL));
        if (r == UPNP_DISCONNECTED_IGD)
            out.description += " (reports no Internet connection)";
        out.internalIp = m_lan;
        char ext[64] = {};
        if (UPNP_GetExternalIPAddress(m_urls.controlURL, m_data.first.servicetype, ext) == UPNPCOMMAND_SUCCESS)
            m_externalIp = ext;
        else if (wan[0])
            m_externalIp = wan;
        out.externalIp = m_externalIp;
        const auto extAddr = Address::parse(m_externalIp);
        out.doubleNat = r == UPNP_PRIVATEIP_IGD || (extAddr && extAddr->isPrivate());
        return true;
    }

    MappingResult map(const MappingRequest& req) override {
        MappingResult res;
        if (!m_haveUrls) {
            res.error = "no gateway";
            return res;
        }
        const std::string ext = std::to_string(req.externalPort);
        const std::string in = std::to_string(req.internalPort);

        // Refuse to overwrite somebody else's forwarding.
        char client[64] = {}, port[8] = {}, desc[80] = {}, enabled[8] = {}, lease[16] = {};
        if (UPNP_GetSpecificPortMappingEntry(m_urls.controlURL, m_data.first.servicetype, ext.c_str(), "UDP", nullptr,
                                             client, port, desc, enabled, lease) == UPNPCOMMAND_SUCCESS &&
            client[0] && (m_lan != client || std::atoi(port) != req.internalPort)) {
            res.outcome = MappingResult::Outcome::Conflict;
            res.error = std::format("external port {} is already forwarded to {}:{}", req.externalPort, client, port);
            return res;
        }

        std::uint32_t leaseSeconds = req.leaseSeconds;
        int rc = UPNP_AddPortMapping(m_urls.controlURL, m_data.first.servicetype, ext.c_str(), in.c_str(), m_lan.c_str(),
                                     req.description.c_str(), "UDP", nullptr, std::to_string(leaseSeconds).c_str());
        if (rc == kOnlyPermanentLeasesSupported && leaseSeconds != 0) {
            leaseSeconds = 0;
            rc = UPNP_AddPortMapping(m_urls.controlURL, m_data.first.servicetype, ext.c_str(), in.c_str(),
                                     m_lan.c_str(), req.description.c_str(), "UDP", nullptr, "0");
        }
        if (rc == UPNPCOMMAND_SUCCESS) {
            res.outcome = MappingResult::Outcome::Ok;
            res.externalPort = req.externalPort;
            res.leaseSeconds = leaseSeconds;
            res.externalIp = m_externalIp;
            return res;
        }
        res.outcome = rc == kConflictInMappingEntry ? MappingResult::Outcome::Conflict : MappingResult::Outcome::Failed;
        res.error = upnpError(rc);
        if (rc == kSamePortValuesRequired)
            res.error += ": the router only forwards to the same port number";
        return res;
    }

    bool unmap(const MappingRecord& record, std::string& error) override {
        if (!m_haveUrls) {
            error = "no gateway";
            return false;
        }
        const std::string ext = std::to_string(record.externalPort);
        char client[64] = {}, port[8] = {}, desc[80] = {}, enabled[8] = {}, lease[16] = {};
        const int q = UPNP_GetSpecificPortMappingEntry(m_urls.controlURL, m_data.first.servicetype, ext.c_str(), "UDP",
                                                       nullptr, client, port, desc, enabled, lease);
        if (q == kNoSuchEntry)
            return true; // already gone (lease expired or router rebooted)
        if (q == UPNPCOMMAND_SUCCESS && client[0] && record.internalIp != client) {
            error = std::format("port {} now belongs to {}; left alone", record.externalPort, client);
            return false;
        }
        const int rc = UPNP_DeletePortMapping(m_urls.controlURL, m_data.first.servicetype, ext.c_str(), "UDP", nullptr);
        if (rc == UPNPCOMMAND_SUCCESS || rc == kNoSuchEntry)
            return true;
        error = upnpError(rc);
        return false;
    }

private:
    void release() {
        if (m_haveUrls)
            FreeUPNPUrls(&m_urls);
        m_haveUrls = false;
        m_urls = {};
        m_data = {};
    }

    NetLibrary m_lib; // miniupnpc needs Winsock initialised on Windows
    UPNPUrls m_urls{};
    IGDdatas m_data{};
    bool m_haveUrls = false;
    std::string m_lan;
    std::string m_externalIp;
};

} // namespace

std::unique_ptr<PortMappingBackend> makeUpnpBackend() { return std::make_unique<UpnpBackend>(); }

} // namespace mm2::net
