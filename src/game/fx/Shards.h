#pragma once

#include "game/TextureLibrary.h"
#include "game/fx/Random.h"
#include "render/Device.h"

#include <array>
#include <string>
#include <vector>

namespace mm2::game::fx {

// fxShardManager: 16 glass/paint shards per car, thrown from hard impacts.
// Each is a small right triangle (0.1 m legs) textured with a random
// 0.3 x 0.3 patch of one of the car's paint materials (shard i takes
// material i), tumbling and falling for 1.8 s.
class Shards {
public:
    static constexpr int kCount = 16;
    static constexpr float kLifetime = 1.8f;

    // fxShardManager::EmitShards: above impact 500 and 5 m/s, impact / 300
    // shards (at most 2) from `position` with the car's speed, thrown
    // sideways, up and back in the car's body frame.
    void emit(const Vec3& position, float impact, float speed, const Mat34& body);
    // fxShard::Update.
    void update(float dt);
    // `textures`: the car's paint job material textures, in order.
    void draw(render::Device& device, TextureLibrary& textures, const std::vector<std::string>& materials) const;
    void reset();

    struct Shard {
        Mat34 frame;  // rotation and position
        Vec3 velocity;
        Vec3 axis;    // spin axis
        float spin = 0.0f;
        float age = 3.4e38f; // dead until thrown
        float u = 0.5f, v = 0.5f;
    };
    const std::array<Shard, kCount>& shards() const { return m_shards; }
    int live() const;
    Rand& rng() { return m_rand; }

private:
    void emitOne(const Vec3& position, float speed, const Mat34& body);

    std::array<Shard, kCount> m_shards{};
    int m_next = 0;
    Rand m_rand{0x5A4Du};
};

} // namespace mm2::game::fx
