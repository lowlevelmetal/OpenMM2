// Parity tests for the city data the AI drivers read (ai-vehicles audit).
#include "TestData.h"
#include "city/CityData.h"
#include "city/SdlCollect.h"

#include <gtest/gtest.h>

using namespace mm2;
using namespace mm2::city;

namespace {

PsdlAttribute textureAttr(int value) {
    PsdlAttribute a;
    a.type = PsdlAttrType::Texture;
    a.subtype = static_cast<std::uint8_t>(value >> 8);
    a.args = {static_cast<std::uint16_t>(value & 0xFF)};
    return a;
}

} // namespace

// cityLevel::Load sets lvlRoomInfo flag 4 only when a room's first attribute
// is a Texture attribute whose texture has material table value 2.
TEST(ParityRoomFlags, WaterRoomsStartWithADeepWaterTexture) {
    Psdl p;
    p.textures = {"s_thames", "r2_f"};
    p.rooms.resize(5);
    p.rooms[1].attributes = {textureAttr(1)}; // s_thames
    p.rooms[2].attributes = {textureAttr(2)}; // r2_f
    PsdlAttribute strip;
    strip.type = PsdlAttrType::RoadStrip;
    p.rooms[3].attributes = {strip, textureAttr(1)}; // texture not first
    const std::vector<std::uint8_t> materials{0, 2, 8};
    EXPECT_EQ(waterRooms(p, materials), (std::vector<std::uint8_t>{0, 1, 0, 0, 0}));
}

TEST(ParityRoomFlags, RetailCitiesHaveWaterRooms) {
    MM2_REQUIRE_GAME_DATA();
    for (const char* name : {"london", "sf"}) {
        std::string err;
        const auto city = loadCity(*test::gameData(), name, &err);
        ASSERT_TRUE(city) << err;
        const auto materials = sdlTextureMaterials(city->psdl, city->textureMaterials, [&](std::string_view n) {
            return sdlMaterialIndex(city->materials, n);
        });
        const auto water = waterRooms(city->psdl, materials);
        int count = 0;
        for (std::size_t room = 0; room < water.size(); ++room) {
            if (!water[room])
                continue;
            ++count;
            const int texture = city->psdl.rooms[room].attributes.front().textureBase();
            const std::string& t = city->psdl.textures[static_cast<std::size_t>(texture)];
            EXPECT_TRUE(t.starts_with("s_thames") || t.starts_with("s_ocean")) << name << " room " << room << " " << t;
        }
        EXPECT_GT(count, 0) << name;
    }
}
