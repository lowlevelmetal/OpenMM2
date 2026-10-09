// netprobe syncreport: how far apart the machines of a network race show the
// players' cars, how often a car jumps, and which collisions between players
// one machine had and another did not. Reads each machine's
// OPENMM2_NET_TRACE file (the D, K and C lines, see NetGame::traceDrawn).
//
// The machines must have run on one computer: "the same moment" is the same
// monotonic wall clock, which every process on a machine shares, so the
// comparison is what two screens side by side show.

#include "SyncReport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <fstream>
#include <map>
#include <print>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace mm2::netprobe {
namespace {

struct V3 {
    double x = 0, y = 0, z = 0;
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator*(double k) const { return {x * k, y * k, z * k}; }
    double length() const { return std::sqrt(x * x + y * y + z * z); }
    double horizontal() const { return std::sqrt(x * x + z * z); }
};

struct Drawn {
    double wall = 0, time = 0;
    V3 p, v;
};
struct Hit {
    double wall = 0, time = 0;
    int a = 0, b = 0;
    V3 p;
    double strength = 0;
};
struct Correction {
    double wall = 0, time = 0;
    int replayed = 0;
    V3 dx;
    double dv = 0;
    bool snapped = false;
};

struct Machine {
    std::string path;
    int self = -1;
    std::map<int, std::vector<Drawn>> drawn;
    std::vector<Hit> hits;
    std::vector<Correction> corrections;
    double firstWall = 0;
};

bool load(const std::string& path, Machine& m) {
    std::ifstream in(path);
    if (!in)
        return false;
    m.path = path;
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (line.size() < 2 || line[1] != ' ')
            continue;
        std::istringstream s(line.substr(2));
        switch (line[0]) {
        case 'D': {
            Drawn d;
            int id = 0, own = 0;
            if (!(s >> d.wall >> d.time >> id >> own >> d.p.x >> d.p.y >> d.p.z >> d.v.x >> d.v.y >> d.v.z))
                continue;
            if (own)
                m.self = id;
            if (first) {
                m.firstWall = d.wall;
                first = false;
            }
            m.drawn[id].push_back(d);
            break;
        }
        case 'K': {
            Hit h;
            if (!(s >> h.wall >> h.time >> h.a >> h.b >> h.p.x >> h.p.y >> h.p.z >> h.strength))
                continue;
            m.hits.push_back(h);
            break;
        }
        case 'C': {
            Correction c;
            unsigned seq = 0;
            int snapped = 0;
            if (!(s >> c.wall >> c.time >> seq >> c.replayed >> c.dx.x >> c.dx.y >> c.dx.z >> c.dv >>
                  snapped))
                continue;
            c.snapped = snapped != 0;
            m.corrections.push_back(c);
            break;
        }
        default: break;
        }
    }
    return true;
}

// A series' position at wall time `w` (linear between its samples), if it
// has samples within 200 ms on each side.
bool at(const std::vector<Drawn>& series, double w, V3& out) {
    const auto it = std::ranges::lower_bound(series, w, {}, &Drawn::wall);
    if (it == series.begin() || it == series.end())
        return false;
    const Drawn& b = *it;
    const Drawn& a = *(it - 1);
    if (b.wall - a.wall > 200.0)
        return false;
    const double f = b.wall > a.wall ? (w - a.wall) / (b.wall - a.wall) : 0.0;
    out = a.p + (b.p - a.p) * f;
    return true;
}

struct Stats {
    std::vector<double> v;
    void add(double x) { v.push_back(x); }
    double pct(double p) {
        if (v.empty())
            return 0;
        std::ranges::sort(v);
        const double k = std::clamp(p, 0.0, 1.0) * static_cast<double>(v.size() - 1);
        const auto i = static_cast<std::size_t>(k + 0.5);
        return v[i];
    }
    double rms() const {
        double s = 0;
        for (double x : v)
            s += x * x;
        return v.empty() ? 0 : std::sqrt(s / static_cast<double>(v.size()));
    }
};

std::string who(const std::vector<Machine>& ms, std::size_t i) {
    return ms[i].self == 0 ? std::string("host") : std::format("client {}", ms[i].self);
}

// Collision episodes: a pair's impacts closer than 500 ms apart are one; an
// episode is its first impact.
std::map<std::pair<int, int>, std::vector<Hit>> episodes(const Machine& m) {
    std::map<std::pair<int, int>, std::vector<Hit>> byPair;
    for (const Hit& h : m.hits)
        byPair[{std::min(h.a, h.b), std::max(h.a, h.b)}].push_back(h);
    std::map<std::pair<int, int>, std::vector<Hit>> out;
    for (auto& [pair, hits] : byPair) {
        std::ranges::sort(hits, {}, &Hit::time);
        double last = -1e18;
        for (const Hit& h : hits) {
            if (h.time - last > 500.0)
                out[pair].push_back(h);
            last = h.time;
        }
    }
    return out;
}

