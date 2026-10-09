// netprobe rulesreport: whether the machines of a network race agree on its
// rules. Reads each machine's OPENMM2_NET_TRACE file, the host's first:
//
//   RS / RX  what each machine's HUD showed of its own car's checkpoints
//            (and took back)
//   RH       the host's referee counting a car's checkpoints
//   RG       a referee's finish that was not the race's (a build where the
//            players still decided their own: the host's measure of it)
//   RF       a finish in a machine's results
//   RE       a machine's "Place: n/N"
//   CG / CS  Cops and Robbers' gold events and scores as each machine shows
//            them
//
// The machines must have run on one computer: the clock is the machine's
// monotonic clock, which every process on it shares.

#include "RulesReport.h"

#include <algorithm>
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

constexpr unsigned kDnfMs = 86400000;

struct Shown {
    double clock = 0, frame = 0;
    int index = 0, count = 0, kind = 0;
    bool takenBack = false;
};
struct HostHit {
    double clock = 0, frame = 0;
    int player = 0;
    unsigned sample = 0;
    int index = 0, count = 0, lap = 0;
};
struct Finish {
    double clock = 0;
    unsigned ms = 0;
};
struct Gold {
    double clock = 0;
    int type = 0, car = 0, value = 0;
};

struct Machine {
    std::string path;
    int self = -1;
    std::vector<Shown> shown; // its own car's, as its HUD showed them
    std::vector<HostHit> hostHits;
    std::map<int, Finish> finishes, refereeFinishes;
    std::vector<std::pair<int, int>> standings;
    std::vector<Gold> gold;
    std::map<int, int> scores;
    double firstClock = 0, lastClock = 0;
};

bool load(const std::string& path, Machine& m) {
    std::ifstream in(path);
    if (!in)
        return false;
    m.path = path;
    std::string line;
    while (std::getline(in, line)) {
        const auto space = line.find(' ');
        if (space == std::string::npos)
            continue;
        const std::string tag = line.substr(0, space);
        std::istringstream s(line.substr(space + 1));
        double clock = 0, frame = 0;
        if (!(s >> clock >> frame))
            continue;
        if (m.firstClock == 0)
            m.firstClock = clock;
        m.lastClock = std::max(m.lastClock, clock);
        if (tag == "D") {
            int id = 0, own = 0;
            if (s >> id >> own && own)
                m.self = id;
        } else if (tag == "RS") {
            Shown e{clock, frame};
            int player = 0;
            if (s >> player >> e.index >> e.count >> e.kind) {
                m.self = player;
                m.shown.push_back(e);
            }
        } else if (tag == "RX") {
            int player = 0, index = 0;
            if (!(s >> player >> index))
                continue;
            // The newest showing of that waypoint not taken back yet.
            for (auto it = m.shown.rbegin(); it != m.shown.rend(); ++it)
                if (it->index == index && !it->takenBack) {
                    it->takenBack = true;
                    break;
                }
        } else if (tag == "RH") {
            HostHit h{clock, frame};
            if (s >> h.player >> h.sample >> h.index >> h.count >> h.lap)
                m.hostHits.push_back(h);
        } else if (tag == "RF" || tag == "RG") {
            int player = 0;
            unsigned ms = 0;
            if (!(s >> player >> ms))
                continue;
            auto& map = tag == "RF" ? m.finishes : m.refereeFinishes;
            map.try_emplace(player, Finish{clock, ms});
        } else if (tag == "RE") {
            int place = 0, racers = 0;
            if (s >> place >> racers)
                m.standings.emplace_back(place, racers);
        } else if (tag == "CG") {
            Gold g{clock};
            if (s >> g.type >> g.car >> g.value)
                m.gold.push_back(g);
        } else if (tag == "CS") {
            std::size_t n = 0;
            if (!(s >> n))
                continue;
            m.scores.clear();
            for (std::size_t i = 0; i < n; ++i) {
                int id = 0, score = 0;
                if (s >> id >> score)
                    m.scores[id] = score;
            }
        }
    }
    return true;
}

// A hit's identity: the waypoint and how many times the car hit it before.
using Key = std::pair<int, int>;
std::vector<Key> keys(const std::vector<int>& seq) {
    std::vector<Key> out;
    std::map<int, int> seen;
    for (int i : seq)
        out.emplace_back(i, seen[i]++);
    return out;
}

