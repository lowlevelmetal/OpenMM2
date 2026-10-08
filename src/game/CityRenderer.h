#pragma once

#include "city/CityData.h"
#include "city/CityMesh.h"
#include "game/Camera.h"
#include "game/MeshDraw.h"
#include "game/ModelLibrary.h"
#include "game/RaceConfig.h"
#include "city/RoomLocator.h"
#include "game/TextureLibrary.h"
#include "render/Device.h"

#include <array>
#include <functional>
#include <string_view>
#include <vector>

namespace mm2::game {

// Graphics options that shape the environment.
struct EnvironmentOptions {
    // gxLightQuality 0-3: how many of the key, fill1 and fill2 lights are on
    // and which ambient level is used (cityLevel::SetupLighting).
    int lightQuality = 3;
    // The Far Clip option in metres (MM2: 100-1000): the camera's far plane,
    // clamping the fog.
    float farClip = 1000.0f;
};

// Lighting, fog and sky for one time of day and weather.
struct Environment {
    render::FrameConstants frame; // lights, ambient and fog (view/proj filled per frame)
    int skyPaintjob = 0;
    float farClip = 1000.0f;
    float fogEnd = 1000.0f;
    Vec4 clearColor{0.5f, 0.6f, 0.7f, 1.0f};
    // The 64 wall shades of sdlCommon::UpdateLighting (0xAARRGGBB): ambient
    // plus the lights falling on a vertical wall facing angle
    // i * pi / 32 - pi / 2.
    std::array<std::uint32_t, 64> wallShades{};
};

// Builds the environment from the city's .ltNN and _fog.csv tables.
// Snow (an OpenMM2 option; MM2's weather is 0-3) uses the rain tables.
Environment makeEnvironment(const city::CityData& city, TimeOfDay time, Weather weather,
                            const EnvironmentOptions& options = {});

// Level of detail for city objects.
struct DetailSettings {
    ObjectDetail objects;  // the Object Detail option (cityLevel::SetObjectDetail)
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

    // Shades the street geometry for an environment (the walls' light table).
    void setEnvironment(const Environment& env);
    // lvlSky::Update: turns the sky.
    void update(float dt);

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
        std::string textureName; // PSDL texture (sdlTextureName), empty = untextured
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
        float radius = 0.0f;
    };

    void drawSky(const Camera& camera, const Environment& env);
    void drawRoom(std::size_t room, const Frustum& frustum, const Mat34& camera, const DetailSettings& detail);
    void drawInstance(InstanceDraw& inst, const Frustum& frustum, const Mat34& camera, const DetailSettings& detail);
    void drawModel(const GpuModel& model, const Mat34& transform, asset::Lod lod, int depth);
    void resolve(InstanceDraw& inst);

    render::Device& m_device;
    TextureLibrary& m_textures;
    ModelLibrary& m_models;
    const city::CityData& m_city;
    city::RoomLocator m_locator;
    render::BufferHandle m_vertices, m_indices;
    // CPU copy of the street vertices and their kinds, reshaded per environment.
    std::vector<render::Vertex3D> m_streetVertices;
    std::vector<city::SurfaceKind> m_streetKinds;
    std::vector<Room> m_rooms;
    std::vector<InstanceDraw> m_instances;
    std::vector<std::uint8_t> m_roomMarks;
    const GpuModel* m_sky = nullptr;
    float m_skyAngle = 0.0f;
    int m_lastRoom = 0; // cityLevel's sm_LastPvsRoom
    Stats m_stats;
};

// lvlInstance::GetGeomSet: a missing level of detail takes the next less
// detailed one (VL -> L -> M -> H); a missing VL draws nothing.
const GpuMesh* findFilledLod(const GpuModel& model, std::string_view part, asset::Lod lod);

// lvlInstance::GetGeomSet's radius of a part: the largest distance of a
// vertex from the model origin over its levels of detail (modGetStatic).
float geomRadius(const GpuModel& model, std::string_view part);

// lvlSDL::LoadBinary: the name a PSDL texture is looked up by (a movie
// frame name "<base>-0NNN" becomes "<base>").
std::string sdlTextureName(std::string_view name);

} // namespace mm2::game