// How far apart machine `m` drew two players' cars at wall time `w`.
bool drawnGap(const Machine& m, int a, int b, double w, double& gap) {
    const auto ia = m.drawn.find(a);
    const auto ib = m.drawn.find(b);
    V3 pa, pb;
    if (ia == m.drawn.end() || ib == m.drawn.end() || !at(ia->second, w, pa) || !at(ib->second, w, pb))
        return false;
    gap = (pa - pb).length();
    return true;
}

// The closest machine `m` drew the two cars within `window` ms of wall time
// `w` (the same collision a machine shows a little earlier or later).
bool closestGap(const Machine& m, int a, int b, double w, double window, double& gap) {
    const auto ia = m.drawn.find(a);
    if (ia == m.drawn.end())
        return false;
    bool any = false;
    gap = 1e30;
    for (const Drawn& d : ia->second) {
        if (d.wall < w - window || d.wall > w + window)
            continue;
        double g = 0;
        if (drawnGap(m, a, b, d.wall, g)) {
            gap = std::min(gap, g);
            any = true;
        }
    }
    return any;
}

} // namespace

int syncReport(const SyncReportOptions& o) {
    std::vector<Machine> ms;
    for (const auto& path : o.traces) {
        Machine m;
        if (!load(path, m)) {
            std::println(stderr, "cannot read {}", path);
            return 1;
        }
        if (m.self < 0)
            std::println(stderr, "{}: no line for this machine's own car", path);
        ms.push_back(std::move(m));
    }
    const double skipMs = o.skipSeconds * 1000.0;

    // 1. Where each machine draws each player's car, against where the
    // car's own player sees it at the same moment.
    std::println("Screen divergence: a player's car as another machine draws it, against its own player's");
    std::println("screen at the same wall-clock moment (metres; samples after the first {:.0f} s):",
                 o.skipSeconds);
    std::println("  {:<10} {:<10} {:>7} {:>7} {:>7} {:>7} {:>7} {:>7}", "viewer", "car of", "frames",
                 "median", "p90", "p99", "max", "rms");
    for (std::size_t v = 0; v < ms.size(); ++v) {
        for (std::size_t own = 0; own < ms.size(); ++own) {
            if (own == v || ms[own].self < 0)
                continue;
            const auto seen = ms[v].drawn.find(ms[own].self);
            const auto truth = ms[own].drawn.find(ms[own].self);
            if (seen == ms[v].drawn.end() || truth == ms[own].drawn.end())
                continue;
            Stats s;
            for (const Drawn& d : seen->second) {
                if (d.wall < ms[v].firstWall + skipMs || d.wall < ms[own].firstWall + skipMs)
                    continue;
                V3 p;
                if (at(truth->second, d.wall, p))
                    s.add((d.p - p).length());
            }
            if (s.v.empty())
                continue;
            const double rms = s.rms();
            std::println("  {:<10} {:<10} {:>7} {:>7.3f} {:>7.3f} {:>7.3f} {:>7.3f} {:>7.3f}", who(ms, v),
                         who(ms, own), s.v.size(), s.pct(0.5), s.pct(0.9), s.pct(0.99), s.pct(1.0), rms);
        }
    }

    // 2. Jumps: a car's move from one drawn frame to the next that its
    // velocity does not explain.
    std::println("\nJumps: a frame's move that the car's velocity does not explain (frames 100 ms apart");
    std::println("or more are left out):");
    std::println("  {:<10} {:<10} {:>7} {:>9} {:>9} {:>9} {:>8}", "machine", "car of", "frames", ">0.25 m",
                 std::format(">{:.2g} m", o.teleportMetres), "/minute", "max");
    for (std::size_t m = 0; m < ms.size(); ++m) {
        for (const auto& [id, series] : ms[m].drawn) {
            int small = 0, big = 0;
            double worst = 0;
            std::size_t frames = 0;
            for (std::size_t k = 1; k < series.size(); ++k) {
                const Drawn& a = series[k - 1];
                const Drawn& b = series[k];
                if (a.wall < ms[m].firstWall + skipMs)
                    continue;
                const double dt = (b.wall - a.wall) / 1000.0;
                if (dt <= 0.0 || dt > 0.1)
                    continue;
                ++frames;
                const V3 expected = a.p + (a.v + b.v) * (0.5 * dt);
                const double r = (b.p - expected).length();
                worst = std::max(worst, r);
                if (r > 0.25)
                    ++small;
                if (r > o.teleportMetres)
                    ++big;
            }
            if (frames == 0)
                continue;
            const double span =
                (series.back().wall - std::max(series.front().wall, ms[m].firstWall + skipMs)) / 60000.0;
            std::println("  {:<10} {:<10} {:>7} {:>9} {:>9} {:>9.2f} {:>8.3f}", who(ms, m),
                         id == ms[m].self ? std::string("(own)") : (id == 0 ? std::string("host")
                                                                            : std::format("client {}", id)),
                         frames, small, big, span > 0 ? static_cast<double>(big) / span : 0.0, worst);
        }
    }

    // 3. Collisions between players: episodes (impacts of a pair less than
    // 500 ms apart) in each machine's simulation; whether the other machines
    // that report that pair had one within 750 ms of session time and 4 m;
    // and how far apart each machine drew the two cars at that moment
    // (touching cars' centres are 2-5 m apart).
    std::println("\nCollisions between players' cars (episodes; matched: another machine reporting the");
    std::println("pair had one within 750 ms of session time and 4 m; gap: the two cars' centres as each");
    std::println("machine drew them at that moment, and the closest they came within 0.6 s of it; cars");
    std::println("drawn less than 5 m apart are touching):");
    std::vector<std::map<std::pair<int, int>, std::vector<Hit>>> eps;
    for (const auto& m : ms)
        eps.push_back(episodes(m));
    std::set<std::pair<int, int>> pairs;
    for (const auto& e : eps)
        for (const auto& [pair, list] : e)
            pairs.insert(pair);
    if (pairs.empty())
        std::println("  none");
    for (const auto& pair : pairs) {
        for (std::size_t m = 0; m < ms.size(); ++m) {
            const auto it = eps[m].find(pair);
            if (it == eps[m].end())
                continue;
            std::string line = std::format("  {}-{} on {:<9}: {:>3} episodes", pair.first, pair.second,
                                           who(ms, m), it->second.size());
            for (std::size_t n = 0; n < ms.size(); ++n) {
                if (n == m)
                    continue;
                const auto other = eps[n].find(pair);
                int matched = 0;
                for (const Hit& h : it->second) {
                    const auto near = [&h](const Hit& u) {
                        return std::abs(u.time - h.time) <= 750.0 && (u.p - h.p).length() <= 4.0;
                    };
                    if (other != eps[n].end() && std::ranges::any_of(other->second, near))
                        ++matched;
                }
                line += std::format(", {} also on {}", matched, who(ms, n));
            }
            std::println("{}", line);
            for (std::size_t n = 0; n < ms.size(); ++n) {
                Stats gaps, closest;
                int touching = 0;
                for (const Hit& h : it->second) {
                    double g = 0;
                    if (drawnGap(ms[n], pair.first, pair.second, h.wall, g))
                        gaps.add(g);
                    if (closestGap(ms[n], pair.first, pair.second, h.wall, 600.0, g)) {
                        closest.add(g);
                        touching += g < 5.0 ? 1 : 0;
                    }
                }
                if (!gaps.v.empty())
                    std::println("      drawn gap on {:<9}: median {:.2f} m, largest {:.2f} m; "
                                 "closest within 0.6 s median {:.2f} m, largest {:.2f} m ({} of {} touching)",
                                 who(ms, n), gaps.pct(0.5), gaps.pct(1.0), closest.pct(0.5), closest.pct(1.0),
                                 touching, closest.v.size());
            }
        }
    }

    // 4. Corrections of a predicted car by the host's states.
    bool anyCorrections = false;
    for (std::size_t m = 0; m < ms.size(); ++m) {
        const auto& cs = ms[m].corrections;
        if (cs.empty())
            continue;
        if (!anyCorrections)
            std::println("\nCorrections of each machine's own car by the host's state (metres):");
        anyCorrections = true;
        Stats size, replayed;
        int over1 = 0, snaps = 0, over10cm = 0;
        for (const auto& c : cs) {
            const double d = c.dx.length();
            size.add(d);
            replayed.add(c.replayed);
            over1 += d > 1.0 ? 1 : 0;
            over10cm += d > 0.1 ? 1 : 0;
            snaps += c.snapped ? 1 : 0;
        }
        const double span = (cs.back().wall - cs.front().wall) / 60000.0;
        std::println("  {:<10} {} corrections ({:.1f} a minute), median {:.4f}, p90 {:.4f}, p99 {:.4f}, "
                     "max {:.3f}; {} over 10 cm, {} over 1 m, {} snapped; steps replayed median {:.0f}, "
                     "max {:.0f}",
                     who(ms, m), cs.size(), span > 0 ? static_cast<double>(cs.size()) / span : 0.0,
                     size.pct(0.5), size.pct(0.9), size.pct(0.99), size.pct(1.0), over10cm, over1, snaps,
                     replayed.pct(0.5), replayed.pct(1.0));
    }
    return 0;
}

} // namespace mm2::netprobe
