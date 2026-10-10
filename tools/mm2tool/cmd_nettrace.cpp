// nettrace: how far a client's shared traffic is from the host's.
//
// Reads two OPENMM2_NET_TRACE files of one network cruise with shared
// traffic (game/net/TrafficTrace.h): the host's, whose TH lines have every
// car near a player at the session time its state belongs to, and a
// client's, whose TC lines have the cars where its car met them each frame
// and whose TV lines give that frame's session time of its car. Prints the
// error of the client's cars against the host's at the same session time,
// the cars near the client that one machine had and the other did not, the
// police targets, and the hits and knocks (docs/review/
// multiplayer-desync-traffic.md, docs/multiplayer.md "Diagnosing
// replication").

#include "Command.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::tool {
namespace {

// The whitespace-separated fields of a line.
std::vector<std::string_view> fields(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ')
            ++i;
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ')
            ++i;
        if (i > start)
            out.push_back(line.substr(start, i - start));
    }
    return out;
}

double number(std::string_view s) {
    double v = 0.0;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}
int integer(std::string_view s) { return static_cast<int>(number(s)); }

// `n` of `of`, in percent.
double share(std::size_t n, std::size_t of) {
    return 100.0 * static_cast<double>(n) / static_cast<double>(std::max<std::size_t>(1, of));
}

struct Sample {
    double time = 0.0;
    double x = 0.0, y = 0.0, z = 0.0;
    double speed = 0.0;
    int flags = 0;
    int target = -1;
    int kind = 0;
};

using Key = std::pair<int, int>; // id, generation

struct ClientCar {
    Key key;
    int kind = 0;
    double x = 0.0, z = 0.0;
    double time = 0.0;
    int flags = 0;
    int target = -1;
    int mode = 0;
};

struct Frame {
    double frame = 0.0, own = 0.0;
    double x = 0.0, z = 0.0;
    std::vector<ClientCar> cars;
};

struct Event {
    double time = 0.0;
    int id = 0, generation = 0, player = 0;
    double value = 0.0;
};

struct HostTrace {
    std::map<Key, std::vector<Sample>> cars;
    std::vector<Event> knocks;
};

struct ClientTrace {
    std::vector<Frame> frames;
    std::vector<Event> hits, handovers;
};

bool readHost(const char* path, HostTrace& out) {
    std::ifstream in(path);
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line)) {
        const auto f = fields(line);
        if (f.empty())
            continue;
        if (f[0] == "TH" && f.size() >= 13) {
            Sample s{number(f[1]),
                     number(f[5]),
                     number(f[6]),
                     number(f[7]),
                     number(f[10]),
                     integer(f[11]),
                     integer(f[12]),
                     integer(f[4])};
            out.cars[{integer(f[2]), integer(f[3])}].push_back(s);
        } else if (f[0] == "TK" && f.size() >= 5) {
            out.knocks.push_back({number(f[1]), integer(f[2]), integer(f[3]), integer(f[4])});
        }
    }
    for (auto& [key, samples] : out.cars)
        std::ranges::stable_sort(samples, {}, &Sample::time);
    return true;
}

bool readClient(const char* path, ClientTrace& out) {
    std::ifstream in(path);
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line)) {
        const auto f = fields(line);
        if (f.empty())
            continue;
        if (f[0] == "TV" && f.size() >= 6) {
            out.frames.push_back({number(f[1]), number(f[2]), number(f[3]), number(f[5]), {}});
        } else if (f[0] == "TC" && f.size() >= 15 && !out.frames.empty()) {
            ClientCar c;
            c.key = {integer(f[3]), integer(f[4])};
            c.kind = integer(f[5]);
            c.x = number(f[6]);
            c.z = number(f[8]);
            c.time = number(f[2]);
            c.flags = integer(f[12]);
            c.target = integer(f[13]);
            c.mode = integer(f[14]);
            out.frames.back().cars.push_back(c);
        } else if (f[0] == "TX" && f.size() >= 4) {
            out.hits.push_back({number(f[1]), integer(f[2]), integer(f[3]), 0});
        } else if (f[0] == "TL" && f.size() >= 5) {
            out.handovers.push_back({number(f[1]), integer(f[2]), 0, integer(f[3]), number(f[4])});
        }
    }
    return true;
}

// A host car at `time`: between its two samples around it; none in a gap
// (it left and came back) or more than `slack` ms beyond its first or last.
std::optional<Sample> at(const std::vector<Sample>& s, double time, double slack = 60.0) {
    if (s.empty())
        return std::nullopt;
    const auto hi = std::ranges::upper_bound(s, time, {}, &Sample::time);
    if (hi == s.begin())
        return s.front().time - time > slack ? std::nullopt : std::optional(s.front());
    if (hi == s.end())
        return time - s.back().time > slack ? std::nullopt : std::optional(s.back());
    const Sample& a = *(hi - 1);
    const Sample& b = *hi;
    if (b.time - a.time > 400.0)
        return std::nullopt;
    const double t = (time - a.time) / (b.time - a.time);
    Sample out = a;
    out.time = time;
    out.x = a.x + (b.x - a.x) * t;
    out.y = a.y + (b.y - a.y) * t;
    out.z = a.z + (b.z - a.z) * t;
    return out;
}

double percentile(std::vector<double> v, double q) {
    if (v.empty())
        return 0.0;
    std::ranges::sort(v);
    return v[std::min(v.size() - 1, static_cast<std::size_t>(static_cast<double>(v.size()) * q))];
}

