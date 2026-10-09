// netprobe propdiff <trace A> <trace B>: how two machines' props differ in a
// network race, from their OPENMM2_DEBUG_NETPROPS traces (game::PropTrace,
// docs/multiplayer.md "Props"). Same-named session times ("T" ticks every
// 250 ms) are compared:
//
//   knocks     the placed props knocked by the end on both, on one only, and
//              how far apart in session time both knocked them
//   standing   at every common tick, the props knocked on one machine but
//              standing on the other
//   resting    at the last common tick, how far apart the same knocked-over
//              prop or thrown part rests (both at rest), and the ones only
//              one machine shows
//   damage     each car's damage at the common ticks
//   impacts    the damaging impacts on each machine's own car, by cause

#include "PropDiff.h"

#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <format>
#include <fstream>
#include <map>
#include <numeric>
#include <print>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace mm2::netprobe {
namespace {

struct Shown {
    double x = 0, y = 0, z = 0;
    bool moving = false;
    bool local = false;
};
struct Tick {
    std::set<long> broken;
    std::map<std::string, Shown> shown;
    std::map<std::string, double> damage; // level, 0..1
    std::map<std::string, double> dents;
};
struct Knock {
    double time = 0;
    std::string model, cause;
};
struct Trace {
    std::string name;
    std::map<long, Knock> knocks; // first knock of each prop
    std::map<long, double> undone;
    std::map<long, Tick> ticks;
    std::map<std::string, int> impacts;
    std::map<std::string, double> impactValue;
};

bool load(const std::string& path, Trace& t) {
    std::ifstream in(path);
    if (!in)
        return false;
    t.name = path;
    std::string line;
    Tick* tick = nullptr;
    while (std::getline(in, line)) {
        std::istringstream s(line);
        std::string kind;
        s >> kind;
        if (kind == "K") {
            double time;
            long prop;
            Knock k;
            s >> time >> prop >> k.model >> k.cause;
            k.time = time;
            t.knocks.try_emplace(prop, k);
        } else if (kind == "X") {
            double time;
            long prop;
            s >> time >> prop;
            t.undone[prop] = time;
        } else if (kind == "T") {
            double at;
            s >> at;
            tick = &t.ticks[static_cast<long>(at)];
        } else if (kind == "B" && tick) {
            long id;
            while (s >> id)
                tick->broken.insert(id);
        } else if (kind == "H" && tick) {
            std::string what;
            Shown h;
            int moving = 0, local = 0;
            s >> what >> h.x >> h.y >> h.z >> moving >> local;
            h.moving = moving != 0;
            h.local = local != 0;
            tick->shown[what] = h;
        } else if (kind == "D" && tick) {
            std::string car;
            double level = 0, dents = 0;
            s >> car >> level >> dents;
            tick->damage[car] = level;
            tick->dents[car] = dents;
        } else if (kind == "I") {
            double time, value, damage;
            std::string cause;
            s >> time >> cause >> value >> damage;
            ++t.impacts[cause];
            t.impactValue[cause] += value;
        }
    }
    return true;
}

double percentile(std::vector<double> v, double p) {
    if (v.empty())
        return 0.0;
    std::ranges::sort(v);
    const double at = std::clamp(p, 0.0, 1.0) * static_cast<double>(v.size() - 1);
    const auto i = static_cast<std::size_t>(at + 0.5);
    return v[i];
}

std::string kindOf(const std::string& model) {
    if (model.starts_with("giz_pcar"))
        return "parked car";
    if (model.starts_with("sp_traflit"))
        return "traffic light";
    return "prop";
}

} // namespace

