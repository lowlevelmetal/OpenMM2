#pragma once

// Development aid: OPENMM2_DEBUG_DRAW_TRACE=<file> writes, once a frame,
// where the race drew the player's car, a roadside point and the nearest
// other car, in the camera's frame, to measure how smoothly they move (see
// docs/rendering.md, "Drawing between simulation steps"). Off unless set.

#include "core/Math.h"

#include <cstdio>
#include <memory>
#include <optional>

namespace mm2::app {

class DrawTrace {
public:
    // A trace when OPENMM2_DEBUG_DRAW_TRACE names a file it can write.
    static std::unique_ptr<DrawTrace> fromEnvironment();
    explicit DrawTrace(std::FILE* file) : m_file(file, &std::fclose) {}

    // One frame: its time (s), the simulation steps it ran and the blend
    // between the last two (0 before interpolation), the camera, the car as
    // drawn and its speed (m/s), and the nearest other car as drawn.
    void frame(double dt, int steps, float alpha, const Mat34& camera, const Mat34& car, float speed,
               const std::optional<Mat34>& other);

private:
    std::unique_ptr<std::FILE, int (*)(std::FILE*)> m_file;
    std::optional<Vec3> m_point; // the roadside point (world)
    int m_segment = 0;           // counts the points chosen
    long m_frame = 0;
};

} // namespace mm2::app
