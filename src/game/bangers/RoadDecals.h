#pragma once

#include "city/PathSet.h"
#include "game/TextureLibrary.h"
#include "render/Device.h"

#include <string>
#include <vector>

namespace mm2::game::bangers {

// Road decals (dgRoadDecalInstance) from city/<map>/decals.pathset: each
// path of more than two points is a strip of point pairs, 1 cm above the
// road, textured with the texture named like the path; u is 0 on a pair's
// first point and 1 on its second, v = floor(distance of the pair's first
// point from the strip's start / the first pair's width + 0.5). Drawn in
// the shadow pass: alpha blended, no depth writes, pulled towards the
// viewer, unlit with the room colour (white in retail, see rendering.md).
class RoadDecals {
public:
    void load(const city::PathSet& set);
    std::size_t size() const { return m_decals.size(); }

    // Inside the scene pass, after the street geometry.
    void draw(render::Device& device, TextureLibrary& textures) const;

    struct Decal {
        std::string texture;
        std::vector<Vec3> points;
        std::vector<float> v; // per pair
    };
    const std::vector<Decal>& decals() const { return m_decals; }

private:
    std::vector<Decal> m_decals;
};

} // namespace mm2::game::bangers
