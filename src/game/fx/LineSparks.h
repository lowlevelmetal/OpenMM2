#pragma once

#include "game/fx/Random.h"
#include "render/Device.h"
#include "vfs/Vfs.h"

#include <cstdint>
#include <span>
#include <vector>

namespace mm2::game::fx {

// asSparkLut: spark colours from texture/spark.tga (an uncompressed Targa of
// at most 256 pixels with power-of-two sides, read in file order; 24-bit
// pixels get alpha 0x80). Rows are colour ramps; a spark's age picks the
// column.
struct SparkLut {
    std::vector<std::uint32_t> colors; // 0xAARRGGBB
    int shift = 5;                     // age >> shift = column
    int rows = 4;

    static SparkLut builtin();
    // Null (the built-in table) when the file is missing or unsupported.
    static SparkLut load(const vfs::Vfs& vfs, std::string_view texture = "spark");
};

// asLineSparks: up to 64 sparks drawn as short lines, thrown out around an
// impact normal (vehCarDamage::ApplyImpact above 15 mph).
class LineSparks {
public:
    static constexpr int kMax = 64; // vehCarDamage::Init

    explicit LineSparks(SparkLut lut = SparkLut::builtin());

    // asLineSparks::RadialBlast: `count` sparks from `position`, 4-5 m/s
    // along the normal and 6-7 m/s across it in a random direction.
    void radialBlast(int count, const Vec3& position, const Vec3& normal);
    // asLineSparks::Update: steps of at least 1/30 s of accumulated time.
    void update(float dt);
    void draw(render::Device& device) const;
    void reset() { m_count = 0; }

    int count() const { return m_count; }
    struct Spark {
        Vec3 position, tail, velocity;
        std::uint8_t row = 0; // colour row offset into the table
        std::uint8_t age = 0; // counts down; the colour column is age >> shift
        std::uint32_t color = 0;
    };
    std::span<const Spark> sparks() const { return {m_sparks.data(), static_cast<std::size_t>(m_count)}; }
    Rand& rng() { return m_rand; }

private:
    void step(float dt);

    SparkLut m_lut;
    std::vector<Spark> m_sparks;
    int m_count = 0;
    float m_accumulator = 0.0f;
    Rand m_rand{0x5BA4u};
};

} // namespace mm2::game::fx
