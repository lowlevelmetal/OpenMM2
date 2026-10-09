// The shared traffic in the OPENMM2_NET_TRACE file (development aid). See
// TrafficTrace.h.
#include "game/net/TrafficTrace.h"

namespace mm2::game {
namespace {

void traceCar(std::FILE* f, const Mat34& m, float speed, std::uint8_t flags, std::uint8_t target) {
    std::fprintf(f, " %.3f %.3f %.3f %.4f %.4f %.3f %u %d", m.m3.x, m.m3.y, m.m3.z, -m.m2.x, -m.m2.z, speed,
                 flags, target == net::kAmbientNoTarget ? -1 : static_cast<int>(target));
}

// The generation as it travels (its low bits).
int generationOf(int generation) { return generation % static_cast<int>(net::kAmbientGenerations); }

} // namespace

void traceHostCar(std::FILE* f, double time, const SharedCar& car) {
    if (!f)
        return;
    const int kind = static_cast<int>(car.kind);
    std::fprintf(f, "TH %.3f %d %d %d", time, car.id, generationOf(car.generation), kind);
    traceCar(f, car.transform, car.speed, car.flags, car.target);
    std::fprintf(f, " %.3f %.3f %.3f\n", car.velocity.x, car.velocity.y, car.velocity.z);
}

void traceClientCar(std::FILE* f, double frame, double time, const TrafficClient::Car& car, int mode) {
    if (!f)
        return;
    std::fprintf(f, "TC %.3f %.3f %d %d %d", frame, time, car.id, car.generation, static_cast<int>(car.kind));
    traceCar(f, car.transform, car.speed, car.flags, car.target);
    std::fprintf(f, " %d\n", mode);
}

void traceClientView(std::FILE* f, double frame, double time, const Vec3& position) {
    if (f)
        std::fprintf(f, "TV %.3f %.3f %.3f %.3f %.3f\n", frame, time, position.x, position.y, position.z);
}

void traceHit(std::FILE* f, double time, int id, int generation) {
    if (f)
        std::fprintf(f, "TX %.3f %d %d\n", time, id, generation);
}

void traceKnock(std::FILE* f, double time, int id, int generation, int player) {
    if (f)
        std::fprintf(f, "TK %.3f %d %d %d\n", time, id, generationOf(generation), player);
}

void traceHandover(std::FILE* f, double time, int id, bool confirmed, float off) {
    if (f)
        std::fprintf(f, "TL %.3f %d %d %.3f\n", time, id, confirmed ? 1 : 0, off);
}

} // namespace mm2::game