int propDiff(const std::string& pathA, const std::string& pathB) {
    Trace a, b;
    if (!load(pathA, a) || !load(pathB, b)) {
        std::println(stderr, "propdiff: cannot read the traces");
        return 1;
    }
    std::vector<long> common;
    for (const auto& [at, tick] : a.ticks)
        if (b.ticks.contains(at))
            common.push_back(at);
    if (common.empty()) {
        std::println(stderr, "propdiff: the traces share no session time");
        return 1;
    }
    std::println("A {}\nB {}\n{} common ticks, session {} .. {} ms", pathA, pathB, common.size(),
                 common.front(), common.back());

    // Knocks by the end (the last common tick's standing lists).
    const Tick& lastA = a.ticks[common.back()];
    const Tick& lastB = b.ticks[common.back()];
    std::map<std::string, std::array<int, 3>> byKind; // both, A only, B only
    std::vector<double> offsets;
    auto modelOf = [&](long id) {
        if (const auto it = a.knocks.find(id); it != a.knocks.end())
            return it->second.model;
        if (const auto it = b.knocks.find(id); it != b.knocks.end())
            return it->second.model;
        return std::string("?");
    };
    std::set<long> all = lastA.broken;
    all.insert(lastB.broken.begin(), lastB.broken.end());
    std::map<std::string, int> causesOnlyA, causesOnlyB;
    for (const long id : all) {
        const bool inA = lastA.broken.contains(id), inB = lastB.broken.contains(id);
        auto& k = byKind[kindOf(modelOf(id))];
        if (inA && inB) {
            ++k[0];
            if (a.knocks.contains(id) && b.knocks.contains(id))
                offsets.push_back(std::abs(a.knocks[id].time - b.knocks[id].time));
        } else if (inA) {
            ++k[1];
            ++causesOnlyA[a.knocks.contains(id) ? a.knocks[id].cause : "?"];
        } else {
            ++k[2];
            ++causesOnlyB[b.knocks.contains(id) ? b.knocks[id].cause : "?"];
        }
    }
    std::println("\nknocked by the end      both   A only   B only");
    for (const auto& [kind, k] : byKind)
        std::println("  {:<20} {:>6} {:>8} {:>8}", kind, k[0], k[1], k[2]);
    auto causes = [](const std::map<std::string, int>& m) {
        std::string s;
        for (const auto& [c, n] : m)
            s += std::format(" {}:{}", c, n);
        return s.empty() ? std::string(" -") : s;
    };
    std::println("  knocked on A only, by (A's view):{}", causes(causesOnlyA));
    std::println("  knocked on B only, by (B's view):{}", causes(causesOnlyB));
    if (!offsets.empty())
        std::println("  knock time apart (both): median {:.0f} ms, 90% {:.0f} ms, max {:.0f} ms",
                     percentile(offsets, 0.5), percentile(offsets, 0.9), percentile(offsets, 1.0));
    std::println("  undone predictions: A {}, B {}", a.undone.size(), b.undone.size());

    // Standing on one, knocked on the other, over time.
    std::vector<double> mismatch;
    int ticksWith = 0;
    double worst = 0;
    long worstAt = 0;
    for (const long at : common) {
        const Tick& ta = a.ticks[at];
        const Tick& tb = b.ticks[at];
        int n = 0;
        for (const long id : ta.broken)
            n += tb.broken.contains(id) ? 0 : 1;
        for (const long id : tb.broken)
            n += ta.broken.contains(id) ? 0 : 1;
        mismatch.push_back(n);
        ticksWith += n > 0 ? 1 : 0;
        if (n > worst) {
            worst = n;
            worstAt = at;
        }
    }
    double sum = 0;
    for (const double m : mismatch)
        sum += m;
    std::println("\nknocked on one, standing on the other: mean {:.2f}, max {:.0f} (at {} ms), at the end "
                 "{:.0f}; {:.1f}% of the ticks",
                 sum / static_cast<double>(mismatch.size()), worst, worstAt, mismatch.back(),
                 100.0 * ticksWith / static_cast<double>(mismatch.size()));

    // Where the knocked-over props and parts rest.
    std::vector<double> apart;
    std::map<std::string, std::vector<double>> apartByKind;
    int onlyA = 0, onlyB = 0, moving = 0;
    for (const auto& [what, ha] : lastA.shown) {
        const auto it = lastB.shown.find(what);
        if (it == lastB.shown.end()) {
            ++onlyA;
            continue;
        }
        const Shown& hb = it->second;
        if (ha.moving || hb.moving) {
            ++moving;
            continue;
        }
        const double d = std::sqrt((ha.x - hb.x) * (ha.x - hb.x) + (ha.y - hb.y) * (ha.y - hb.y) +
                                   (ha.z - hb.z) * (ha.z - hb.z));
        apart.push_back(d);
        const long id =
            what.starts_with('p') ? str::parseInt(what.substr(1, what.find('.') - 1)).value_or(-1) : -1;
        apartByKind[what.starts_with('c') ? "car part" : kindOf(modelOf(id))].push_back(d);
    }
    for (const auto& [what, hb] : lastB.shown)
        if (!lastA.shown.contains(what))
            ++onlyB;
    std::println("\nknocked-over props and parts at the end: {} on both ({} still moving), {} on A only, "
                 "{} on B only",
                 apart.size() + static_cast<std::size_t>(moving), moving, onlyA, onlyB);
    if (!apart.empty()) {
        const auto within = std::ranges::count_if(apart, [](double d) { return d <= 0.05; });
        std::println("  resting apart: median {:.3f} m, 90% {:.3f} m, max {:.3f} m; {} of {} within 5 cm",
                     percentile(apart, 0.5), percentile(apart, 0.9), percentile(apart, 1.0), within,
                     apart.size());
        for (const auto& [kind, v] : apartByKind)
            std::println("    {:<14} {:>3}: median {:.3f} m, max {:.3f} m", kind, v.size(),
                         percentile(v, 0.5), percentile(v, 1.0));
    }

    // The cars' damage: the level (0..1 between MedDamage and MaxDamage)
    // and the dents each machine shows.
    std::map<std::string, std::vector<double>> damage, dents;
    std::map<std::string, std::array<double, 4>> lastDamage;
    for (const long at : common)
        for (const auto& [car, da] : a.ticks[at].damage)
            if (const auto it = b.ticks[at].damage.find(car); it != b.ticks[at].damage.end()) {
                damage[car].push_back(std::abs(da - it->second));
                const double na = a.ticks[at].dents[car], nb = b.ticks[at].dents[car];
                dents[car].push_back(std::abs(na - nb));
                lastDamage[car] = {da, it->second, na, nb};
            }
    std::println("\ndamage (A vs B): level 0..1, dents");
    for (const auto& [car, v] : damage) {
        const auto& l = lastDamage[car];
        const auto& d = dents[car];
        const double meanLevel = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
        const double meanDents = std::accumulate(d.begin(), d.end(), 0.0) / static_cast<double>(d.size());
        std::println("  {:<10} level apart: mean {:.3f}, max {:.3f}; dents apart: mean {:.1f}, max {:.0f}; "
                     "at the end A {:.3f} / {:.0f} dents, B {:.3f} / {:.0f} dents",
                     car, meanLevel, percentile(v, 1.0), meanDents, percentile(d, 1.0), l[0], l[2], l[1],
                     l[3]);
    }
    auto impacts = [](const Trace& t) {
        std::string s;
        for (const auto& [c, n] : t.impacts)
            s += std::format(" {}:{} ({:.0f})", c, n, t.impactValue.at(c));
        return s.empty() ? std::string(" -") : s;
    };
    std::println("damaging impacts on A's own car:{}", impacts(a));
    std::println("damaging impacts on B's own car:{}", impacts(b));
    return 0;
}

} // namespace mm2::netprobe
