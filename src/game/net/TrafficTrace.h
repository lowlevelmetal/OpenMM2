#pragma once

// Development aid for the shared traffic of a network cruise (OpenMM2 extra):
// with OPENMM2_NET_TRACE=<file> set, each machine writes the shared cars as it
// has them, one line each, next to the players' cars' lines (docs/
// multiplayer.md, "Diagnosing replication"). `mm2tool nettrace` compares a
// host's trace with its clients' (docs/review/multiplayer-desync-traffic.md).
//
//   TH time id gen kind x y z fx fz speed flags target vx vy vz
//       host: a car as its simulation has it at session time `time` (its own:
//       the AI step's for a car on its rail, the physics step's for a body)
//   TC frame time id gen kind x y z fx fz speed flags target mode
//       client: a car where its collisions meet it (the physics proxy) for the
//       frame that began at session time `frame`, placed for session time
//       `time`; mode 0 interpolated, 1 extrapolated, 2 predicted
//   TV frame time x y z
//       client: where this machine's car is for the same frame, and its time
//   TX time id gen          client: reported hitting car id
//   TK time id gen player   host: knocked car id off its rail (player 255:
//                           its own simulation's collision)
//   TR time from id gen why host: a client's hit report it did not apply
//   TL time id confirmed off
//                           client: a car it knocked loose went back to the
//                           host's messages (confirmed 1: the host knocked it
//                           too), its drawing `off` metres from them then
//
// (fx, fz) is the car's forward direction (-m2) on the ground plane.

#include "game/net/TrafficSync.h"

#include <cstdint>
#include <cstdio>
#include <string_view>

namespace mm2::game {

void traceHostCar(std::FILE* f, double time, const SharedCar& car);
void traceClientCar(std::FILE* f, double frame, double time, const TrafficClient::Car& car, int mode);
void traceClientView(std::FILE* f, double frame, double time, const Vec3& position);
void traceHit(std::FILE* f, double time, int id, int generation);
void traceKnock(std::FILE* f, double time, int id, int generation, int player);
void traceRefusedHit(std::FILE* f, double time, int from, int id, int generation, std::string_view why);
void traceHandover(std::FILE* f, double time, int id, bool confirmed, float off);

} // namespace mm2::game
