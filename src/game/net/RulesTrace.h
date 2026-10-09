#pragma once

// Development aid for the rules of a network race: with OPENMM2_NET_TRACE=
// <file> set, each machine writes what its rules showed and decided, one
// line each, next to the players' cars' lines (docs/multiplayer.md,
// "Diagnosing replication"). `netprobe rulesreport` compares a host's trace
// with its clients' (docs/review/multiplayer-desync-rules.md). `clock` is the
// machine's monotonic clock (shared by the processes on one computer),
// `frame` the frame's session time.
//
//   RS clock frame player index count kind
//       this machine's HUD showed its car clearing waypoint `index` (`count`
//       waypoints passed after it); kind 0 as it predicted it, 1 when the
//       host's word brought it
//   RX clock frame player index
//       the host's word took a waypoint this machine had shown back
//   RH clock frame player sample index count lap
//       host: its referee counted player's car clearing `index` after the
//       car's sample `sample`
//   RF clock frame player ms
//       this machine's results took player's finish (ms 86400000: did not)
//   RE clock frame place racers
//       this machine's "Place: n/N" changed
//   CG clock frame type car value
//       Cops and Robbers: this machine's HUD showed a gold event
//       (CopsAndRobbers::EventType; value: the points, or 1 for a hit)
//   CS clock frame n id score ...
//       Cops and Robbers: every player's score, when one changed

#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace mm2::game {

void traceRuleShown(std::FILE* f, double frame, int player, int index, int count, int kind);
void traceRuleTakenBack(std::FILE* f, double frame, int player, int index);
void traceRuleHostHit(std::FILE* f, double frame, int player, std::uint32_t sample, int index, int count,
                      int lap);
void traceRuleFinish(std::FILE* f, double frame, int player, std::uint32_t ms);
void traceRuleStanding(std::FILE* f, double frame, int place, int racers);
void traceCopsEvent(std::FILE* f, double frame, int type, int car, int value);
void traceCopsScores(std::FILE* f, double frame, const std::vector<std::pair<int, int>>& scores);

} // namespace mm2::game