void row(const char* name, const std::vector<double>& v) {
    if (v.empty())
        return;
    std::println("  {:<26} n={:7} median {:6.2f}  90% {:6.2f}  99% {:6.2f}  max {:7.2f} m", name, v.size(),
                 percentile(v, 0.5), percentile(v, 0.9), percentile(v, 0.99), percentile(v, 1.0));
}

int cmdNettrace(std::span<char* const> args) {
    if (args.size() < 2) {
        std::println(stderr, "usage: mm2tool nettrace <host trace> <client trace> [radius]");
        return 2;
    }
    const double radius = args.size() > 2 ? number(args[2]) : 150.0;
    HostTrace host;
    ClientTrace client;
    if (!readHost(args[0], host) || !readClient(args[1], client)) {
        std::println(stderr, "nettrace: cannot read the traces");
        return 1;
    }
    constexpr int kOffRail = 0x10;
    std::vector<double> rail, moving, offRail, police, policeMoving, policeSimulated;
    std::vector<double> near, local, shownMoving, afterHit;
    std::size_t present = 0, extra = 0, missing = 0, copFrames = 0, copTargets = 0;
    for (std::size_t i = 0; i < client.frames.size(); ++i) {
        const Frame& fr = client.frames[i];
        std::vector<Key> listed;
        for (const ClientCar& c : fr.cars) {
            const double fromMe = std::hypot(c.x - fr.x, c.z - fr.z);
            if (fromMe > radius)
                continue;
            listed.push_back(c.key);
            const auto it = host.cars.find(c.key);
            const auto h = it != host.cars.end() ? at(it->second, fr.own) : std::nullopt;
            if (!h) {
                ++extra;
                continue;
            }
            ++present;
            const double e = std::hypot(c.x - h->x, c.z - h->z);
            if (c.mode == 3)
                (c.kind == 1 ? policeSimulated : local).push_back(e);
            // In the second after the client's car hit it.
            if (std::ranges::any_of(client.hits, [&](const Event& hit) {
                    return hit.id == c.key.first && hit.generation == c.key.second && fr.own >= hit.time &&
                           fr.own < hit.time + 1000.0;
                }))
                afterHit.push_back(e);
            if (c.kind == 1) {
                police.push_back(e);
                if (std::abs(h->speed) > 2.0)
                    policeMoving.push_back(e);
                ++copFrames;
                copTargets += h->target != c.target ? 1 : 0;
            } else if ((c.flags & kOffRail) != 0) {
                offRail.push_back(e);
            } else {
                rail.push_back(e);
                if (std::abs(h->speed) > 2.0)
                    moving.push_back(e);
            }
            if (fromMe < 40.0)
                near.push_back(e);
            // Against the host's at the time the client showed it (another
            // time than its car's with OPENMM2_DEBUG_TRAFFIC_LEAD_MS).
            if (const auto s = at(it->second, c.time); s && c.kind == 0 && (c.flags & kOffRail) == 0 &&
                                                        std::abs(s->speed) > 2.0)
                shownMoving.push_back(std::hypot(c.x - s->x, c.z - s->z));
        }
        // The host's cars near this client's car it did not have (every
        // fourth frame).
        if (i % 4 != 0)
            continue;
        for (const auto& [key, samples] : host.cars) {
            if (std::ranges::find(listed, key) != listed.end())
                continue;
            const auto h = at(samples, fr.own, 20.0);
            if (h && std::hypot(h->x - fr.x, h->z - fr.z) < radius - 20.0)
                missing += 4;
        }
    }
    std::println("frames {}, car-frames compared {}", client.frames.size(), present);
    std::println("error against the host at the client car's session time (on the ground plane):");
    row("rail cars", rail);
    row("rail cars moving > 2 m/s", moving);
    row("off-rail (knocked) cars", offRail);
    row("police", police);
    row("police moving > 2 m/s", policeMoving);
    row("police simulated here", policeSimulated);
    row("any car within 40 m", near);
    row("simulated here (knocked)", local);
    row("hit by the client, 1 s", afterHit);
    std::println("error against the host at the time the client showed the car:");
    row("rail cars moving > 2 m/s", shownMoving);
    std::println("cars within {:.0f} m: on the host only {} car-frames ({:.2f}%), on the client only {} "
                 "({:.2f}%)",
                 radius - 20.0, missing, share(missing, present + missing), extra,
                 share(extra, present + extra));
    if (copFrames)
        std::println("police targets differ in {} of {} car-frames", copTargets, copFrames);
    // The cars the client's car hit that the host had off their rails within
    // 2.5 s too, and the host's knocks.
    const auto knockedToo = std::ranges::count_if(client.hits, [&](const Event& hit) {
        return std::ranges::any_of(host.knocks, [&](const Event& e) {
            return e.id == hit.id && e.generation == hit.generation && std::abs(e.time - hit.time) < 2500.0;
        });
    });
    std::println("cars the client's car hit {}: off their rails on the host too {}; cars the host knocked off "
                 "their rails {}",
                 client.hits.size(), knockedToo, host.knocks.size());
    if (!client.handovers.empty()) {
        std::vector<double> confirmed, withdrawn;
        for (const Event& h : client.handovers)
            (h.player ? confirmed : withdrawn).push_back(h.value);
        std::println("cars the client knocked loose: {} confirmed by the host, {} withdrawn",
                     confirmed.size(), withdrawn.size());
        row("drawing jump when confirmed", confirmed);
        row("drawing jump when withdrawn", withdrawn);
    }
    return 0;
}

const Registrar reg({"nettrace", "<host trace> <client trace> [radius]",
                     "compare a client's shared traffic with the host's (OPENMM2_NET_TRACE files)",
                     &cmdNettrace});

} // namespace
} // namespace mm2::tool
