#include "game/net/RulesTrace.h"

#include <chrono>

namespace mm2::game {
namespace {

// The machine's monotonic clock (every process on it shares it).
double machineMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double, std::milli>(now).count();
}

} // namespace

void traceRuleShown(std::FILE* f, double frame, int player, int index, int count, int kind) {
    if (f)
        std::fprintf(f, "RS %.3f %.3f %d %d %d %d\n", machineMs(), frame, player, index, count, kind);
}

void traceRuleTakenBack(std::FILE* f, double frame, int player, int index) {
    if (f)
        std::fprintf(f, "RX %.3f %.3f %d %d\n", machineMs(), frame, player, index);
}

void traceRuleHostHit(std::FILE* f, double frame, int player, std::uint32_t sample, int index, int count,
                      int lap) {
    if (f)
        std::fprintf(f, "RH %.3f %.3f %d %u %d %d %d\n", machineMs(), frame, player, sample, index, count,
                     lap);
}

void traceRuleFinish(std::FILE* f, double frame, int player, std::uint32_t ms) {
    if (f)
        std::fprintf(f, "RF %.3f %.3f %d %u\n", machineMs(), frame, player, ms);
}

void traceRuleStanding(std::FILE* f, double frame, int place, int racers) {
    if (f)
        std::fprintf(f, "RE %.3f %.3f %d %d\n", machineMs(), frame, place, racers);
}

void traceCopsEvent(std::FILE* f, double frame, int type, int car, int value, const Vec3& gold) {
    if (f)
        std::fprintf(f, "CG %.3f %.3f %d %d %d %.2f %.2f %.2f\n", machineMs(), frame, type, car, value, gold.x,
                     gold.y, gold.z);
}

void traceCopsScores(std::FILE* f, double frame, const std::vector<std::pair<int, int>>& scores) {
    if (!f)
        return;
    std::fprintf(f, "CS %.3f %.3f %zu", machineMs(), frame, scores.size());
    for (const auto& [id, score] : scores)
        std::fprintf(f, " %d %d", id, score);
    std::fprintf(f, "\n");
}

} // namespace mm2::game