double percentile(std::vector<double> v, double p) {
    if (v.empty())
        return 0.0;
    std::ranges::sort(v);
    const auto k = static_cast<std::size_t>(p * static_cast<double>(v.size() - 1) + 0.5);
    return v[std::min(k, v.size() - 1)];
}

std::string time(unsigned ms) {
    if (ms >= kDnfMs)
        return "DNF";
    return std::format("{}:{:02}.{:03}", ms / 60000, ms / 1000 % 60, ms % 1000);
}

void checkpoints(const Machine& host, const Machine& m, double endClock) {
    if (m.self < 0) {
        std::println("  {}: no player of its own in the trace", m.path);
        return;
    }
    std::vector<int> counted;
    std::vector<const HostHit*> hostHits;
    for (const auto& h : host.hostHits)
        if (h.player == m.self) {
            counted.push_back(h.index);
            hostHits.push_back(&h);
        }
    std::vector<int> finalSeq;
    std::vector<const Shown*> finalShown;
    int predicted = 0, fromHost = 0, takenBack = 0;
    for (const auto& s : m.shown) {
        (s.kind ? fromHost : predicted) += 1;
        if (s.takenBack) {
            ++takenBack;
            continue;
        }
        finalSeq.push_back(s.index);
        finalShown.push_back(&s);
    }
    const auto hostKeys = keys(counted);
    const auto shownKeys = keys(finalSeq);
    int shownNotCounted = 0, pendingAtEnd = 0, countedNotShown = 0;
    std::vector<double> leadPredicted, lagHostWord;
    for (std::size_t i = 0; i < shownKeys.size(); ++i) {
        const auto it = std::ranges::find(hostKeys, shownKeys[i]);
        if (it == hostKeys.end()) {
            (finalShown[i]->clock > endClock - 1000.0 ? pendingAtEnd : shownNotCounted) += 1;
            continue;
        }
        const double dt = finalShown[i]->clock - hostHits[static_cast<std::size_t>(it - hostKeys.begin())]->clock;
        (finalShown[i]->kind ? lagHostWord : leadPredicted).push_back(dt);
    }
    for (const auto& k : hostKeys)
        if (std::ranges::find(shownKeys, k) == shownKeys.end())
            ++countedNotShown;
    const int hostLaps = static_cast<int>(std::ranges::count(counted, 0));
    const int shownLaps = static_cast<int>(std::ranges::count(finalSeq, 0));
    std::println("  player {} ({}): host counted {} checkpoints ({} at gate 0); its HUD showed {} ({} as "
                 "predicted, {} on the host's word), took {} back",
                 m.self, m.path, counted.size(), hostLaps, m.shown.size(), predicted, fromHost, takenBack);
    std::println("    shown and never counted by the host: {} (+{} still on their way at the end); counted and "
                 "not shown: {}; gate-0 passes shown {} against {}",
                 shownNotCounted, pendingAtEnd, countedNotShown, shownLaps, hostLaps);
    if (!leadPredicted.empty())
        std::println("    predicted ones shown {:.0f} ms before the host counted them (median; 1% {:.0f}, 99% {:.0f})",
                     -percentile(leadPredicted, 0.5), -percentile(leadPredicted, 0.99),
                     -percentile(leadPredicted, 0.01));
    if (!lagHostWord.empty())
        std::println("    the host's own shown {:.0f} ms after it counted them (median; 99% {:.0f})",
                     percentile(lagHostWord, 0.5), percentile(lagHostWord, 0.99));
}

void finishes(const std::vector<Machine>& machines) {
    std::set<int> players;
    for (const auto& m : machines)
        for (const auto& [id, f] : m.finishes)
            players.insert(id);
    // The race's referee: a build that kept the players' own word has the
    // host's measure in RG; otherwise the host's results are the referee's.
    const Machine& host = machines.front();
    const auto& referee = host.refereeFinishes.empty() ? host.finishes : host.refereeFinishes;
    int agree = 0, total = 0, offReferee = 0;
    for (const int id : players) {
        std::string line = std::format("  player {}:", id);
        std::set<unsigned> values;
        for (const auto& m : machines) {
            const auto it = m.finishes.find(id);
            line += it == m.finishes.end() ? " -" : " " + time(it->second.ms);
            if (it != m.finishes.end())
                values.insert(it->second.ms);
        }
        if (const auto r = referee.find(id); r != referee.end()) {
            line += std::format("  (the host's measure {})", time(r->second.ms));
            for (const auto& m : machines)
                if (const auto it = m.finishes.find(id); it != m.finishes.end() && it->second.ms != r->second.ms)
                    ++offReferee;
        }
        ++total;
        bool all = values.size() == 1;
        for (const auto& m : machines)
            all = all && m.finishes.contains(id);
        agree += all ? 1 : 0;
        std::println("{}", line);
    }
    std::println("  {} of {} finishes the same on every machine; {} results differ from the host's measure", agree,
                 total, offReferee);
    auto order = [](const Machine& m) {
        std::vector<std::pair<unsigned, int>> v;
        for (const auto& [id, f] : m.finishes)
            v.emplace_back(f.ms, id);
        std::ranges::sort(v);
        std::vector<int> ids;
        for (const auto& [ms, id] : v)
            ids.push_back(id);
        return ids;
    };
    bool same = true;
    for (const auto& m : machines)
        same = same && order(m) == order(machines.front());
    std::println("  finishing order the same on every machine: {}", same ? "yes" : "no");
}

