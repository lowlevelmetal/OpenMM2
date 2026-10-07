#pragma once

#include "city/CityData.h"
#include "city/CityMesh.h"
#include "game/Camera.h"
#include "game/ModelLibrary.h"
#include "game/RaceConfig.h"
#include "city/RoomLocator.h"
#include "game/TextureLibrary.h"
#include "render/Device.h"

#include <functional>
#include <string_view>
#include <vector>

namespace mm2::game {

// Lighting, fog and sky for one time of day and weather.
struct Environment {
    render::FrameConstants frame; // lights, ambient and fog (view/proj filled per frame)
    int skyPaintjob = 0;
    float fogEnd = 1000.0f;
    Vec4 clearColor{0.5f, 0.6f, 0.7f, 1.0f};
};

// Builds the environment from the city's .ltNN and _fog.csv tables.
// Snow (multiplayer only) uses the rain tables, as the city data has no
// snow entries.
Environment makeEnvironment(const city::CityData& city, TimeOfDay time, Weather weather);

// Level of detail for city objects.
struct DetailSettings {
    float lodScale = 1.0f; // multiplies the LOD switch distances ("Object Detail")
    bool usePvs = true;    // cull rooms with the city's precomputed visibility
};

// Draws the static city: PSDL street geometry, instances and the sky.
class CityRenderer {
public:
    // `isDynamic` marks instances drawn elsewhere (bangers), skipped here.
    CityRenderer(render::Device& device, TextureLibrary& textures, ModelLibrary& models, const city::CityData& city,
                 const std::function<bool(std::string_view)>& isDynamic = {});
    ~CityRenderer();
    CityRenderer(const CityRenderer&) = delete;
    CityRenderer& operator=(const CityRenderer&) = delete;

    // Room containing `position` (0 = none).
    int roomAt(const Vec3& position) const { return m_locator.find(position); }

    // Call inside a scene pass after setFrameConstants().
    void draw(const Camera& camera, const Frustum& frustum, const Environment& env, const DetailSettings& detail);

    struct Stats {
        int cameraRoom = 0;
        int roomsDrawn = 0;
        int instancesDrawn = 0;
        int drawCalls = 0;
    };
    const Stats& stats() const { return m_stats; }

    // Draws one model mesh with its materials (shared with vehicles).
    void drawMesh(const GpuMesh& mesh, const std::vector<asset::PkgMaterial>& materials, const Mat44& world,
                  bool fog = true, bool lighting = true);

private:
    struct Batch {
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
        const std::string* textureName = nullptr; // PSDL texture slot, null = untextured
        city::SurfaceKind kind{};
    };
    struct Room {
        std::vector<Batch> batches;
        Aabb bounds;
        std::vector<std::size_t> instances; // indices into m_instances
    };
    struct InstanceDraw {
        std::string model;
        const GpuModel* gpu = nullptr;
        bool resolved = false;
        Mat44 world;
        Mat34 transform;
        Aabb worldBounds; // valid once resolved
    };

    void drawSky(const Camera& camera, const Environment& env);
    void drawRoom(std::size_t room, const Frustum& frustum, const Vec3& eye, const DetailSettings& detail);
    void drawInstance(InstanceDraw& inst, const Frustum& frustum, const Vec3& eye, const DetailSettings& detail);
    void drawModel(const GpuModel& model, const Mat34& transform, asset::Lod lod, int depth);
    void resolve(InstanceDraw& inst);

    render::Device& m_device;
    TextureLibrary& m_textures;
    ModelLibrary& m_models;
    const city::CityData& m_city;
    city::RoomLocator m_locator;
    render::BufferHandle m_vertices, m_indices;
    std::vector<Room> m_rooms;
    std::vector<InstanceDraw> m_instances;
    std::vector<std::uint8_t> m_roomMarks;
    const GpuModel* m_sky = nullptr;
    Stats m_stats;
};

} // namespace mm2::game
