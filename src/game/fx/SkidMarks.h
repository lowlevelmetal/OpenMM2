/*
    OpenMM2 - tyre tracks (MM2's lvlTrackManager, laid by vehCar::UpdateTrack).
*/
#pragma once

#include "game/TextureLibrary.h"
#include "render/Device.h"

#include <vector>

namespace mm2::game::fx {

// lvlTrackManager: one wheel's tyre track, a ring of vertex pairs drawn as
// triangle strips with texture/tire_track.tga.
//
// While the wheel lays a track, each update builds the pair contact point
// -+ half the tyre width along the wheel's axle. The first pair is held back
// until the wheel has moved 10 cm; it then starts a strip (texture v = 0)
// with a second pair at the wheel. While the wheel keeps its direction
// (within acos 0.99) and stays within 10 m of the last fixed pair, the
// newest pair just follows the wheel; otherwise a new pair is added. A
// pair's v is its distance from the last fixed pair in tyre widths, so the
// texture repeats once per width along the track. When the ring is full the
// oldest pair is dropped (and a strip's leftover first pair with it).
class SkidTrack {
public:
    // vehCar::Init gives each wheel 64 pairs; the count must be a power of two.
    explicit SkidTrack(int pairs = 64);

    // lvlTrackManager::Init: the tyre width (vehWheel Width).
    void setWidth(float width);
    void reset();

    // lvlTrackManager::Update. `axle`: the wheel matrix's first row (world),
    // `contact`: the ground contact point. `laying` false ends the strip.
    void update(const Vec3& contact, const Vec3& axle, bool laying);

    struct Pair {
        Vec3 left, right;
        float v = 0.0f; // 0 starts a strip
    };
    // Strips from oldest to newest, each a run of pairs (lvlTrackManager::Draw).
    template <class Fn>
    void forEachStrip(Fn&& fn) const {
        std::vector<const Pair*> strip;
        std::size_t i = m_tail;
        while (i != m_head) {
            strip.clear();
            do {
                strip.push_back(&m_pairs[i]);
                i = (i + 1) & m_mask;
            } while (i != m_head && m_pairs[i].v != 0.0f);
            fn(strip);
        }
    }
    std::size_t pairCount() const { return (m_head - m_tail) & m_mask; }
    bool laying() const { return m_laying; }

private:
    void push(const Pair& p);

    std::vector<Pair> m_pairs;
    std::size_t m_mask = 0;
    std::size_t m_head = 0, m_tail = 0;
    float m_halfWidth = 0.05f, m_invWidth = 10.0f;
    bool m_laying = false;  // a strip is open
    bool m_pending = false; // the first pair waits for the wheel to move
    Pair m_pendingPair;
    Vec3 m_last;      // where the last fixed pair was laid
    Vec3 m_direction; // direction of the last segment
    bool m_directionValid = false;
};

// Draws tracks with texture/tire_track.tga: u across the tyre (clamped,
// vehCar::Init sets the texture's clamp-U flag), v along it (repeating),
// white vertex colour, alpha blended, no depth writes. MM2's vehCar::
// DrawTracks also turns the depth test off and relies on drawing them right
// after the static city; OpenMM2 keeps the test (with a depth bias) because
// its draw order differs.
class SkidRenderer {
public:
    void draw(render::Device& device, TextureLibrary& textures, const std::vector<const SkidTrack*>& tracks);
};

} // namespace mm2::game::fx