void gold(const std::vector<Machine>& machines) {
    // The events that matter (taken, dropped, delivered, limits) in order,
    // a client's predicted pickup that it took back left out.
    auto events = [](const Machine& m, int* undone) {
        std::vector<std::pair<int, int>> out;
        for (const auto& g : m.gold) {
            if (g.type == 7) { // PickupUndone
                for (auto it = out.rbegin(); it != out.rend(); ++it)
                    if (it->first == 0 && it->second == g.car) {
                        out.erase(std::next(it).base());
                        break;
                    }
                ++*undone;
                continue;
            }
            if (g.type == 3 || g.type == 4) // NewSet, TimeWarning
                continue;
            out.emplace_back(g.type, g.car);
        }
        return out;
    };
    int hostUndone = 0;
    const auto host = events(machines.front(), &hostUndone);
    std::println("  host: {} gold events, scores{}", host.size(),
                 [&] {
                     std::string s;
                     for (const auto& [id, sc] : machines.front().scores)
                         s += std::format(" {}={}", id, sc);
                     return s;
                 }());
    for (std::size_t i = 1; i < machines.size(); ++i) {
        int undone = 0;
        const auto mine = events(machines[i], &undone);
        // The longest common subsequence: what both had in the same order.
        std::vector<std::vector<int>> lcs(host.size() + 1, std::vector<int>(mine.size() + 1, 0));
        for (std::size_t a = 1; a <= host.size(); ++a)
            for (std::size_t b = 1; b <= mine.size(); ++b)
                lcs[a][b] = host[a - 1] == mine[b - 1] ? lcs[a - 1][b - 1] + 1
                                                        : std::max(lcs[a - 1][b], lcs[a][b - 1]);
        const int common = lcs[host.size()][mine.size()];
        std::string scores;
        for (const auto& [id, sc] : machines[i].scores)
            scores += std::format(" {}={}", id, sc);
        std::println("  {}: {} gold events; host's missing here {}, here and not on the host {}; pickups "
                     "predicted and undone {}; scores{} ({})",
                     machines[i].path, mine.size(), static_cast<int>(host.size()) - common,
                     static_cast<int>(mine.size()) - common, undone, scores,
                     machines[i].scores == machines.front().scores ? "the same" : "different");
    }
}

} // namespace

int rulesReport(const RulesReportOptions& options) {
    if (options.traces.empty()) {
        std::println(stderr, "rulesreport: no traces");
        return 1;
    }
    std::vector<Machine> machines(options.traces.size());
    for (std::size_t i = 0; i < options.traces.size(); ++i)
        if (!load(options.traces[i], machines[i])) {
            std::println(stderr, "rulesreport: cannot read {}", options.traces[i]);
            return 1;
        }
    const Machine& host = machines.front();
    const bool cops = std::ranges::any_of(machines, [](const Machine& m) { return !m.gold.empty(); });
    if (!host.hostHits.empty() || !cops) {
        std::println("checkpoints (each machine's HUD against the host's referee):");
        for (const auto& m : machines)
            checkpoints(host, m, std::min(host.lastClock, m.lastClock));
        std::println("finishes (each machine's results):");
        finishes(machines);
        for (const auto& m : machines)
            if (!m.standings.empty())
                std::println("  {}: \"Place\" changed {} times, last {}/{}", m.path, m.standings.size(),
                             m.standings.back().first, m.standings.back().second);
    }
    if (cops) {
        std::println("Cops and Robbers:");
        gold(machines);
    }
    return 0;
}

} // namespace mm2::netprobe
