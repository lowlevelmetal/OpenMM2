// A session in the city.
#include "app/Controls.h"
#include "app/Screens.h"
#include "city/CityData.h"
#include "city/RoomInfo.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "asset/Pkg.h"
#include "asset/VehicleModel.h"
#include "audio/MusicDirector.h"
#include "audio/SoundBank.h"
#include "audio/game/Ambience.h"
#include "audio/game/CarAudio.h"
#include "audio/game/Object3D.h"
#include "audio/AngelRandom.h"
#include "audio/game/PedAudio.h"
#include "audio/game/Voices.h"
#include "game/CamMirror.h"
#include "game/Profile.h"
#include "data/DatFile.h"
#include "data/TextTables.h"
#include "ai/Opponent.h"
#include "ai/Police.h"
#include "ai/World.h"
#include "game/AiRenderer.h"
#include "game/session/CopsAndRobbers.h"
#include "game/session/Hud.h"
#include "game/session/Session.h"
#include "game/TrafficBodies.h"
#include "game/bangers/BangerSet.h"
#include "game/bangers/PropPlacement.h"
#include "game/bangers/RoadDecals.h"
#include "game/fx/EffectLibrary.h"
#include "game/fx/ParticleRenderer.h"
#include "game/fx/SkidMarks.h"
#include "game/fx/VehicleEffects.h"
#include "game/fx/Weather.h"
#include "game/net/NetGame.h"
#include "game/CamMirror.h"
#include "game/CamPlayer.h"
#include "game/CityRenderer.h"
#include "game/CityLevel.h"
#include "game/PlayerVehicle.h"
#include "game/VehicleRenderer.h"
#include "phys/World.h"
#include "render/Projection.h"
#include "ui/Text.h"
#include "ui/TextureCache.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <format>

namespace mm2::app {
namespace {

// Cops and Robbers' own messages (mmMultiCR's 0x25e pickup request and 0x261
// ChangeSet) as OpenMM2 game events.
constexpr auto kCrPickupRequest = static_cast<std::uint16_t>(static_cast<int>(net::GameEventType::Custom) + 1);
constexpr auto kCrNewSet = static_cast<std::uint16_t>(static_cast<int>(net::GameEventType::Custom) + 2);
struct CrSetEvent {
    Vec3 gold, bank, hideout;
};
template <class S>
bool serialize(S& s, CrSetEvent& e) {
    s.vec3(e.gold);
    s.vec3(e.bank);
    s.vec3(e.hideout);
    return s.ok();
}

const char* modePrefix(game::GameMode m) {
    switch (m) {
    case game::GameMode::Blitz: return "blitz";
    case game::GameMode::Circuit: return "circuit";
    case game::GameMode::Checkpoint: return "race";
    case game::GameMode::CrashCourse: return "crash";
    default: return "";
    }
}

class RaceScreen final : public Screen {
public:
    RaceScreen(Context& ctx, const game::RaceConfig& config)
        : m_ui(ctx.device(), ctx.game->vfs), m_text(ctx.device()) {
        m_result.config = config;
        // Loading screen art: <city>_<mode><n>.jpg, else the generic one.
        const std::string prefix = modePrefix(config.mode);
        m_loadingImage = "jpg/loading.jpg";
        if (!prefix.empty() && config.raceIndex >= 0) {
            const std::string specific = std::format("jpg/{}_{}{}.jpg", config.city, prefix, config.raceIndex);
            if (ctx.game->vfs.exists(specific))
                m_loadingImage = specific;
        } else if (config.mode == game::GameMode::Cruise && ctx.game->vfs.exists("jpg/" + config.city + "_roam.jpg")) {
            m_loadingImage = "jpg/" + config.city + "_roam.jpg";
        }
    }

    ~RaceScreen() override {
        if (m_cityLevel && m_trafficBodies)
            m_cityLevel->removeSource(m_trafficBodies.get());
        if (m_cityLevel && m_bangers)
            m_cityLevel->removeSource(m_bangers.get());
        m_carAudio.stop();
        for (auto& o : m_opponents)
            if (o.audio)
                o.audio->stop();
        for (auto& c : m_cops)
            if (c.audio)
                c.audio->stop();
        m_rain.stop();
        m_announcer.stop();
        m_pedAudio.stop();
        if (m_ctxMixer)
            m_ctxMixer->stopAll();
        // The police drivers hand their cars' impact callbacks back when they
        // go: before the cars (m_cops) are destroyed.
        m_police.reset();
    }

    bool usesScene() const override { return m_city != nullptr; }

    void update(Context& ctx, double dt) override {
        m_time += dt;
        m_frameDt = static_cast<float>(dt);
        if (m_state == State::ShowLoading) {
            m_state = State::Load; // draw the loading screen once before blocking
            return;
        }
        if (m_state == State::Load) {
            load(ctx);
            return;
        }
        // mmPopup: Escape opens the main menu (pausing a single-player game,
        // mmPopup::ProcessEscape(1)); while it is up the game's keys are off.
        m_popupGraveyard.clear();
        if (m_popup != Popup::None) {
            updatePopup(ctx, dt);
            if (ctx.nextScreen)
                return;
        } else if (ctx.input.keyPressed(platform::Key::Escape)) {
            openPopup(ctx, true);
        } else if (!m_flyCamera && m_bindings.pressed(ctx.input, controls::Action::EnterChat)) {
            openChat(ctx);
        }
        if (m_textInput && m_popup != Popup::Chat) {
            ctx.input.stopTextInput(ctx.window());
            m_textInput = false;
        }
        postIncomingChat(ctx);
        if (m_hud)
            m_hud->updateChat(static_cast<float>(dt));
        if (multiplayer(ctx)) {
            ctx.netGame->update();
            if (ctx.netGame->takeReturnToLobby() || !ctx.netGame->inSession()) {
                leaveRace(ctx, m_result);
                return;
            }
        }
        if (ctx.input.keyPressed(platform::Key::F2))
            m_flyCamera = !m_flyCamera;
        if (!m_flyCamera && m_popup == Popup::None)
            updateGameInput(ctx);
        if (m_paused) {
            // asRoot paused (the full-screen map in single player): the
            // game, the physics and the clocks stand still.
            if (m_flyCamera || !m_player)
                updateFlyCamera(ctx, static_cast<float>(dt));
            m_textures->update(m_time);
            return;
        }
        updatePlayer(ctx, static_cast<float>(dt));
        // aiMap::Update: the ambient traffic and the pedestrians first, then
        // the racers and the police, before the physics step.
        updateAmbient(ctx, static_cast<float>(dt));
        updateAiDrivers(static_cast<float>(dt));
        if (m_ai)
            m_ai->updateLights(); // the light sets last (aiMap::Update)
        updateRemoteCars(ctx);
        // aiVehicleManager::Update and the rail cars' rooms, before the
        // collision manager runs.
        if (m_trafficBodies)
            m_trafficBodies->beforeStep();
        // vehCar::Update: cars in a water room float once below its level.
        if (m_player)
            m_player->sim().setWaterLevel(waterLevelAt(m_player->sim().modelMatrix().m3));
        for (auto& o : m_opponents)
            if (o.sim)
                o.sim->sim().setWaterLevel(waterLevelAt(o.sim->sim().modelMatrix().m3));
        for (auto& c : m_cops)
            if (c.sim)
                c.sim->sim().setWaterLevel(waterLevelAt(c.sim->sim().modelMatrix().m3));
        // dgTrailerJoint::Update's debug key: Ctrl+B breaks every trailer
        // hitch (held until a physics sample has seen it).
        {
            using platform::Key;
            const auto& in = ctx.input;
            if (!m_flyCamera && (in.keyDown(Key::LCtrl) || in.keyDown(Key::RCtrl)) && in.keyPressed(Key::B))
                phys::Trailer::breakKeyPressed = true;
        }
        if (m_world && m_world->advanceFixed(static_cast<float>(dt)) > 0)
            phys::Trailer::breakKeyPressed = false;
        // The props and traffic cars the collisions set moving follow their
        // bodies; the ones that came to rest stop being simulated.
        if (m_bangers)
            m_bangers->update(static_cast<float>(dt));
        if (m_trafficBodies)
            m_trafficBodies->afterStep();
        if (m_player) {
            m_pose = m_player->pose();
            m_trailerPose = m_player->trailerPose();
            if (multiplayer(ctx))
                sendLocalState(ctx);
        }
        if (m_flyCamera || !m_player)
            updateFlyCamera(ctx, static_cast<float>(dt));
        else
            updateCarCamera(ctx, static_cast<float>(dt));
        updateAudio(ctx, static_cast<float>(dt));
        updateEffects(static_cast<float>(dt));
        updateSession(ctx, static_cast<float>(dt));
        updateCopsAndRobbers(ctx, static_cast<float>(dt));
        // Development aid: OPENMM2_DEBUG_FOCUS=ped|car frames the nearest
        // pedestrian or traffic car (for screenshots).
        if (const char* focus = std::getenv("OPENMM2_DEBUG_FOCUS"); focus && m_ai) {
            const Vec3 ref = m_player ? m_pose.body.m3 : m_camera.position();
            std::optional<Mat34> target;
            float best = 1e30f;
            auto consider = [&](const Mat34& m) {
                const float d = m.m3.dist2(ref);
                if (d < best) {
                    best = d;
                    target = m;
                }
            };
            if (std::string_view(focus) == "ped")
                for (const auto& p : m_ai->peds())
                    consider(p.transform);
            else
                for (const auto& c : m_ai->cars())
                    consider(c.transform);
            if (target) {
                const Vec3 eye = target->m3 + target->transformDir({2.5f, 1.6f, -3.5f});
                m_camera.transform = game::Camera::lookAt(eye, target->m3 + Vec3{0, 0.9f, 0});
            }
        }
        m_textures->update(m_time);
        if (m_cityRenderer)
            m_cityRenderer->update(m_frameDt);
        if (ctx.input.keyPressed(platform::Key::F3))
            m_showDebug = !m_showDebug;
        if (m_showDebug)
            drawDebugUi();
    }

    void drawScene(Context& ctx) override {
        render::Device& dev = ctx.device();
        render::ClearValues clear;
        clear.color = m_env.clearColor;
        const auto extent = dev.sceneExtent();
        render::Rect band{0, 0, extent.width, extent.height};
        // The 3D view (gfxPipeline::VP) on black: letterboxed to 66% of the
        // height, 18% down, for the wide angle (mmPlayer::SetWideFOV), the
        // top half with the split map, the small map's place with the
        // full-screen map (mmHudMap::SetMapMode; Hud::sceneRect).
        syncHudView();
        if (m_hud && !m_flyCamera)
            band = m_hud->sceneRect(extent);
        const bool letterbox = band != render::Rect{0, 0, extent.width, extent.height};
        if (letterbox)
            clear.color = {0.0f, 0.0f, 0.0f, 1.0f};
        dev.beginScene(clear);
        std::vector<game::session::MapBlip> blips = hudBlips();
        // mmGameManager::Cull draws the full-screen map before the level.
        const bool hudShown = m_hud && m_session && m_player && !m_flyCamera;
        const bool mapShown = hudShown && (m_popup == Popup::None || m_popup == Popup::Chat);
        const bool fullMap = mapShown && m_cams.mapMode() == game::MapMode::FullScreen;
        if (fullMap)
            m_hud->drawMap(*m_session, m_playerState, blips, m_frameDt);
        if (letterbox) {
            dev.setViewport({static_cast<float>(band.x), static_cast<float>(band.y),
                             static_cast<float>(band.width), static_cast<float>(band.height)});
            dev.setScissor(&band);
            render::ClearValues sky;
            sky.color = m_env.clearColor;
            dev.clear(sky);
        }
        const float aspect = band.height ? static_cast<float>(band.width) / static_cast<float>(band.height) : 1.0f;
        const auto proj = render::computeProjection(m_camera.horizontalFov, aspect, ctx.display.fovMode,
                                                    ctx.display.maxAspect);
        m_camera.farPlane = m_env.farClip;
        render::FrameConstants frame = m_env.frame;
        frame.view = m_camera.view();
        frame.proj = Mat44::perspective(proj.fovY, proj.aspect, m_camera.nearPlane, m_camera.farPlane, true);
        frame.cameraPosition = m_camera.position();
        dev.setFrameConstants(frame);
        const game::Frustum frustum(frame.view * frame.proj);
        const bool playerBody = m_flyCamera || m_cams.display() == game::CarDisplay::Body;
        // The sirens' lens flares (vehSiren::Draw, ltLensFlare) are queued
        // while the level draws and added over it afterwards.
        std::vector<game::fx::LensFlareQuad> flares;
        const Mat44 viewProj = frame.view * frame.proj;
        game::VehicleRenderer::setLensFlareTarget(&viewProj, proj.aspect, &flares);
        drawLevel(ctx, m_camera, frustum, playerBody, m_frameDt);
        game::VehicleRenderer::setLensFlareTarget(nullptr, 1.0f, nullptr);
        if (!flares.empty()) {
            game::fx::drawLensFlares(dev, *m_textures, flares);
            dev.setFrameConstants(frame);
        }
        if (m_hud && m_session && m_player) {
            // The arrow, icons, stands and dash are drawn in the 3D view.
            m_hud->setViewProjection(frame.view * frame.proj);
            m_hud->drawWorld(*m_session, m_camera, m_playerState, m_lastPedals.steering, blips);
        }
        if (letterbox) {
            dev.setScissor(nullptr);
            dev.setViewport(
                {0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height)});
        }
        // mmPopup::ProcessEscape deactivates the map (the chat line does not).
        if (mapShown && !fullMap)
            m_hud->drawMap(*m_session, m_playerState, blips, m_frameDt);
        // mmGameManager::Update declares the mirror after the dashboard and
        // the HUD map.
        drawMirror(ctx, frame);
        dev.endScene();
    }

    // The HUD's view options from the cameras' view settings (mmViewMgr):
    // the map's mode, the wide angle and the dashboard.
    void syncHudView() {
        if (!m_hud)
            return;
        auto& o = m_hud->options();
        o.mapMode = m_cams.mapMode();
        o.wideAngle = !m_flyCamera && m_cams.wideAngle();
        o.dashActive = !m_flyCamera && m_cams.dashboard();
        o.dashboard = !m_flyCamera && m_cams.display() == game::CarDisplay::Dash;
        // mmGame::UpdateGameInput: looking around from a point-of-view
        // camera disables the HUD (mmHUD::Disable), straight ahead enables
        // it again. mmPopup::ProcessEscape disables it too.
        o.visible = (m_flyCamera || m_cams.display() == game::CarDisplay::Body || m_camPan == 0.0f) &&
                    (m_popup == Popup::None || m_popup == Popup::Chat);
    }

    // The other cars for the HUD's map and icons.
    std::vector<game::session::MapBlip> hudBlips() const {
        std::vector<game::session::MapBlip> blips;
        // mmHudMap and mmIcons follow the cars' phInertialCS matrices.
        for (const auto& o : m_opponents)
            blips.push_back({o.sim->sim().body.ics.matrix, game::session::MapBlip::Kind::Opponent});
        // mmHudMap::DrawCops: the police in pursuit (aiPoliceOfficer::InPersuit).
        for (const auto& c : m_cops)
            if (c.driver->mode() == ai::PoliceCar::Mode::Chasing)
                blips.push_back({c.sim->sim().body.ics.matrix, game::session::MapBlip::Kind::Police});
        return blips;
    }

    // The level as lvlLevel::Draw draws it for one view: the city, traffic,
    // props, the cars and their effects (lvlLevel's callbacks) and the rain.
    // `playerBody` false hides the player's car; `dt` advances the remote
    // cars' wheels (0 for a second view of the same frame).
    void drawLevel(Context& ctx, const game::Camera& camera, const game::Frustum& frustum, bool playerBody,
                   float dt) {
        render::Device& dev = ctx.device();
        m_cityRenderer->draw(camera, frustum, m_env, m_detail);
        m_roadDecals.draw(dev, *m_textures);
        if (m_ai && m_aiRenderer)
            m_aiRenderer->draw(*m_ai, camera, frustum, m_result.config.timeOfDay, carLights(), m_detail.objects,
                               [this](int id) { return m_trafficBodies ? m_trafficBodies->transformOf(id) : nullptr; });
        drawRemoteCars(ctx, dt, camera);
        const bool night = m_result.config.timeOfDay == game::TimeOfDay::Night;
        if (m_bangers)
            m_bangers->draw(dev, *m_models, *m_textures, m_cards, frustum, camera, {m_detail.objects, night});
        const bool lights = carLights();
        if (m_vehicle && playerBody) {
            m_pose.headlights = lights;
            m_vehicle->draw(m_pose, camera.transform);
        }
        if (m_trailer && (m_flyCamera || m_cams.display() == game::CarDisplay::Body || !playerBody)) {
            m_trailerPose.headlights = lights;
            m_trailer->draw(m_trailerPose, camera.transform);
        }
        for (const auto& o : m_opponents) {
            game::VehiclePose pose = o.sim->pose();
            pose.headlights = lights;
            o.renderer->draw(pose, camera.transform);
        }
        for (const auto& c : m_cops) {
            game::VehiclePose pose = c.sim->pose();
            pose.headlights = lights;
            pose.siren = c.driver->siren();
            pose.sirenAngle = c.sirenAngle;
            c.renderer->draw(pose, camera.transform);
        }
        if (m_vehicleFx)
            m_vehicleFx->draw(dev, *m_textures, m_cards, m_skids, camera.transform);
        for (const auto& o : m_opponents)
            if (o.fx)
                o.fx->draw(dev, *m_textures, m_cards, m_skids, camera.transform);
        for (const auto& c : m_cops)
            if (c.fx)
                c.fx->draw(dev, *m_textures, m_cards, m_skids, camera.transform);
        if (m_weather && rainVisible(camera.position()))
            m_weather->draw(dev, *m_textures, m_cards, camera.transform);
    }

    // mmMirror::Cull: the rear-view mirror's inset at the top right, cleared
    // to black, the level drawn from the mirror's frame on the player's car
    // with its fixed-aspect projection, the winding swapped (the frame is
    // mirrored) and the player's car hidden.
    void drawMirror(Context& ctx, const render::FrameConstants& mainFrame) {
        if (!m_mirror.enabled() || !m_player || !m_vehicle || m_flyCamera)
            return;
        render::Device& dev = ctx.device();
        const auto extent = dev.sceneExtent();
        const auto inset = m_mirror.viewport(static_cast<int>(extent.width), static_cast<int>(extent.height));
        if (inset.width <= 0 || inset.height <= 0)
            return;
        const auto& params = m_mirror.params();
        game::Camera camera;
        camera.transform = m_mirror.worldMatrix(m_pose.body);
        camera.nearPlane = params.nearClip;
        camera.farPlane = params.farClip;
        render::FrameConstants frame = mainFrame;
        frame.view = camera.view();
        frame.proj = Mat44::perspective(params.fov * 0.017453292f, params.aspect, camera.nearPlane,
                                        camera.farPlane, true);
        frame.cameraPosition = camera.position();
        const render::Rect rect{inset.x, inset.y, static_cast<std::uint32_t>(inset.width),
                                static_cast<std::uint32_t>(inset.height)};
        dev.setViewport({static_cast<float>(inset.x), static_cast<float>(inset.y), static_cast<float>(inset.width),
                         static_cast<float>(inset.height)});
        dev.setScissor(&rect);
        render::ClearValues clear;
        clear.color = {0.0f, 0.0f, 0.0f, 1.0f};
        dev.clear(clear);
        dev.setFrameConstants(frame);
        dev.setFrontFaceFlipped(true);
        drawLevel(ctx, camera, game::Frustum(frame.view * frame.proj), false, 0.0f);
        dev.setFrontFaceFlipped(false);
        dev.setScissor(nullptr);
        dev.setViewport({0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height)});
        dev.setFrameConstants(mainFrame);
    }

    void drawOverlay(Context& ctx) override {
        if (m_state == State::Running && m_hud && m_session && m_player && !m_showDebugOnly) {
            m_hud->drawOverlay(*ctx.overlay, m_text, m_ui, *m_session, m_playerState);
            if (m_popup != Popup::None)
                drawPopup(ctx);
            return;
        }
        if (m_state != State::Running) {
            auto& ov = *ctx.overlay;
            ov.begin(ctx.display.uiScale);
            ui::drawImage(ov, m_ui.get(m_loadingImage), 0, 0, 640, 480);
            ov.end();
        }
    }

private:
    enum class State { ShowLoading, Load, Running };

    void load(Context& ctx) {
        const auto t0 = std::chrono::steady_clock::now();
        std::string error;
        auto city = city::loadCity(ctx.game->vfs, m_result.config.city, &error);
        if (!city) {
            log::error("race: cannot load city '{}': {}", m_result.config.city, error);
            ctx.nextScreen = makeFrontendScreen(ctx, m_result);
            return;
        }
        for (const auto& w : city->warnings)
            log::debug("city: {}", w);
        m_city = std::make_unique<city::CityData>(std::move(*city));
        m_vfs = &ctx.game->vfs;
        m_textures = std::make_unique<game::TextureLibrary>(ctx.device(), ctx.game->vfs);
        m_models = std::make_unique<game::ModelLibrary>(ctx.device(), ctx.game->vfs);
        m_bangerData = std::make_unique<game::bangers::BangerDataLibrary>(ctx.game->vfs);
        // cityLevel::Load: gfxTexReduceSize = 32 << the Texture Quality
        // option (gfxTextureQuality) while the city loads, no limit after.
        const int textureQuality =
            static_cast<int>(std::clamp(ctx.settings.ini.getInt("Graphics", "TextureQuality", 2), 0LL, 3LL));
        m_textures->setSizeLimit(32 << textureQuality);
        m_cityRenderer = std::make_unique<game::CityRenderer>(ctx.device(), *m_textures, *m_models, *m_city,
                                                              [this](std::string_view n) { return m_bangerData->has(n); });
        m_textures->setSizeLimit(0);
        m_objectDetail = std::clamp(static_cast<int>(ctx.settings.ini.getInt("Graphics", "ObjectDetail", 3)), 0, 3);
        m_detail.objects = game::ObjectDetail::forLevel(m_objectDetail);
        // Development aid for screenshots: "<timeOfDay 0-3>,<weather 0-3>".
        if (const char* env = std::getenv("OPENMM2_DEBUG_ENV"); env && std::strlen(env) >= 3) {
            m_result.config.timeOfDay = static_cast<game::TimeOfDay>(std::clamp(env[0] - '0', 0, 3));
            m_result.config.weather = static_cast<game::Weather>(std::clamp(env[2] - '0', 0, 4));
        }
        {
            // mmGame::SetLevelGraphics: cityLevel::sm_LightQuality is the
            // LIGHTING QUALITY option (0-3); the far clip is the FAR CLIP
            // option in metres (PUGraphics::SetFarClip, mmGame::FarClipCB).
            // The keys and defaults are the Graphics options page's.
            m_envOptions.lightQuality =
                static_cast<int>(std::clamp(ctx.settings.ini.getInt("Graphics", "LightingQuality", 3), 0LL, 3LL));
            m_envOptions.farClip = static_cast<float>(
                std::clamp(ctx.settings.ini.getDouble("Graphics", "FarClip", 1000.0), 100.0, 1000.0));
            // mmGame::SetLevelGraphics: vglCloudMapEnable by CLOUD SHADOWS.
            const auto clouds = ctx.settings.ini.getInt("Graphics", "CloudShadows", 2);
            m_envOptions.cloudShadows = static_cast<int>(std::clamp(clouds, 0LL, 2LL));
        }
        applyEnvironment();
        m_position = m_city->psdl.sphereCenter + Vec3{0, 3, 0};
        m_yaw = 0.0f;
        m_pitch = -0.15f;
        m_bindings.load(ctx.settings.ini);
        m_controlOptions = controls::Options::load(ctx.settings.ini);
        createSession(ctx);
        loadVehicle(ctx); // places the camera behind the car
        loadAi(ctx);
        loadEffects(ctx);
        loadPedestrianProps(ctx);
        spawnOpponents(ctx);
        spawnPolice(ctx);
        // Every vehCar::Init builds a vehSiren, whose constructor sets the
        // light glow scales to 0.2 / 0.6; aiMap::Init ends with
        // aiVehicleManager::Init, which sets 0.2 / 0.95 (network cars set up
        // later bring the 0.6 back, see drawRemoteCars).
        game::VehicleRenderer::setLightGlowScales(0.2f, 0.95f);
        m_hud = std::make_unique<game::session::Hud>(ctx.device(), *m_textures, *m_models, ctx.game->vfs,
                                                     ctx.game->strings, m_result.config.city, m_result.config.vehicle);
        m_hud->options().metric = ctx.settings.metricUnits;
        m_hud->options().uiScale = ctx.display.uiScale;
        loadViewSettings(ctx);
        // mmSingleBlitz::InitHUD turns the icons on (iconState = 1).
        if (m_result.config.mode == game::GameMode::Blitz)
            m_hud->options().opponentIcons = true;
        m_hud->preload(&m_ui);
        if (m_session) {
            phys::setElasticityCap(phys::kElasticityCap); // mmGame::Reset
            m_session->start();
            setupCopsAndRobbers(ctx);
            // mmPlayer::SetPreRaceCam (every single-player mode but cruise).
            if (m_result.config.mode != game::GameMode::Cruise && !multiplayer(ctx))
                m_cams.startPreRace();
        }
        if (auto* music = ctx.music()) {
            // The song is chosen now; MusicDirector starts it 1.25 s in.
            const bool cruise = m_result.config.mode == game::GameMode::Cruise;
            music->startRace(-1, cruise, false);
            music->setAmbience(m_result.config.city);
            m_musicDirector = std::make_unique<audio::MusicDirector>(cruise);
        }
        // Development aid for reproducible screenshots: "x,y,z,yaw,pitch".
        if (const char* cam = std::getenv("OPENMM2_DEBUG_CAMERA")) {
            const auto parts = str::split(cam, ',');
            if (parts.size() == 5) {
                auto f = [&](int i) { return static_cast<float>(str::parseDouble(parts[i]).value_or(0.0)); };
                m_position = {f(0), f(1), f(2)};
                m_yaw = f(3);
                m_pitch = f(4);
            }
        }
        m_state = State::Running;
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        log::info("race: loaded {} in {:.2f} s", m_result.config.city, seconds);
    }

    // Spawn point and heading. Until race starts are wired up, cruise uses
    // the first waypoint of the city's first Blitz race.
    std::pair<Vec3, float> spawnPoint(Context& ctx) const {
        for (const auto& race : m_city->races) {
            if (race.waypoints.empty())
                continue;
            auto bytes = ctx.game->vfs.readAll(race.waypoints);
            if (!bytes)
                continue;
            const auto csv = data::CsvTable::parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
            if (csv.rows().size() < 2)
                continue;
            const Vec3 a{csv.cellFloat(0, 0), csv.cellFloat(0, 1), csv.cellFloat(0, 2)};
            const Vec3 b{csv.cellFloat(1, 0), csv.cellFloat(1, 1), csv.cellFloat(1, 2)};
            return {a, std::atan2(-(b.x - a.x), -(b.z - a.z))};
        }
        return {m_city->psdl.sphereCenter, 0.0f};
    }

    void loadVehicle(Context& ctx) {
        // Physics world: the city as the collision manager sees it (rooms,
        // collision polygons, collidable objects; materials), and the probe
        // geometry the wheels use.
        m_cityLevel = std::make_unique<game::CityLevel>(*m_city, ctx.game->vfs,
                                                        [this](std::string_view n) { return m_bangerData->has(n); });
        m_world = std::make_unique<phys::World>(m_cityLevel->takeMaterials());
        m_world->setStatic(m_cityLevel->takeProbeSoup());
        m_world->setLevel(m_cityLevel.get());

        std::string error;
        // mmPlayer::Init: no trailer in multiplayer cruise or Cops and Robbers.
        const auto mode = m_result.config.mode;
        const bool withTrailer =
            !(multiplayer(ctx) && (mode == game::GameMode::Cruise || mode == game::GameMode::CopsAndRobbers));
        m_player = game::SimVehicle::loadPlayer(ctx.game->vfs, m_result.config.vehicle, &error, withTrailer);
        if (!m_player) {
            log::error("race: vehicle '{}': {}", m_result.config.vehicle, error);
            return;
        }
        m_player->sim().options.player = true; // mmPlayer::Update's input overrides
        // mmGame::Init: vehTransmission::Automatic with the player's
        // transmission choice; the AUTO REVERSE option (mmInput +0x18C).
        m_player->sim().trans.automatic(m_result.config.automatic);
        m_player->controls().autoReverse = m_controlOptions.autoReverse;
        // The player's car collides with its polygonal bound (vehCar::Init
        // with vehBound) and marks what it hits (dgPhysManager's PlayerInst):
        // mmGame::Update declares it each frame as the type-4 mover, whose
        // room and neighbours keep knocked-over props simulated.
        m_player->sim().setPolygonalBound(true);
        m_player->sim().body.declare(4, 0x1b);
        if (auto* trailer = m_player->trailer())
            trailer->body.declare(2, 0x1b); // mmGame::Update: the trailer as type 2, 0x1b
        m_player->sim().options.weatherFriction = weatherFriction();
        m_vehicle = std::make_unique<game::VehicleRenderer>(ctx.device(), *m_textures, *m_models, m_player->model(),
                                                            m_result.config.vehicleColor);
        setupVehicleRenderer(ctx, *m_vehicle);
        if (const auto* trailer = m_player->trailerModel()) {
            m_trailer = std::make_unique<game::VehicleRenderer>(ctx.device(), *m_textures, *m_models, *trailer,
                                                                m_result.config.vehicleColor, "TRAILER", "TWHL");
            setupVehicleRenderer(ctx, *m_trailer);
        }
        auto [pos, heading] = spawnPoint(ctx);
        if (m_session) {
            const Mat34 sp = m_session->playerSpawn();
            pos = sp.m3;
            heading = std::atan2(sp.m2.x, sp.m2.z);
            // mmMultiBlitz / mmMultiCircuit / mmMultiRace::InitMyPlayer: the
            // player's start slot on the grid behind the start
            // (mmGameMulti::StartXYZ; the slot is NetStartArray's, here the
            // player's id, inferred).
            const auto raceMode = m_result.config.mode;
            if (multiplayer(ctx) && (raceMode == game::GameMode::Blitz || raceMode == game::GameMode::Circuit ||
                                     raceMode == game::GameMode::Checkpoint)) {
                const bool longVehicle = m_player->sim().body.radius() > 6.0f || m_player->trailerModel();
                pos += Mat34::rotationY(heading).transformDir(
                    game::session::multiplayerGridOffset(ctx.netGame->localId(), longVehicle));
            }
        }
        // Development aid: OPENMM2_DEBUG_SPAWN="x,y,z,heading".
        if (const char* sp = std::getenv("OPENMM2_DEBUG_SPAWN")) {
            const auto parts = str::split(sp, ',');
            if (parts.size() == 4) {
                auto f = [&](int i) { return static_cast<float>(str::parseDouble(parts[i]).value_or(0.0)); };
                pos = {f(0), f(1), f(2)};
                heading = f(3);
            }
        }
        m_spawn = Mat34::rotationY(heading);
        m_spawn.m3 = pos;
        // Drop the spawn point onto the surface below it (mmGame::FindGroundPos
        // probes as the wheels do, lvlSDL::CollideProbe).
        phys::RayHit hit;
        if (m_world->wheelProbe(pos + Vec3{0, 5, 0}, pos - Vec3{0, 30, 0}, hit, nullptr, nullptr))
            m_spawn.m3 = hit.position;
        m_player->addTo(*m_world);
        m_player->reset(m_spawn);
        m_pose = m_player->pose();
        std::vector<std::string> missing;
        // mmPlayer::Init: the dashboard eye depends on the screen's shape.
        float aspect = 4.0f / 3.0f;
        if (const auto extent = ctx.device().sceneExtent(); extent.height)
            aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        m_cams.load(ctx.game->vfs, m_result.config.vehicle, &missing, aspect);
        // mmMirror::Init: the defaults, then tune/<car>.mmmirror.
        m_mirror.load(ctx.game->vfs, m_result.config.vehicle);
        if (const auto* info = ctx.game->catalog.vehicle(m_result.config.vehicle))
            m_cams.setVehicleFlags(static_cast<int>(info->flags));
        for (const auto& m : missing)
            log::debug("race: camera file {} missing (engine defaults)", m);
        // mmPlayerConfig::SetViewSettings: the driver's camera, wide angle and
        // dashboard before mmPlayer::Init and Reset read them; mmViewMgr::Init
        // leaves the mirror on only when the driver had it on (off for a new
        // driver).
        loadProfile();
        m_mirror.setEnabled(m_profile && m_profile->mirror);
        if (m_profile)
            m_cams.setViewSettings({m_profile->camera, m_profile->wideAngle, m_profile->dashboard});
        m_cams.reset(cameraTarget());
        loadAudio(ctx);
        m_position = pos + Mat34::rotationY(heading).transformDir({0, 2.2f, 7.0f});
        m_yaw = heading;
    }

    void createSession(Context& ctx) {
        std::string error;
        game::session::SessionOptions opts;
        if (const auto* info = ctx.game->catalog.vehicle(m_result.config.vehicle))
            opts.scoringBias = info->scoringBias;
        opts.playerName = ctx.settings.playerName;
        // mmMultiRoam: RespawnXYZ draws the cruise start from a random stream
        // seeded with the player's id, so every player starts elsewhere.
        if (multiplayer(ctx))
            opts.seed = 1u + ctx.netGame->localId();
        // Crash course pursuit checks look through the level (the world is
        // built after the session).
        opts.lineOfSight = [this](const Vec3& a, const Vec3& b) {
            phys::RayHit hit;
            return !m_world || !m_world->probe(a, b, hit);
        };
        m_session = game::session::Session::create(m_result.config, *m_city, ctx.game->vfs, ctx.game->strings, &error,
                                                  opts);
        if (!m_session)
            log::warn("race: no race rules: {}", error);
    }

    // Loads an AI-driven car onto the ground at `spawn`.
    std::unique_ptr<game::SimVehicle> loadAiCar(Context& ctx, const std::string& vehicle, std::string_view tune,
                                                Mat34& spawn) {
        std::string error;
        auto car = game::SimVehicle::load(ctx.game->vfs, vehicle, &error, tune);
        if (!car) {
            log::warn("race: AI car {}: {}", vehicle, error);
            return nullptr;
        }
        car->sim().options.weatherFriction = weatherFriction();
        // vehCarModel::InitBound builds a model's bound once, and the
        // player's car is set up first: AI cars of the player's model collide
        // with its polygonal bound too.
        if (vehicle == m_result.config.vehicle)
            car->sim().setPolygonalBound(true);
        car->addTo(*m_world);
        phys::RayHit hit;
        if (m_world->wheelProbe(spawn.m3 + Vec3{0, 5, 0}, spawn.m3 - Vec3{0, 30, 0}, hit, nullptr, nullptr))
            spawn.m3 = hit.position;
        car->reset(spawn);
        return car;
    }

    // lvlRoomInfo's flags of the room `p` is in (city::LevelRoomFlag), not
    // the PSDL's room flags.
    int levelRoomFlagsAt(const Vec3& p) const {
        if (!m_cityRenderer)
            return 0;
        const int room = m_cityRenderer->roomAt(p);
        if (room <= 0 || static_cast<std::size_t>(room) >= m_city->levelRoomFlags.size())
            return 0;
        return m_city->levelRoomFlags[static_cast<std::size_t>(room)];
    }

    // The water level under `p` if it lies in a Water of Death room
    // (lvlRoomInfo flag 4: a deepwater first texture, or listed in
    // city/<map>.water) and cityLevel::GetWaterLevel, the .water file's
    // level for every room.
    std::optional<float> waterLevelAt(const Vec3& p) const {
        if (!m_city->water || !(levelRoomFlagsAt(p) & city::LevelRoomFlag::WaterOfDeath))
            return std::nullopt;
        return m_city->water->height;
    }

    // mmGame::InitWeather: the tyres' WeatherFriction is 0.8 in rain, 0.75 in
    // rain at night, 1 otherwise.
    float weatherFriction() const {
        if (m_result.config.weather != game::Weather::Rain)
            return 1.0f;
        return m_result.config.timeOfDay == game::TimeOfDay::Night ? 0.75f : 0.8f;
    }

    std::unique_ptr<audio::game::OpponentCarAudio> loadAiCarAudio(Context& ctx, const std::string& vehicle,
                                                                   bool police) {
        if (!m_bank || !ctx.mixer)
            return nullptr;
        audio::game::CarAudioOptions opts;
        opts.city = m_result.config.city;
        opts.weather = surfaceWeather();
        opts.manager = &m_audioSlots;
        auto audio = std::make_unique<audio::game::OpponentCarAudio>();
        std::string error;
        if (!audio->load(ctx.game->vfs, *m_bank, *ctx.mixer, vehicle, police, opts, &error)) {
            log::debug("race: AI car audio {}: {}", vehicle, error);
            return nullptr;
        }
        return audio;
    }

    // Racers from the session (aiVehicleOpponent), driven by ai::Opponent.
    void spawnOpponents(Context& ctx) {
        if (!m_session || !m_world)
            return;
        const auto& setups = m_session->opponents();
        for (std::size_t i = 0; i < setups.size(); ++i) {
            const auto& s = setups[i];
            Opponent opp;
            opp.sessionIndex = i;
            Mat34 spawn = s.spawn;
            // aiVehiclePhysics::Init: vehCar::Init(<car>), the car's own tune
            // (the retail *_opp.vehCarSim files are not used by MM2).
            opp.sim = loadAiCar(ctx, s.vehicle, {}, spawn);
            if (!opp.sim)
                continue;
            opp.spawn = spawn;
            opp.renderer = std::make_unique<game::VehicleRenderer>(ctx.device(), *m_textures, *m_models,
                                                                    opp.sim->model(), static_cast<int>(m_opponents.size()) % 4);
            setupVehicleRenderer(ctx, *opp.renderer);
            opp.audio = loadAiCarAudio(ctx, s.vehicle, false);
            opp.fx = loadVehicleFx(ctx, s.vehicle, opp.sim->model(), *opp.renderer);
            opp.sim->sim().onImpactCallback = [fx = opp.fx.get(), sim = &opp.sim->sim(),
                                                sounds = opp.impacts](const phys::CarImpact& impact) {
                if (impact.sound)
                    sounds->push_back({impact.soundStrength, impact.audioId, impact.position});
                fx->impact(impact, *sim);
            };
            if (m_ai) {
                std::string error;
                opp.driver = ai::Opponent::create(m_ai->map(), opp.sim->sim(), s.path, s.params,
                                                  m_session->laps(), 1 + static_cast<int>(i),
                                                  static_cast<int>(i), &error, m_world.get(), s.vehicle);
                if (opp.driver)
                    opp.driver->setResetCar([&v = *opp.sim](const Mat34& m) { v.reset(m); });
                else
                    log::warn("race: opponent {} cannot drive: {}", s.vehicle, error);
            }
            m_opponents.push_back(std::move(opp));
        }
        log::info("race: {} opponents", m_opponents.size());
    }

    // The race's police posts (.aimap [Police]): aiMap::Init places the first
    // trunc(count * clamp(CopDensity, 0, 1)) of them, CopDensity being the
    // menu's cop density in every mode (the race menu starts it at the race
    // table's cop count, the crash course sets 1). Cops stay for the whole
    // race.
    void spawnPolice(Context& ctx) {
        if (!m_session || !m_world || !m_ai)
            return;
        const auto& posts = m_session->police();
        const std::size_t count = ai::PoliceSquad::countForDensity(posts.size(), m_result.config.copDensity);
        std::optional<float> chaseDistance;
        if (m_session->setup().aiMap)
            chaseDistance = m_session->setup().aiMap->copChaseDistance;
        m_police = std::make_unique<ai::PoliceSquad>(m_ai->map());
        // The game's room flags (lvlRoomInfo): a cop in a "water of death"
        // room drops out (aiPoliceOfficer::Update).
        m_police->setRoomFlags(m_city->levelRoomFlags);
        for (std::size_t i = 0; i < count; ++i) {
            const auto& p = posts[i];
            Cop cop;
            Mat34 post = p.spawn;
            // vpcop has a pursuit tune (vpcop_cop.vehcarsim); other cars use their base tune.
            cop.sim = loadAiCar(ctx, p.vehicle, {}, post);
            if (!cop.sim)
                continue;
            ai::PoliceSettings settings = ai::PoliceSettings::fromData(p.params, chaseDistance);
            settings.seed += i;
            cop.driver = &m_police->add(cop.sim->sim(), post, 100 + static_cast<int>(m_cops.size()), settings,
                                        p.vehicle);
            // vpcop paint job 0 is the California livery (vpcop_ca_*), 1 the
            // London one (vpcop_ln_*); picked by city (inferred).
            const int livery = str::iequals(m_result.config.city, "london") ? 1 : 0;
            cop.renderer = std::make_unique<game::VehicleRenderer>(ctx.device(), *m_textures, *m_models,
                                                                    cop.sim->model(), livery);
            setupVehicleRenderer(ctx, *cop.renderer);
            cop.audio = loadAiCarAudio(ctx, p.vehicle, true);
            cop.fx = loadVehicleFx(ctx, p.vehicle, cop.sim->model(), *cop.renderer);
            cop.sim->sim().onImpactCallback = [fx = cop.fx.get(), sim = &cop.sim->sim(),
                                                sounds = cop.impacts](const phys::CarImpact& impact) {
                if (impact.sound)
                    sounds->push_back({impact.soundStrength, impact.audioId, impact.position});
                fx->impact(impact, *sim);
            };
            m_cops.push_back(std::move(cop));
        }
        log::info("race: {} police cars", m_cops.size());
    }

    // A car as MM2's AI sees it (ai::trackedCar: its inertial frame, speed,
    // bumper and side distances).
    static ai::TrackedCar trackedCar(const phys::CarSim& sim, int id, bool player = false) {
        return ai::trackedCar(sim, id, player);
    }

    // aiMap::StopRoadTraffic: every racer's and cop's road window holds (or
    // lets go) the controlled roads of the intersections it drives into.
    void stopRoadTraffic(bool stop) {
        if (!m_ai)
            return;
        ai::Traffic& traffic = m_ai->traffic();
        auto stopSources = [&traffic](int node, bool s) { traffic.stopSources(node, s); };
        for (const auto& o : m_opponents)
            if (o.driver)
                o.driver->driver().stopRoadTraffic(stop, stopSources);
        for (const auto& c : m_cops)
            c.driver->driver().stopRoadTraffic(stop, stopSources);
    }

    // The ambient traffic, pedestrians and traffic lights (ai::World; the
    // lights run with them, where aiMap::Update runs its light sets after the
    // racers and the police).
    void updateAmbient(Context& ctx, float dt) {
        if (!m_ai || !m_player)
            return;
        const auto& sim = m_player->sim();
        ai::PlayerCar pc;
        pc.transform = sim.body.ics.matrix;
        pc.velocity = sim.body.ics.frameVelocity;
        // aiVehiclePlayer's side and bumper distances: half vehCarSim's Size
        // (its InertiaBox).
        pc.width = sim.params.inertiaBox.x;
        pc.length = sim.params.inertiaBox.z;
        // lvlInstance::GetRadius: the car's geometry radius.
        pc.radius = sim.body.radius();
        pc.steering = sim.steering;
        pc.reversing = sim.trans.getCurrentGear() < 0;
        pc.horn = hornDown(ctx);
        std::vector<Vec3> racers;
        for (const auto& o : m_opponents)
            racers.push_back(o.sim->sim().body.ics.matrix.m3);
        m_ai->setOpponents(racers);
        m_ai->update(dt, pc);
    }

    // Opponent and police AI: reads every car, writes the AI cars' inputs.
    void updateAiDrivers(float dt) {
        if (!m_player || (m_opponents.empty() && m_cops.empty()))
            return;
        std::vector<ai::TrackedCar> cars;
        ai::TrackedCar player = trackedCar(m_player->sim(), 0, true);
        player.isPlayer = true;
        player.suspect = true;
        player.reversing = m_player->sim().trans.getCurrentGear() == -1;
        const int impacts = m_vehicleImpacts + m_objectImpacts;
        player.collided = impacts != m_lastImpacts;
        m_lastImpacts = impacts;
        // aiVehiclePlayer::Update: the road the player is on, for the
        // drivers' obstacle checks.
        if (m_ai)
            m_ai->map().trackPlayer(player);
        cars.push_back(player);
        for (const auto& o : m_opponents) {
            ai::TrackedCar t = trackedCar(o.sim->sim(), 1 + static_cast<int>(o.sessionIndex));
            t.suspect = true;
            if (o.driver)
                o.driver->describe(t);
            cars.push_back(t);
        }
        for (const auto& c : m_cops) {
            ai::TrackedCar t = trackedCar(c.sim->sim(), c.driver->selfId());
            t.isPolice = true;
            cars.push_back(t);
        }
        if (m_ai) {
            for (const ai::AmbientCar& c : m_ai->cars())
                cars.push_back(ai::trackedAmbient(c, 10000 + c.id));
        }
        // aiMap::Update: the physics cars let the intersections ahead of them
        // go (aiMap::StopRoadTraffic(false)), drive, and hold them again
        // (StopRoadTraffic(true)) while the ambient traffic updates.
        stopRoadTraffic(false);
        for (auto& o : m_opponents) {
            if (!o.driver) {
                o.sim->sim().setInputs(0.0f, 1.0f, 0.0f, 1.0f);
                continue;
            }
            o.driver->setHeld(m_session &&
                              (!m_session->racersReleased() || !m_session->opponentActive(o.sessionIndex)));
            o.driver->update(dt, cars);
        }
        if (m_police)
            m_police->update(dt, cars, m_world.get(), !m_session || m_session->policeActive());
        stopRoadTraffic(true);
        // dgPhysManager::DeclareMover for the racers (aiRouteRacer::Update)
        // and the police (aiPoliceOfficer::Update), by their distance to the
        // player; a cop MM2 does not declare neither updates nor collides.
        for (auto& o : m_opponents) {
            if (!o.driver)
                continue;
            const ai::MoverDeclaration m = o.driver->mover();
            o.sim->sim().body.declare(m.type, m.flags);
        }
        for (auto& c : m_cops) {
            const ai::MoverDeclaration m = c.driver->mover();
            if (m.type != 0)
                c.sim->sim().body.declare(m.type, m.flags);
            else
                c.sim->sim().body.declared = false;
        }
        // Development aid: OPENMM2_DEBUG_AI logs the AI cars once a second.
        if (std::getenv("OPENMM2_DEBUG_AI") && std::floor(m_time) != std::floor(m_time - dt)) {
            const Vec3 p = m_player->sim().modelMatrix().m3;
            log::info("ai: t {:.0f} player ({:.0f},{:.0f}) {:.0f} mph", m_time, p.x, p.z, m_player->sim().speedMph());
            for (const auto& o : m_opponents) {
                const Vec3 q = o.sim->sim().modelMatrix().m3;
                log::info("ai:   opp {} ({:.0f},{:.0f}) {:.0f} mph mode {} progress {:.0f} m", o.sessionIndex, q.x, q.z,
                          o.sim->sim().speedMph(), o.driver ? static_cast<int>(o.driver->mode()) : -1,
                          o.driver ? o.driver->progress() : 0.0f);
            }
            for (const auto& c : m_cops) {
                const Vec3 q = c.sim->sim().modelMatrix().m3;
                log::info("ai:   cop {} ({:.0f},{:.0f}) {:.0f} mph mode {} siren {}", c.driver->selfId(), q.x, q.z,
                          c.sim->sim().speedMph(), static_cast<int>(c.driver->mode()), c.driver->siren());
            }
        }
    }

    // The pedestrians' screams (aiPedAudio): a dodge this step asks for a
    // sound slot and queues a line; the voices take the player's speed.
    void updatePedestrianAudio(float dt) {
        if (!m_ai || !m_player)
            return;
        m_pedSounds.clear();
        for (const auto& p : m_ai->peds())
            m_pedSounds.push_back({p.id, p.typeName, p.transform.m3, p.scream, p.placed});
        m_pedAudio.update(m_pedSounds, m_camera.transform, m_player->sim().speed(), dt, m_tunnel);
    }

    // Engine, tyre and siren sounds of the opponents and police, positioned.
    void updateAiAudio(float dt) {
        const Mat34& listener = m_camera.transform;
        auto feed = [&](audio::game::OpponentCarAudio& audio, const phys::CarSim& sim, bool siren,
                        bool pursuingPlayer, bool wrecked, std::vector<audio::game::ImpactInput>& impacts) {
            audio::game::CarAudioInputs in = carAudioInputs(sim);
            in.wrecked = wrecked;
            in.throttle = sim.engine.throttle;
            in.brake = sim.brakes;
            in.transform = sim.modelMatrix();
            in.siren = siren;
            // aiPoliceOfficer::StartSiren(IsPlayer): only a chase of the
            // player counts for the cop chase music.
            in.sirenPursuingPlayer = pursuingPlayer;
            // vehCarDamage::ApplyImpact's AudImpact::Play for this car.
            in.impacts = std::move(impacts);
            impacts.clear();
            audio.update(in, dt, listener);
        };
        for (auto& o : m_opponents)
            if (o.audio)
                feed(*o.audio, o.sim->sim(), false, false, o.sim->sim().damage.wrecked(), *o.impacts);
        // A cop's explosion follows its driver's wrecked flag (aiVehiclePhysics
        // +0x9686: set the first update past MaxDamage, cleared by Reset).
        for (auto& c : m_cops)
            if (c.audio)
                feed(*c.audio, c.sim->sim(), c.driver->siren(), c.driver->target() == 0, c.driver->driver().wrecked(),
                     *c.impacts);
    }

    // The ambient cars' sounds (aiAmbientVehicleAudio, which aiVehicleSpline::Init
    // gives every pooled car; OpenMM2 loads it when the car first drives):
    // engine, horn and impacts, and the driver's voice (AudCreature).
    void updateAmbientAudio(Context& ctx, float dt) {
        if (!m_ai || !m_bank || !ctx.mixer || !m_player)
            return;
        const Mat34& listener = m_camera.transform;
        // aiMap::Update: aiAmbientVehicleAudio::UpdateStatics and
        // AudCreatureContainer::UpdateStatics, with the player's speed, each
        // advance the shared impact-line clock (so it runs twice a frame) and
        // update the voices.
        const float playerSpeed = m_player->sim().speed();
        audio::game::CreatureVoice::advanceClock(dt);
        audio::game::CreatureVoice::advanceClock(dt);
        std::vector<char> seen;
        for (const ai::AmbientCar& c : m_ai->cars()) {
            AmbientAudio* a = ambientAudio(ctx, c);
            if (!a)
                continue;
            seen.resize(m_ambientAudio.size(), 0);
            seen[static_cast<std::size_t>(c.id)] = 1;
            a->active = true;
            if (a->hasVoice)
                a->voice.update(playerSpeed, dt);
            a->car.update(c.speed, c.transform, c.velocity, dt, listener);
            // The voice follows the car's attenuation and pan while the car
            // holds its sound slot.
            if (a->hasVoice && a->car.audible())
                a->voice.updateAttenuation(a->car.attenuation(), a->car.pan(), a->car.distance2());
        }
        for (std::size_t i = 0; i < m_ambientAudio.size(); ++i) {
            AmbientAudio* a = m_ambientAudio[i].get();
            if (a && a->active && (i >= seen.size() || !seen[i])) {
                // Back in the pool (aiAmbientVehicleAudio::Reset).
                a->car.stop();
                a->active = false;
            }
        }
        // aiGoalAvoidPlayer::Reset: PlayAvoidanceHorn, and when a horn
        // pattern starts, PlayAvoidanceReaction (for a car holding a sound
        // slot).
        for (int id : m_ai->takeAvoidEvents()) {
            if (AmbientAudio* a = ambientAudioOf(id); a && a->car.honk() && a->hasVoice && a->car.audible())
                a->voice.avoid();
        }
        // aiVehicleActive's impact callback: AudImpact::Play and
        // PlayImpactHorn with |x| + |y| + |z| of the impulse, then
        // PlayImpactReaction (for a car holding a sound slot).
        for (const game::TrafficImpact& e : m_trafficImpacts) {
            AmbientAudio* a = ambientAudioOf(e.carId);
            if (!a)
                continue;
            a->car.impact({e.strength, e.audioId, {}});
            if (a->hasVoice && a->car.audible())
                a->voice.impact(e.strength);
        }
        m_trafficImpacts.clear();
    }

    struct AmbientAudio : audio::game::CreatureVoice::Owner {
        audio::game::AmbientCarAudio car;
        audio::game::CreatureVoice voice;
        bool hasVoice = false;
        bool active = false;
        // The voice's container is the car's audio (its 3D slot).
        bool requestSlot() override { return car.audible(); }
    };

    AmbientAudio* ambientAudioOf(int id) {
        if (id < 0 || static_cast<std::size_t>(id) >= m_ambientAudio.size())
            return nullptr;
        return m_ambientAudio[static_cast<std::size_t>(id)].get();
    }

    // The audio of ambient car `c`, loaded on first use: the engine and horn
    // of its type (else the defaults) and its driver's voice
    // (aiAmbientVehicleAudio::Init).
    AmbientAudio* ambientAudio(Context& ctx, const ai::AmbientCar& c) {
        if (c.id < 0 || !c.data)
            return nullptr;
        const auto id = static_cast<std::size_t>(c.id);
        if (id >= m_ambientAudio.size())
            m_ambientAudio.resize(id + 1);
        if (!m_ambientAudio[id]) {
            auto a = std::make_unique<AmbientAudio>();
            if (!a->car.load(ctx.game->vfs, *m_bank, *ctx.mixer, c.data->model, &m_audioSlots))
                return nullptr;
            if (const auto* def = ambientVoice(ctx.game->vfs, c.data->model)) {
                a->voice.load(*ctx.mixer, *m_bank, *def);
                a->voice.setOwner(a.get());
                a->hasVoice = true;
            }
            m_ambientAudio[id] = std::move(a);
        }
        return m_ambientAudio[id].get();
    }

    // aiAmbientVehicleAudio::LoadVoices: aud/creaturedata/<type>_ambcarvoice<c>,
    // else default_ambcarvoice<c><n>, with c "_l" in London and "_s"
    // elsewhere (mmGame::Init's SetCSVCatString) and n drawn once per session
    // from numambcarvoicefiles<c> (LoadNumVFileChoices).
    const audio::game::CreatureVoiceDef* ambientVoice(const vfs::Vfs& vfs, const std::string& type) {
        if (m_voiceFileNum < 0) {
            m_voiceCategory = m_result.config.city == "london" ? "_l" : "_s";
            m_voiceFileNum = 0;
            const auto text =
                audio::game::readText(vfs, "aud/creaturedata/numambcarvoicefiles" + m_voiceCategory + ".csv");
            if (const auto n = text ? audio::game::parseNumFileChoices(*text) : std::nullopt)
                m_voiceFileNum = static_cast<int>(audio::randomizeNumber(1.0f, *n + 0.25f));
        }
        auto [it, fresh] = m_voiceDefs.try_emplace(type);
        if (fresh) {
            std::string path = std::format("aud/creaturedata/{}_ambcarvoice{}.csv", type, m_voiceCategory);
            if (!vfs.exists(path))
                path = std::format("aud/creaturedata/default_ambcarvoice{}{}.csv", m_voiceCategory,
                                   m_voiceFileNum);
            if (const auto text = audio::game::readText(vfs, path))
                it->second = audio::game::parseCreatureVoice(*text);
        }
        return it->second ? &*it->second : nullptr;
    }

    game::session::PlayerState playerState() const {
        game::session::PlayerState ps;
        const auto& sim = m_player->sim();
        // The rules (mmWaypoints, mmGame::Update, mmSingleStunt) and the map
        // read the car's phInertialCS matrix (vehCarSim +0x6C).
        ps.transform = sim.body.ics.matrix;
        ps.velocity = sim.body.ics.frameVelocity;
        ps.speedMph = sim.speedMph();
        ps.rpm = std::max(sim.engine.rpm, sim.params.engine.idleRPM);
        ps.maxRpm = sim.params.engine.maxRPM;
        ps.gear = sim.trans.getCurrentGear();
        ps.automatic = m_result.config.automatic;
        ps.throttle = m_lastPedals.accelerator;
        ps.damage01 = sim.damage.damage;
        // mmPlayer::IsMaxDamaged: CurrentDamage strictly past MaxDamage.
        ps.wrecked = sim.damage.maxDamaged();
        if (const auto level = waterLevelAt(ps.transform.m3))
            ps.inWater = ps.transform.m3.y < *level;
        ps.vehicleImpacts = m_vehicleImpacts;
        ps.objectImpacts = m_objectImpacts;
        ps.inertiaBox = sim.params.inertiaBox;
        return ps;
    }

    void updateSession(Context& ctx, float dt) {
        if (!m_session || !m_player)
            return;
        m_playerState = playerState();
        auto carState = [](const phys::CarSim& sim) {
            game::session::OpponentState s;
            s.transform = sim.body.ics.matrix; // as mmWaypoints::AIWPHit gets it
            s.velocity = sim.body.ics.frameVelocity;
            s.damage01 = sim.damage.damage;
            s.wrecked = sim.damage.wrecked();
            s.currentDamage = sim.damage.currentDamage;
            s.inertiaBox = sim.params.inertiaBox;
            return s;
        };
        // In Session::opponents() order; a car that failed to load stays parked at its spawn.
        std::vector<game::session::OpponentState> opps;
        for (const auto& s : m_session->opponents()) {
            game::session::OpponentState parked;
            parked.transform = s.spawn;
            opps.push_back(parked);
        }
        for (const auto& o : m_opponents) {
            auto& st = opps[o.sessionIndex] = carState(o.sim->sim());
            st.finished = o.driver && o.driver->finished(); // aiRouteRacer::Finished
        }
        std::vector<game::session::OpponentState> cops;
        for (const auto& c : m_cops) {
            auto& st = cops.emplace_back(carState(c.sim->sim()));
            st.pursuing = c.driver->mode() == ai::PoliceCar::Mode::Chasing && c.driver->target() == 0;
        }
        const auto phaseBefore = m_session->phase();
        m_session->setPreRaceCamera(m_cams.preRace());
        // The multiplayer countdown (2.5 s) starts with the host's start
        // message; OpenMM2 shares the start time instead.
        m_session->setStartSignal(!multiplayer(ctx) || ctx.netGame->secondsToStart() <= 2.5);
        m_session->update(dt, m_playerState, opps, cops);
        // mmSingleStunt::UpdateEvade turns the map on during its first line.
        if (m_hud && m_session->wantsMap() && m_cams.mapMode() == game::MapMode::Off)
            m_cams.cycleMap();
        // mmPlayer::SetPostRaceCam when the race is over (not in cruise).
        if (phaseBefore != game::session::Phase::PostRace && m_session->phase() == game::session::Phase::PostRace &&
            m_result.config.mode != game::GameMode::Cruise)
            startFinishCamera(ctx);
        for (const auto& e : m_session->takeEvents()) {
            using game::session::EventType;
            if (e.type == EventType::HitWater)
                m_cams.startWaterCam();
            // mmSingleStunt::InitHUD turns the icons on for the follow and
            // destroy events (iconState, mmGame::SetIconsState).
            if (e.type == EventType::LessonEventStarted && m_hud) {
                const auto& events = m_session->setup().lessonEvents;
                if (e.index >= 0 && static_cast<std::size_t>(e.index) < events.size() &&
                    (events[static_cast<std::size_t>(e.index)].type == game::session::LessonType::Follow ||
                     events[static_cast<std::size_t>(e.index)].type == game::session::LessonType::Destroy))
                    m_hud->options().opponentIcons = true;
            }
            if (e.type == EventType::Respawn) {
                m_player->reset(m_session->respawnTransform());
                if (m_vehicleFx)
                    m_vehicleFx->reset(); // vehCar::Reset
                if (m_vehicle)
                    m_vehicle->resetDamage();
                m_cams.reset(cameraTarget());
            } else if (e.type == EventType::Restart) {
                // The race starts over (mmGame::Reset): every car to its start,
                // and the elasticity cap back to 1 (the "/blubber" cheat's 4).
                phys::setElasticityCap(phys::kElasticityCap);
                m_player->reset(m_spawn);
                if (m_vehicleFx)
                    m_vehicleFx->reset();
                if (m_vehicle)
                    m_vehicle->resetDamage();
                for (auto& o : m_opponents) {
                    o.sim->reset(o.spawn);
                    if (o.fx)
                        o.fx->reset();
                    o.renderer->resetDamage();
                    if (o.driver)
                        o.driver->reset();
                }
                for (auto& c : m_cops)
                    c.driver->reset();
                m_cams.reset(cameraTarget());
                // The race modes' Reset: mmPlayer::SetPreRaceCam again.
                if (m_result.config.mode != game::GameMode::Cruise && !multiplayer(ctx))
                    m_cams.startPreRace();
                m_steering.reset();
            } else if (e.type == EventType::DamageReset) {
                m_player->sim().damage.reset();
                if (m_vehicle)
                    m_vehicle->resetDamage(); // vehCar::ClearDamage
            } else if (e.type == EventType::PlayerDamageLimits) {
                auto& d = m_player->sim().damage.params;
                d.maxDamage = e.value;
                d.medDamage = e.value * 0.5f;
                d.impactThreshold = 0.0f;
            } else if (e.type == EventType::OpponentDamageLimits) {
                for (auto& o : m_opponents)
                    if (o.sessionIndex == static_cast<std::size_t>(e.index)) {
                        o.sim->sim().damage.params.maxDamage = e.value;
                        o.sim->sim().damage.params.medDamage = e.value * 0.5f;
                    }
            } else if (e.type == EventType::Sound) {
                playGameSound(ctx, static_cast<game::session::GameSound>(e.index), e.value);
            } else if (e.type == EventType::Speech) {
                announce(static_cast<game::session::SpeechCue>(e.index), e.value);
            } else if (e.type == EventType::CheckpointCleared) {
                // mmWaypoints::DisplayHUDMessage: the crash course's location
                // line for the checkpoint (mmCCSpeech::PlayCheckPoint, 0.01 s).
                if (m_announcerOk && m_result.config.mode == game::GameMode::CrashCourse)
                    m_announcer.playCrashCourseCheckPoint(e.index, 0.01f);
            }
            // OpponentFinished needs nothing: the game only asks
            // aiRouteRacer::Finished (OpponentState::finished), and the car
            // drives on to its destination.
        }
        if (auto* music = ctx.music(); music && m_musicDirector) {
            using game::session::Phase;
            auto& director = *m_musicDirector;
            const Phase phase = m_session->phase();
            if (phase != Phase::Countdown)
                director.raceStarted();
            if (phase == Phase::PostRace && !m_musicFinished) {
                // The race modes stop the music at the finish (StopSegment(0)),
                // a wreck with an ending on the next beat (StopSegment(1)).
                if (m_session->damagedOut())
                    director.damagedOut();
                else
                    director.finish();
                m_musicFinished = true;
            }
            if (phase == Phase::Done && !m_musicResults) {
                director.results();
                m_musicResults = true;
            }
            director.update(dt, m_player->sim().speed(), audio::game::SirenPlayer::copsPursuingPlayer(),
                            m_carAudio.airborne());
            for (const auto& c : director.takeCommands())
                music->setState(c.state, c.timing);
            if (director.takeBigAir())
                music->triggerBigAir();
        }
        if (m_session->finished() && !m_resultsShown) {
            m_resultsShown = true;
            m_result = m_session->result();
            // A lost race or lesson (state 4) opens the main menu without
            // pausing (mmPopup::ProcessEscape(0)); a finished one shows the
            // results (mmPopup::ShowResults), which OpenMM2 shows as the
            // first page of the menus.
            if (!m_result.finished && !multiplayer(ctx))
                openPopup(ctx, false);
            else
                leaveRace(ctx, m_result);
        }
    }

    // The camera at the end of a race: mmPlayer::SetPostRaceCam, or in a
    // multiplayer checkpoint race or circuit mmGameMulti::SetFinishCam: the
    // orbit camera on the finish (the last waypoint, a circuit's first) at
    // azimuth (heading + 180) x -0.017453292, which the keyboard then turns.
    void startFinishCamera(Context& ctx) {
        const auto& cps = m_session->checkpoints();
        const auto mode = m_result.config.mode;
        if (!multiplayer(ctx) || mode == game::GameMode::Blitz || cps.empty()) {
            m_cams.startPostRace();
            return;
        }
        const auto& finish = mode == game::GameMode::Circuit ? cps.front() : cps.back();
        m_cams.startMultiplayerPostRace(finish.position, (finish.headingDeg + 180.0f) * -0.017453292f);
    }

    // The modes' and mmWaypoints' 2D sounds (AudSoundBase): `mode` 0 plays
    // once (the timer warning only when it is not still playing), 1 loops,
    // -1 stops.
    void playGameSound(Context& ctx, game::session::GameSound sound, float mode) {
        if (!ctx.mixer || !m_bank)
            return;
        const std::string name = game::session::gameSoundName(sound);
        auto it = m_gameSounds.find(name);
        if (it == m_gameSounds.end()) {
            audio::game::SoundSlot slot;
            slot.load(*ctx.mixer, *m_bank, name, audio::Bus::Effects);
            it = m_gameSounds.emplace(name, std::move(slot)).first;
        }
        auto& slot = it->second;
        if (!slot.valid())
            return;
        const float volume =
            game::session::gameSoundVolume(sound, m_result.config.mode == game::GameMode::CrashCourse);
        if (mode < 0.0f)
            slot.stop();
        else if (mode > 0.0f)
            slot.playLoop(volume, 1.0f);
        else if (sound != game::session::GameSound::TimerWarning || !slot.playing())
            slot.playOnce(volume);
    }

    // The announcer's lines the modes ask for (mmRaceSpeech, mmCCSpeech).
    void announce(game::session::SpeechCue cue, float value) {
        if (!m_announcerOk)
            return;
        using game::session::SpeechCue;
        const bool lessons = m_result.config.mode == game::GameMode::CrashCourse;
        switch (cue) {
        case SpeechCue::PreRace:
            if (lessons)
                m_announcer.playCrashCoursePreRace();
            else
                m_announcer.playPreRace();
            break;
        case SpeechCue::Results:
            if (!lessons)
                announceResults(static_cast<int>(value));
            break;
        case SpeechCue::ResultsPoor:
            if (!lessons)
                m_announcer.playResults(10, 10);
            break;
        case SpeechCue::DamagePenalty:
            if (!lessons)
                m_announcer.playDamagePenalty();
            break;
        case SpeechCue::LessonResults:
            if (lessons)
                m_announcer.playCrashCourseResults(value != 0.0f);
            break;
        }
    }

    // The modes' RegisterFinish: a finish under the race's table settings is
    // registered with the driver, and mmGameSingle::UpdateRewards then has
    // the announcer name the car or paint job it unlocks, or else play the
    // results for the place (PlayResults(place, (int)OpponentDensity)).
    // OpenMM2's frontend stores the finish when the race is left; the race
    // works out the same outcome on a copy of the driver.
    void announceResults(int place) {
        if (!m_profile || !m_session)
            return;
        game::RaceResult result = m_session->result();
        if (result.cheated)
            return; // RegisterFinish registers nothing while bCheating is set
        game::RaceConfig defaults = result.config;
        game::session::applyRaceTableDefaults(defaults, m_session->setup().race);
        if (!game::Progress::recordable(result.config, defaults))
            return;
        if (!m_progress)
            m_progress = game::Progress::load(*m_vfs);
        game::Profile copy = *m_profile;
        if (const auto reward = m_progress->record(copy, result)) {
            if (reward->variant == 0 && m_announcer.loadVehicleUnlock(reward->vehicle)) {
                m_announcer.playUnlockVehicle();
                return;
            }
            if (reward->variant != 0 && m_announcer.loadTextureUnlock(reward->vehicle)) {
                m_announcer.playUnlockTexture();
                return;
            }
        }
        m_announcer.playResults(place, m_result.config.opponents);
    }

    // mmPopup::ProcessChat ("Enter Chat Msg"): the chat line pops up without
    // pausing the game.
    void openChat(Context& ctx) {
        m_popup = Popup::Chat;
        m_popupPaused = false;
        m_chatText.clear();
        ctx.input.startTextInput(ctx.window());
        m_textInput = true;
        buildPopup(ctx);
    }

    // The game's SendChatMessage. mmGame (single player) only checks for the
    // "/blubber" cheat: the cheat flag, the elasticity cap 4 and elasticity 4
    // on the player's bound. mmGameMulti::ParseChatMessage keeps "/rc ..."
    // (debug commands) to itself, sends "/wav ..." without posting it, and
    // sends and posts anything else (OpenMM2's session echoes the line back
    // and the race posts it then).
    void sendChatMessage(Context& ctx, const std::string& text) {
        if (!multiplayer(ctx)) {
            if (text.starts_with("/blubber")) {
                game::session::setCheating(true);
                phys::setElasticityCap(phys::kBlubberElasticityCap);
                if (m_player)
                    m_player->sim().setBoundElasticity(phys::kBlubberElasticityCap);
            }
            return;
        }
        if (text.size() > 4 && text.starts_with("/rc"))
            return;
        ctx.netGame->sendChat(text);
    }

    // mmGameMulti's chat message (0x1f8): "name: text" on the HUD; the
    // player's own lines (echoed by the session) as typed.
    void postIncomingChat(Context& ctx) {
        if (!multiplayer(ctx) || !m_hud)
            return;
        const auto& lines = ctx.netGame->chat();
        if (m_chatSeen > lines.size())
            m_chatSeen = 0;
        for (std::size_t i = m_chatSeen; i < lines.size(); ++i) {
            const auto& line = lines[i];
            if (line.system || line.text.starts_with("/wav"))
                continue;
            m_hud->postChat(line.from == ctx.netGame->localId() ? line.text : std::format("{}: {}", line.name, line.text));
        }
        m_chatSeen = lines.size();
    }

    // --- Cops and Robbers (mmMultiCR) ------------------------------------------------------

    game::session::CrTeam crTeam(Context& ctx, const std::string& vehicle, int lobbyTeam) const {
        // mmMultiCR::InitMyPlayer: Cops vs. Robbers and Free-For-All take the
        // team from the car (vehicle flag 0x08, a police car: team 0); Robber
        // Teams from the lobby. (OpenMM2's lobby gives Cops vs. Robbers
        // players the car of their team.)
        using game::session::CrTeam;
        const auto* info = ctx.game->catalog.vehicle(vehicle);
        const bool police = info && (info->flags & 0x08);
        switch (m_result.config.copsAndRobbers) {
        case game::CopsAndRobbersMode::RobberTeams: return lobbyTeam == 0 ? CrTeam::Blue : CrTeam::Red;
        case game::CopsAndRobbersMode::CopsVsRobbers: return lobbyTeam == 0 || police ? CrTeam::Cop : CrTeam::Robber;
        default: return police ? CrTeam::Cop : CrTeam::Robber;
        }
    }

    void setupCopsAndRobbers(Context& ctx) {
        if (!multiplayer(ctx) || m_result.config.mode != game::GameMode::CopsAndRobbers)
            return;
        auto locations = game::session::loadCrLocations(ctx.game->vfs, m_city->info.raceDir);
        if (!locations) {
            log::warn("race: no Cops and Robbers places for {}", m_city->info.raceDir);
            return;
        }
        game::session::CrSettings st;
        st.mode = m_result.config.copsAndRobbers;
        st.goldMass = ctx.netGame->goldMass();
        st.timeLimitSeconds = m_result.config.timeLimitMinutes * 60.0f;
        st.pointLimit = m_result.config.pointLimit;
        // Every machine starts with the same places (OpenMM2: the shared
        // start time seeds them; the host's sets follow by message).
        st.seed = std::max(1u, ctx.netGame->raceStartTime());
        m_crRng = st.seed;
        st.randomIntersection = [this]() -> std::optional<Vec3> {
            if (!m_city->aiMap || m_city->aiMap->intersections.size() < 2)
                return std::nullopt;
            m_crRng = m_crRng * 1103515245u + 12345u;
            const auto& xs = m_city->aiMap->intersections;
            return xs[1 + ((m_crRng >> 16) & 0x7fffu) % (xs.size() - 1)].center;
        };
        // mmMultiCR::DropGold: on the AI map's roads and intersections
        // (aiMap::PositionToAIMapComp), not in deep water; inferred here from
        // the level's street rooms.
        st.canDropAt = [this](const Vec3& p) {
            const int f = levelRoomFlagsAt(p);
            return (f & city::LevelRoomFlag::OpenRoad) && !(f & city::LevelRoomFlag::WaterOfDeath);
        };
        m_cr = std::make_unique<game::session::CopsAndRobbers>(st, *locations);
        m_crSelf = ctx.netGame->localId();
        for (const auto& p : ctx.netGame->players())
            m_cr->addCar(p.id, crTeam(ctx, p.id == m_crSelf ? m_result.config.vehicle : p.car, p.team));
        m_crMyTeam = m_cr->teamOf(m_crSelf);
        m_regen = true; // mmMultiCR::InitMyPlayer: mmPlayer::EnableRegen(1)
        // mmSpeechContainer::InitCNR loads the Cops and Robbers lines; build
        // 3393 never plays them (nothing calls mmCNRSpeech::Play).
        if (m_announcerOk || ctx.settings.commentary)
            m_announcer.beginCopsAndRobbers();
    }

    // mmMultiCR::FondleCarMass: the gold's mass on the carrier
    // (phInertialCS::Init with the mass changed) and its throttle cap.
    void fondleMass(float kg) {
        auto& ics = m_player->sim().body.ics;
        ics.init(ics.mass + kg, ics.inertia.x, ics.inertia.y, ics.inertia.z);
        m_throttleCap = kg > 0.0f ? m_cr->carrierThrottleCap() : 1.0f;
    }

    void sendCr(Context& ctx, const std::vector<game::session::CopsAndRobbers::Message>& messages) {
        using Type = game::session::CopsAndRobbers::Message::Type;
        for (const auto& m : messages) {
            switch (m.type) {
            case Type::PickupRequest:
                ctx.netGame->sendEvent(kCrPickupRequest,
                                       net::encodePayload(net::GoldEvent{m.position, static_cast<std::uint8_t>(m.car)}),
                                       net::kHostPlayerId);
                break;
            case Type::GoldTaken: ctx.netGame->sendGold(net::GameEventType::GoldPickedUp, m.position, m.car); break;
            case Type::GoldDropped: ctx.netGame->sendGold(net::GameEventType::GoldDropped, m.position, m.car); break;
            case Type::GoldDelivered:
                ctx.netGame->sendGold(net::GameEventType::GoldDelivered, m.position, m.car);
                break;
            case Type::NewSet:
                ctx.netGame->sendEvent(kCrNewSet, net::encodePayload(CrSetEvent{m.set.gold, m.set.bank, m.set.hideout}));
                break;
            }
        }
    }

    void updateCopsAndRobbers(Context& ctx, float dt) {
        if (!m_cr || !m_player || !m_session)
            return;
        using game::session::CopsAndRobbers;
        using Type = CopsAndRobbers::Message::Type;
        const bool host = ctx.netGame->isHost();
        // mmPlayer::UpdateRegen while regeneration is on.
        if (m_regen && m_player->sim().regenerate() && m_vehicle)
            m_vehicle->resetDamage();
        // mmMultiCR::UpdateGame for the local car, with the others' places.
        std::vector<CopsAndRobbers::Car> cars;
        cars.push_back({m_crSelf, m_crMyTeam, m_player->sim().body.ics.matrix.m3, m_playerState.wrecked,
                        m_playerState.inWater});
        for (const auto& rc : ctx.netGame->remoteCars())
            if (rc.hasState)
                cars.push_back({rc.id, m_cr->teamOf(rc.id), rc.transform.m3, (rc.flags & net::kVehicleWrecked) != 0,
                                false});
        sendCr(ctx, m_cr->updateNetwork(dt, m_crSelf, host, cars, m_crImpacts));
        m_crImpacts.clear();
        // The others' messages (mmMultiCR::GameMessage).
        for (const auto& ev : ctx.netGame->takeGameEvents()) {
            CopsAndRobbers::Message m;
            const auto type = static_cast<std::uint16_t>(ev.type);
            if (type == kCrPickupRequest) {
                m.type = Type::PickupRequest;
                m.car = ev.from;
            } else if (type == kCrNewSet) {
                const auto set = ev.as<CrSetEvent>();
                if (!set)
                    continue;
                m.type = Type::NewSet;
                m.set = {set->bank, set->gold, set->hideout};
            } else if (const auto g = ev.as<net::GoldEvent>(); g && (ev.type == net::GameEventType::GoldPickedUp ||
                                                                     ev.type == net::GameEventType::GoldDropped ||
                                                                     ev.type == net::GameEventType::GoldDelivered)) {
                m.type = ev.type == net::GameEventType::GoldPickedUp  ? Type::GoldTaken
                         : ev.type == net::GameEventType::GoldDropped ? Type::GoldDropped
                                                                      : Type::GoldDelivered;
                m.car = g->team;
                m.position = g->position;
            } else {
                continue;
            }
            sendCr(ctx, m_cr->receive(m, ev.from, host));
        }
        // What happened, on this machine's HUD and car.
        auto name = [&](int id) {
            const auto* p = ctx.netGame->player(static_cast<std::uint8_t>(id));
            return p ? p->name : std::string();
        };
        const auto& s = ctx.game->strings;
        for (const auto& e : m_cr->takeEvents()) {
            using E = CopsAndRobbers::EventType;
            const bool me = e.car == m_crSelf;
            switch (e.type) {
            case E::GoldTaken:
                if (me) {
                    // StealGold: the mass, no regeneration, "You have the Gold!".
                    fondleMass(m_cr->carrierExtraMassKg());
                    m_regen = false;
                    m_session->showMessage(s.get(host ? 115 : 134, "You have the Gold!"), 5.0f, false);
                } else {
                    m_session->showMessage(std::format("{} {}", name(e.car), s.get(135, "has the Gold!")), 5.0f, false);
                }
                break;
            case E::GoldDropped:
                if (me) {
                    fondleMass(-m_cr->carrierExtraMassKg());
                    m_regen = true;
                    if (!m_playerState.wrecked)
                        m_session->showMessage(s.get(112, "You dropped the gold!"), 5.0f, false);
                } else {
                    m_session->showMessage(std::format("{} {}", name(e.car), s.get(136, "dropped the Gold!")), 5.0f,
                                           false);
                }
                break;
            case E::GoldDelivered:
                if (me) {
                    // UpdateBank / UpdateHideout: regeneration, a repaired car.
                    fondleMass(-m_cr->carrierExtraMassKg());
                    m_regen = true;
                    m_player->sim().damage.reset();
                    if (m_vehicle)
                        m_vehicle->resetDamage();
                    m_session->showMessage(s.get(117, "Gold delivered!"), 5.0f, false);
                } else {
                    m_session->showMessage(std::format("{} {}", name(e.car), s.get(137, "delivered the Gold!")),
                                           5.0f, false);
                }
                break;
            case E::TimeWarning: {
                // UpdateTimeWarning: 20, 15, 10, 5, 1 minutes (138-142).
                static constexpr std::pair<int, std::uint32_t> kIds[] = {{20, 138}, {15, 139}, {10, 140}, {5, 141}, {1, 142}};
                for (const auto& [minutes, id] : kIds)
                    if (minutes == e.value)
                        m_session->showMessage(s.get(id, ""), 5.0f, false);
                break;
            }
            case E::TimeUp:
            case E::PointLimit:
                // UpdateLimit: the message, then 3 s to the results (state 9).
                m_session->showMessage(s.get(e.type == E::TimeUp ? 118 : 119, ""), 5.0f, false);
                m_crEnd = 3.0f;
                break;
            case E::NewSet: break;
            }
        }
        if (m_crEnd >= 0.0f) {
            m_crEnd -= dt;
            if (m_crEnd < 0.0f) {
                // FillResults; mmPlayer +0x2258; ShowResults.
                m_crFinished = true;
                game::RaceResult r = m_session->result();
                r.ended = true;
                leaveRace(ctx, r);
                return;
            }
        }
        // The objects and readouts (mmWaypointObject, mmArrow, mmCRHUD).
        if (m_hud) {
            game::session::CrDisplay d;
            d.enabled = true;
            d.time = static_cast<float>(m_time);
            if (m_cr->goldActive() || m_cr->goldCarrier() >= 0)
                d.gold = m_cr->goldPosition();
            const bool teams = m_result.config.copsAndRobbers != game::CopsAndRobbersMode::FreeForAll;
            const bool colours = m_result.config.copsAndRobbers == game::CopsAndRobbersMode::RobberTeams;
            d.bases.push_back({colours ? "pt_blue" : "pt_bank", m_cr->set().bank});
            d.bases.push_back({colours ? "pt_red" : "pt_hideout", m_cr->set().hideout});
            d.arrowInterest = m_cr->goldCarrier() == m_crSelf ? m_cr->deliveryTarget(m_crMyTeam) : m_cr->goldPosition();
            d.teams = teams;
            d.copsVsRobbers = m_result.config.copsAndRobbers == game::CopsAndRobbersMode::CopsVsRobbers;
            d.blueScore = m_cr->score(game::session::CrTeam::Cop);
            d.redScore = m_cr->score(game::session::CrTeam::Robber);
            d.playerScore = m_cr->playerScore(m_crSelf);
            d.timeLeft = m_cr->timeRemaining();
            m_hud->setCopsAndRobbers(std::move(d));
        }
    }

    // --- The in-race popup (mmPopup, PUMain, PUExit) ---------------------------------------

    void openPopup(Context& ctx, bool pause) {
        m_popup = Popup::Main;
        // ProcessEscape: pauses unless the game already is (the full-screen
        // map), and remembers it so closing does not resume it.
        m_popupPaused = pause && !multiplayer(ctx) && !m_paused;
        if (m_popupPaused)
            m_paused = true;
        buildPopup(ctx);
    }

    void closePopup() {
        // mmPopup::DisablePU.
        m_popup = Popup::None;
        // Buttons close the popup from inside its update: keep the menu
        // until the next frame.
        if (m_popupMenu)
            m_popupGraveyard.push_back(std::move(m_popupMenu));
        if (m_popupPaused)
            m_paused = false;
        m_popupPaused = false;
    }

    // The quit button: back to the race menu (or the crash course page),
    // without the results.
    void quitToMenu(Context& ctx) {
        game::RaceResult r = m_session ? m_session->result() : m_result;
        r.ended = false;
        leaveRace(ctx, r);
    }

    // Back to the menus. mmPlayerConfig::GetViewSettings when the game ends:
    // the driver keeps the camera, wide angle, dashboard and mirror choices
    // (stored before the frontend reads the driver again).
    void leaveRace(Context& ctx, const game::RaceResult& result) {
        if (m_profile) {
            const auto v = m_cams.viewSettings();
            m_profile->camera = v.camera;
            m_profile->wideAngle = v.wideAngle;
            m_profile->dashboard = v.dashboard;
            m_profile->mirror = m_mirror.enabled();
            if (!m_profile->save())
                log::warn("race: cannot save driver '{}'", m_profile->name);
        }
        ctx.nextScreen = makeFrontendScreen(ctx, result);
    }

    void buildPopup(Context& ctx) {
        // mmPopup(game, 0.2, 0.1, 0.6, 0.8): the popup card covers x 0.2-0.8
        // and y 0.1-0.9 of the screen. PUMain's buttons sit at 0.125, 0.25,
        // 0.375 and 0.5 of it, "Resume Driving" (PUMenuBase::AddExit) at
        // x 0.5, y 0.9; PUExit's question at 0.2 and Yes / No at 0.7.
        const auto& s = ctx.game->strings;
        const bool crash = m_result.config.mode == game::GameMode::CrashCourse;
        const bool net = multiplayer(ctx);
        if (m_popupMenu)
            m_popupGraveyard.push_back(std::move(m_popupMenu));
        m_popupMenu = std::make_unique<ui::Menu>();
        auto& menu = *m_popupMenu;
        menu.popupSounds = true;
        const ui::Box card = popupCard();
        auto at = [&](float x, float y, float w) {
            return ui::Box{card.x + x * card.w, card.y + y * card.h, w * card.w, 0.075f * card.h};
        };
        if (m_popup == Popup::Chat) {
            // PUChat (mmPopup::Init: x 0, y 0.99 - the popup line height, 0.75
            // wide): one text field of up to 40 characters, no title, no label.
            const float lineHeight = 0.05f; // MenuManager's popup line height (inferred)
            auto& entry = menu.add<ui::TextEntry>(
                ui::Box{0.0f, (0.99f - lineHeight) * 480.0f, 0.75f * 640.0f, lineHeight * 480.0f}, &m_chatText, 40);
            entry.onCommit = [this, &ctx] {
                // mmPopup::ChatCB: an empty line just closes it.
                const std::string text = m_chatText;
                closePopup();
                if (!text.empty())
                    sendChatMessage(ctx, text);
            };
            menu.setInitialFocus(&entry);
            entry.beginEdit();
            menu.onBack = [this] { closePopup(); };
        } else if (m_popup == Popup::Main) {
            auto& restart = menu.add<ui::TextButton>(
                at(0.0f, 0.125f, 1.0f), crash ? s.get(655, "Restart Lesson") : s.get(464, "Restart Race"),
                [this] {
                    // mmReplayManager's reset flag: the race starts over.
                    closePopup();
                    m_resultsShown = false;
                    if (m_session)
                        m_session->restart();
                });
            // PUMain::RestartRO: no restart in a network game.
            restart.enabled = !net;
            // The in-race option pages (PUOptions, PUAudioOptions,
            // PUControl, PUGraphics) are not ported.
            menu.add<ui::TextButton>(at(0.0f, 0.25f, 1.0f), s.get(466, "Options"), [] {}).enabled = false;
            menu.add<ui::TextButton>(at(0.0f, 0.375f, 1.0f),
                                     crash ? s.get(656, "Back to School") : s.get(468, "Quit to Race Menu"),
                                     [this, &ctx] { quitToMenu(ctx); });
            menu.add<ui::TextButton>(at(0.0f, 0.5f, 1.0f), s.get(469, "Exit to Windows"), [this, &ctx] {
                m_popup = Popup::ConfirmExit;
                buildPopup(ctx);
            });
            auto& resume = menu.add<ui::TextButton>(at(0.5f, 0.9f, 0.5f), s.get(473, "Resume Driving"),
                                                    [this] { closePopup(); });
            menu.setInitialFocus(&resume);
            menu.onBack = [this] { closePopup(); };
        } else {
            auto& yes = menu.add<ui::TextButton>(at(0.2f, 0.7f, 0.2f), s.get(458, "Yes"), [&ctx] { ctx.quit = true; });
            auto& no = menu.add<ui::TextButton>(at(0.6f, 0.7f, 0.2f), s.get(459, "No"), [this, &ctx] {
                m_popup = Popup::Main;
                buildPopup(ctx);
            });
            menu.setInitialFocus(&no);
            (void)yes;
            menu.onBack = [this, &ctx] {
                m_popup = Popup::Main;
                buildPopup(ctx);
            };
        }
    }

    static ui::Box popupCard() { return {0.2f * 640.0f, 0.1f * 480.0f, 0.6f * 640.0f, 0.8f * 480.0f}; }

    void updatePopup(Context& ctx, double dt) {
        if (!m_popupMenu)
            return;
        const render::UiLayout layout = render::computeUiLayout(ctx.device().outputExtent(), ctx.display.uiScale);
        const ui::NavInput nav = m_nav.read(ctx.input, layout, dt);
        ui::UiFrame f{*ctx.overlay, m_ui, m_text, nav, m_time};
        m_popupMenu->update(f); // a button may replace or close it (see m_popupGraveyard)
    }

    void drawPopup(Context& ctx) {
        if (!m_popupMenu)
            return;
        auto& ov = *ctx.overlay;
        ov.begin(ctx.display.uiScale);
        // The popup card (MenuManager::AdjustPopupCard); its shade is inferred.
        // The chat line has none.
        const ui::Box card = popupCard();
        if (m_popup != Popup::Chat)
            ov.rect(card.x, card.y, card.w, card.h, render::packColor(0, 0, 0, 160));
        const ui::NavInput none;
        ui::UiFrame f{ov, m_ui, m_text, none, m_time};
        // No title: PUMain calls PUMenuBase::CreateTitle(0), which adds none,
        // and PUExit only names its menu (UIMenu::AssignName).
        if (m_popup == Popup::ConfirmExit)
            m_text.draw(ov, ui::style::popupFont(), ctx.game->strings.get(457, "Do you want to exit the game?"),
                        card.x + card.w * 0.5f, card.y + 0.2f * card.h, ui::style::kPopupText, ui::Align::Center);
        m_popupMenu->drawContent(f);
        ov.end();
    }

    void loadAi(Context& ctx) {
        ai::Settings settings;
        settings.trafficDensity = m_result.config.trafficDensity;
        settings.pedestrianDensity = m_result.config.pedestrianDensity;
        // mmSingleStunt::LoadEventFile sets the traffic density to the last
        // event's AmbDensity before aiMap::Init reads it.
        if (m_session && !m_session->setup().lessonEvents.empty())
            settings.trafficDensity = m_session->setup().lessonEvents.back().ambientDensity;
        // The SHOW PEDESTRIANS graphics option (mmStatePack +0x64): aiMap::Init
        // creates no pedestrians without it.
        if (!ctx.settings.ini.getBool("Graphics", "ShowPedestrians", true))
            settings.pedestrianDensity = 0.0f;
        // aiMap::Init: no pedestrians in circuit races; the winter models in snow.
        if (m_result.config.mode == game::GameMode::Circuit)
            settings.pedestrianDensity = 0.0f;
        settings.winterPeds = m_result.config.weather == game::Weather::Snow;
        std::string error;
        const city::AiMapConfig* raceMap =
            m_session && m_session->setup().aiMap ? &*m_session->setup().aiMap : nullptr;
        m_ai = ai::World::create(*m_city, ctx.game->vfs, settings, raceMap, &error);
        if (!m_ai) {
            log::warn("race: AI unavailable: {}", error);
            return;
        }
        m_ai->setLightsDeferred(true); // updated after the racers and police
        m_aiRenderer = std::make_unique<game::AiRenderer>(ctx.device(), *m_textures, *m_models, ctx.game->vfs);
        if (m_world) {
            m_trafficBodies = std::make_unique<game::TrafficBodies>(*m_ai, *m_world);
            m_trafficBodies->setWeatherFriction(weatherFriction());
            // aiVehicleActive's impacts, for the ambient cars' sounds.
            m_trafficBodies->setImpactCallback(
                [this](const game::TrafficImpact& e) { m_trafficImpacts.push_back(e); });
            // The rail cars are instances of the level's rooms.
            if (m_cityLevel)
                m_cityLevel->addSource(m_trafficBodies.get());
            m_ai->setProbe([this](const Vec3& from, const Vec3& to, Vec3& at) {
                phys::RayHit hit;
                if (!m_world->wheelProbe(from, to, hit, nullptr, nullptr))
                    return false;
                at = hit.position;
                return true;
            });
            // aiPedestrian's wall probe is dgPhysManager::Collide with the
            // wheels' mask: lvlSDL::CollideProbe's polygons and the objects
            // flagged 0x20. (MM2 keeps a segment cache per pedestrian whose
            // start room is the pedestrian's; here each probe finds its rooms
            // afresh.)
            m_ai->pedestrians().setProbe([this](const Vec3& from, const Vec3& to, Vec3& at) {
                phys::RayHit hit;
                if (!m_world->wheelProbe(from, to, hit, nullptr, nullptr))
                    return false;
                at = hit.position;
                return true;
            });
        }
    }

    // aiMap's load lists the props standing in the city for the pedestrians
    // to step round (aiPath / aiIntersection::AddBangersToObsMap; inferred:
    // the race's own props are placed by then too).
    void loadPedestrianProps(Context& ctx) {
        if (!m_ai || !m_bangers)
            return;
        std::map<std::string, float> radii;
        auto modelRadius = [&](const std::string& model) {
            // lvlInstance::GetRadius of an unhit banger: its geometry set's
            // radius over the model's levels of detail.
            const auto it = radii.find(model);
            if (it != radii.end())
                return it->second;
            float radius = 0.0f;
            if (const auto bytes = ctx.game->vfs.readAll("geometry/" + str::lower(model) + ".pkg"))
                if (const auto pkg = asset::parsePkg(*bytes))
                    for (const auto& mesh : pkg->meshes)
                        if (mesh.part.empty())
                            radius = std::max(radius, mesh.radius());
            return radii.emplace(model, radius).first->second;
        };
        std::vector<ai::PedObstacle> props;
        for (const auto& inst : m_bangers->instances()) {
            ai::PedObstacle o;
            o.room = inst.room;
            const Mat34& m = inst.matrix;
            o.position = m.m3;
            if (inst.data) {
                const Vec3& cg = inst.data->cg;
                // aiBanger::Position: the centre of gravity's frame less the
                // data's CG offset.
                o.origin = {((m.m3.x - m.m0.x * cg.x) - m.m1.x * cg.y) - m.m2.x * cg.z,
                            ((m.m3.y - m.m0.y * cg.x) - m.m1.y * cg.y) - m.m2.y * cg.z,
                            ((m.m3.z - m.m0.z * cg.x) - m.m1.z * cg.y) - m.m2.z * cg.z};
                o.yRadius = inst.data->yRadius;
                o.impulseLimit2 = inst.data->impulseLimit2;
                o.drivable = (inst.data->collisionType & 0x20) != 0;
            } else {
                o.origin = m.m3;
            }
            o.modelRadius = modelRadius(inst.model);
            props.push_back(o);
        }
        const game::bangers::BangerSet* bangers = m_bangers.get();
        m_ai->pedestrians().setObstacles(std::move(props), [bangers](std::size_t i) {
            return i < bangers->instances().size() &&
                   bangers->instances()[i].state == game::bangers::BangerSet::State::Unhit;
        });
    }

    void loadEffects(Context& ctx) {
        m_effects.load(ctx.game->vfs);
        // Road decals (city/<map>/decals.pathset, dgRoadDecalInstance).
        if (auto bytes = ctx.game->vfs.readAll("city/" + str::lower(m_city->info.mapName) + "/decals.pathset"))
            if (auto set = city::parsePathSet(*bytes))
                m_roadDecals.load(*set);
        if (m_world) {
            m_bangers = std::make_unique<game::bangers::BangerSet>(*m_bangerData);
            // With the race's own props (race/<city>/<mode><N>.pathset).
            m_bangers->add(game::bangers::placeCityProps(
                *m_city, ctx.game->vfs, *m_bangerData,
                game::bangers::racePropsName(m_result.config.mode, m_result.config.raceIndex)));
            // The props are instances of the level's rooms.
            if (m_cityLevel)
                m_cityLevel->addSource(m_bangers.get());
            m_bangers->setWorld(m_world.get());
        }
        if (m_player && m_vehicle) {
            m_vehicleFx = loadVehicleFx(ctx, m_result.config.vehicle, m_player->model(), *m_vehicle);
            m_player->sim().onImpactCallback = [this](const phys::CarImpact& impact) { playerImpact(impact); };
        }
        const auto w = m_result.config.weather;
        m_weather = std::make_unique<game::fx::Weather>(
            m_effects, w == game::Weather::Rain   ? game::fx::Weather::Kind::Rain
                       : w == game::Weather::Snow ? game::fx::Weather::Kind::Snow
                                                  : game::fx::Weather::Kind::None);
    }

    // Lighting, fog, texture variants and street shading for the race's time
    // and weather.
    void applyEnvironment() {
        m_env = game::makeEnvironment(*m_city, m_result.config.timeOfDay, m_result.config.weather, m_envOptions);
        // mmGame's texture variants: night textures (and darkening) at
        // night, wet (_fa) textures in rain.
        m_textures->setVariants(m_result.config.timeOfDay == game::TimeOfDay::Night,
                                m_result.config.weather == game::Weather::Rain);
        if (m_cityRenderer)
            m_cityRenderer->setEnvironment(m_env);
    }

    // mmGame::InitWeather's light flag: evening, night or fog turn the car
    // lights on.
    bool carLights() const {
        const auto t = m_result.config.timeOfDay;
        return t == game::TimeOfDay::Evening || t == game::TimeOfDay::Night || m_result.config.weather == game::Weather::Fog;
    }

    // Object Detail, reflections and the shadow's ground probe of a car renderer.
    void setupVehicleRenderer(Context& ctx, game::VehicleRenderer& r) {
        r.setDetail(m_detail.objects);
        r.setReflections(ctx.settings.ini.getBool("Graphics", "VehicleReflections", true));
        r.setGroundProbe([this](const Vec3& from, const Vec3& to, Vec3& point, Vec3& normal) {
            phys::RayHit hit;
            if (!m_world || !m_world->probe(from, to, hit))
                return false;
            point = hit.position;
            normal = hit.normal;
            return true;
        });
    }

    // vehCar's effects (tracks, wheel particles, damage and exhaust smoke) for
    // one car: the .vehCarDamage particle fields over the engine smoke
    // defaults, the exhaust pivots of its model.
    std::unique_ptr<game::fx::VehicleEffects> loadVehicleFx(Context& ctx, const std::string& vehicle,
                                                            const asset::VehicleModel& model,
                                                            const game::VehicleRenderer& renderer) {
        game::fx::VehicleFxSetup setup;
        if (!m_sparkColors)
            m_sparkColors = game::fx::SparkLut::load(ctx.game->vfs);
        setup.sparkColors = *m_sparkColors;
        setup.shardTextures = renderer.materialTextures();
        if (auto bytes = ctx.game->vfs.readAll("tune/vehicle/" + vehicle + ".vehcardamage"))
            if (auto f = data::parseDat(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
                f && f->top())
                game::fx::loadBirthRule(*f->top(), setup.smokeRule);
        for (int i = 0; i < 2; ++i)
            if (const auto* pivot = model.pivot(std::format("exhaust{}", i)))
                setup.exhaust[static_cast<std::size_t>(i)] = pivot->origin;
        setup.rain = m_result.config.weather == game::Weather::Rain;
        return std::make_unique<game::fx::VehicleEffects>(m_effects, setup);
    }

    // Car parts that fly off as bangers (tune/banger/<car>_<part>):
    // vehBreakableMgr::Impact ejects the breakable nearest an impact of
    // 10000 or more; a wrecked car loses wheels, hubs and fenders by speed
    // (vehCarModel::EjectOneshot), thrown at 1.3 times its speed. Parts
    // without banger data stay on.
    void breakParts(game::fx::VehicleEffects& fx, game::VehicleRenderer& r, const phys::CarSim& sim,
                    const std::string& vehicle) {
        const auto impacts = fx.takeImpacts();
        if (!m_bangers || !m_bangerData)
            return;
        const Mat34 body = sim.modelMatrix();
        auto eject = [&](const game::VehicleRenderer::Breakable& b, float speed) {
            const auto* data = m_bangerData->find(vehicle + "_" + str::lower(b.part));
            if (!data)
                return false;
            r.detach(b.part);
            m_bangers->ejectPart(*data, vehicle, b.part, r.paintjob(), Mat34::translation(b.pivot) * body, speed,
                                 sim.body.room);
            return true;
        };
        for (const auto& impact : impacts)
            if (impact.value >= 10000.0f)
                if (auto b = r.nearestBreakable(impact.point))
                    eject(*b, 4.0f);
        if (sim.damage.enabled && sim.damage.params.maxDamage <= sim.damage.currentDamage)
            for (const auto& b : r.wreckParts(sim.speedMph(), m_ejectRand))
                eject(b, sim.speed() * 1.3f);
    }

    // vehCar::UpdateTrack lays no tracks in rooms flagged by gizBridge (the
    // opening bridges, not ported): everywhere else they are allowed.
    game::fx::VehicleFxContext vehicleFxContext(const phys::CarSim&) const { return {}; }

    void updateEffects(float dt) {
        // vehCarDamage::Update paints the first impact since the last frame
        // into the body (fxTexelDamage::ApplyDamage, TextelDamageRadius).
        auto paint = [this](game::fx::VehicleEffects& fx, game::VehicleRenderer& r, const phys::CarSim& sim,
                            const std::string& vehicle) {
            if (auto p = fx.takeDamagePoint())
                r.applyDamage(*p, sim.damage.params.textelDamageRadius);
            breakParts(fx, r, sim, vehicle);
        };
        if (m_player && m_vehicleFx) {
            m_vehicleFx->update(dt, m_player->sim(), vehicleFxContext(m_player->sim()));
            if (m_vehicle)
                paint(*m_vehicleFx, *m_vehicle, m_player->sim(), m_player->model().baseName);
        }
        for (auto& o : m_opponents)
            if (o.fx) {
                o.fx->update(dt, o.sim->sim(), vehicleFxContext(o.sim->sim()));
                paint(*o.fx, *o.renderer, o.sim->sim(), o.sim->model().baseName);
            }
        for (auto& c : m_cops) {
            if (c.fx) {
                c.fx->update(dt, c.sim->sim(), vehicleFxContext(c.sim->sim()));
                paint(*c.fx, *c.renderer, c.sim->sim(), c.sim->model().baseName);
            }
            if (c.driver->siren())
                c.sirenAngle = std::fmod(c.sirenAngle + dt * 2.5f * 3.1415927f, 6.2831855f);
        }
        if (m_weather)
            m_weather->update(dt, m_camera.transform);
    }

    void loadAudio(Context& ctx) {
        if (!ctx.mixer)
            return;
        m_ctxMixer = ctx.mixer.get();
        m_bank = std::make_unique<audio::SoundBank>(ctx.game->vfs);
        m_bank->setQuality(ctx.settings.audioHighQuality ? audio::SoundBank::Quality::High
                                                         : audio::SoundBank::Quality::Low);
        audio::game::CarAudioOptions opts;
        opts.city = m_result.config.city;
        opts.weather = surfaceWeather();
        opts.manager = &m_audioSlots; // the tunnel echo state (Object3DManager::setTunnel)
        std::string error;
        m_carAudioOk = m_carAudio.load(ctx.game->vfs, *m_bank, *ctx.mixer, m_result.config.vehicle, opts, &error);
        if (!m_carAudioOk)
            log::warn("race: car audio: {}", error);
        // mmGame::Init: the session's pedestrian voice files (aiPedAudio).
        m_pedAudio.load(ctx.game->vfs, *m_bank, *ctx.mixer, &m_audioSlots);
        // mmPlayer::Init creates the city ambience only with CITY SOUNDS on.
        if (ctx.settings.citySounds)
            m_ambience.load(ctx.game->vfs, *m_bank, *ctx.mixer, m_result.config.city, &m_audioSlots);
        m_rain.load(*m_bank, *ctx.mixer, m_result.config.timeOfDay == game::TimeOfDay::Night);
        // mmPlayer::InitSpeechAudio, with COMMENTARY on: mmSpeechContainer
        // gives cruise and the races an mmRaceSpeech (InitRace), the crash
        // course an mmCCSpeech (InitCC); Cops and Robbers' mmCNRSpeech is not
        // wired (the mode is not playable yet).
        if (ctx.settings.commentary && m_announcer.load(ctx.game->vfs, *m_bank, *ctx.mixer, m_result.config.city)) {
            using audio::game::AnnouncerMode;
            std::optional<AnnouncerMode> mode;
            switch (m_result.config.mode) {
            case game::GameMode::Cruise: mode = AnnouncerMode::Cruise; break;
            case game::GameMode::Blitz: mode = AnnouncerMode::Blitz; break;
            case game::GameMode::Checkpoint: mode = AnnouncerMode::Checkpoint; break;
            case game::GameMode::Circuit: mode = AnnouncerMode::Circuit; break;
            default: break;
            }
            m_announcer.beginSession();
            if (mode)
                m_announcer.beginRace(*mode, m_result.config.vehicle, static_cast<int>(m_result.config.timeOfDay),
                                      static_cast<int>(m_result.config.weather));
            const bool lessons = m_result.config.mode == game::GameMode::CrashCourse;
            m_announcerOk = mode || (lessons && m_announcer.beginCrashCourse(std::max(0, m_result.config.raceIndex)));
        }
        // Impacts reported by the simulation feed the impact sounds.
        m_player->sim().onImpactCallback = [this](const phys::CarImpact& impact) { playerImpact(impact); };
    }

    // vehCarDamage::ApplyImpact for the player's car: AudImpact (the impact
    // sounds), the damage effects, and the game's impact callback
    // (mmPlayer::ImpactCallback), which counts the hits.
    void playerImpact(const phys::CarImpact& impact) {
        // mmMultiCR::ImpactCallback: a hit from another player's car.
        if (m_cr && impact.otherBody)
            for (const auto& [id, rv] : m_remotes)
                if (rv.sim && &rv.sim->sim().body == impact.otherBody)
                    m_crImpacts.push_back({m_crSelf, id, impact.impulse.mag()});
        if (impact.sound)
            m_impacts.push_back({impact.soundStrength, impact.audioId, impact.position});
        if (m_vehicleFx)
            m_vehicleFx->impact(impact, m_player->sim());
        if (impact.damaging)
            ++(impact.otherIsBody ? m_vehicleImpacts : m_objectImpacts);
    }

    audio::game::SurfaceWeather surfaceWeather() const {
        const auto w = m_result.config.weather;
        return w == game::Weather::Snow   ? audio::game::SurfaceWeather::Snow
               : w == game::Weather::Rain ? audio::game::SurfaceWeather::Wet
                                          : audio::game::SurfaceWeather::Dry;
    }

    // Audio inputs shared by every simulated car (engine, gears, tyres).
    audio::game::CarAudioInputs carAudioInputs(const phys::CarSim& sim) const {
        audio::game::CarAudioInputs in;
        in.rpm = sim.engine.rpm;
        in.speed = sim.speed();
        in.gear = sim.trans.getCurrentGear();
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& w = sim.wheels[i];
            auto& wi = in.wheels[i];
            wi.onGround = w.onGround;
            // vehWheel's sliding amount (0 while the tyre grips), as
            // vehSurfaceAudio reads it.
            wi.slip = w.slide;
            wi.surface = w.material ? audio::game::surfaceSoundIndex(w.material->name, w.material->sound) : 0;
            wi.suspensionSpeed = w.suspensionVelocity;
            wi.brakeCoef = w.params.brakeCoef;
        }
        // vehSurfaceAudio::UpdateTireWobble: damage past MedDamage.
        const float damageRange = sim.damage.maxDamage() - sim.damage.medDamage();
        in.tireWobble = damageRange > 0.0f ? (sim.damage.currentDamage - sim.damage.medDamage()) / damageRange : 0.0f;
        in.wheelRadius = sim.wheels[2].radius;
        in.wrecked = sim.damage.wrecked();
        in.velocity = sim.body.ics.frameVelocity;
        in.inTunnel = m_tunnel; // audio flag 0x80: every car's surface sound
        return in;
    }

    void updateAudio(Context& ctx, float dt) {
        if (!m_player)
            return;
        const auto& sim = m_player->sim();
        // mmPlayer::Update: with the player's car in a room flagged 0x02
        // (subterranean) the audio flag 0x80 is set (the tunnel: surface
        // sounds, ambience areas, rain shelter, Aud3DObjectManager::EchoOn).
        // The room is the car's (its ICS position), not the camera's.
        m_tunnel = (levelRoomFlagsAt(sim.body.ics.matrix.m3) & city::LevelRoomFlag::Subterranean) != 0;
        // Aud3DObjectManager::EchoOn(0.5) / EchoOff: every positioned sound's
        // echo follows the same flag.
        m_audioSlots.setTunnel(m_tunnel);
        // MMDMusicManager::UpdateAmbientSFX: the city's ambience segment stops
        // underground (StopSegment(0)) and starts again outside (PlaySegment).
        if (auto* music = ctx.music(); music && m_tunnel != m_ambienceStopped) {
            music->setAmbience(m_tunnel ? std::string_view{} : std::string_view(m_result.config.city));
            m_ambienceStopped = m_tunnel;
        }
        if (m_carAudioOk) {
            audio::game::CarAudioInputs in = carAudioInputs(sim);
            in.throttle = m_lastPedals.accelerator;
            in.brake = m_lastPedals.brake;
            in.impacts = std::move(m_impacts);
            m_impacts.clear();
            in.horn = hornDown(ctx);
            in.transform = m_pose.body;
            // vehSurfaceAudio::UpdateAir: something 3 to 33 m below ("big air";
            // its segments start 3 m down, so a closer surface is skipped).
            phys::RayHit hit;
            const Vec3 at = sim.modelMatrix().m3;
            if (m_world && m_world->probe(at - Vec3{0, 3, 0}, at - Vec3{0, 33, 0}, hit))
                in.groundBelow = at.y - hit.position.y;
            m_carAudio.silenceEngine(m_session && m_session->engineSilenced());
            m_carAudio.update(in, dt);
        }
        // The listener follows the camera.
        ctx.mixer->setListener(m_camera.transform, m_player->sim().body.ics.frameVelocity);
        updateAiAudio(dt);
        updateAmbientAudio(ctx, dt);
        updatePedestrianAudio(dt);
        if (m_announcerOk)
            m_announcer.update(dt); // AudSpeech::Update
        m_ambience.update(m_camera.transform, dt, m_tunnel);
        // mmPlayer::SetCamera sets mmRainAudio's interior flag: on for the
        // hood camera (car view 1) and the dashboard, off for the others.
        const auto view = m_cams.view();
        const bool interior = view == game::PlayerCameras::View::Pov || view == game::PlayerCameras::View::Dash;
        m_rain.update(m_result.config.weather == game::Weather::Rain, interior, m_tunnel, dt);
    }

    void sendLocalState(Context& ctx) {
        const auto& sim = m_player->sim();
        net::VehicleControls controls;
        controls.steering = m_lastPedals.steering;
        controls.throttle = m_lastPedals.accelerator;
        controls.brake = m_lastPedals.brake;
        controls.handbrake = m_lastPedals.handbrake;
        controls.gear = static_cast<std::int8_t>(sim.trans.getCurrentGear());
        std::uint8_t flags = 0;
        if (m_pose.headlights)
            flags |= net::kVehicleHeadlights;
        if (m_pose.brakeLights)
            flags |= net::kVehicleBrakeLights;
        if (sim.damage.wrecked())
            flags |= net::kVehicleWrecked;
        if (hornDown(ctx))
            flags |= net::kVehicleHorn;
        ctx.netGame->submitLocalState(m_pose.body, sim.body.ics.frameVelocity, sim.body.ics.angularVelocity, controls,
                                      sim.damage.damage, flags);
    }

    // mmNetObject: every other player's car is a vehCar of the level (built
    // with the polygonal bound, a vpcop on vpmustang99's tuning, towing its
    // trailer except in multiplayer cruise and Cops and Robbers), declared
    // each frame as a type-3 mover (its room and the neighbours stay active)
    // with its trailer. MM2 drives it with the player's inputs and pulls it
    // toward the received positions (mmNetObject::Predict, Update); OpenMM2
    // places it at its interpolated snapshot as a kinematic body, which the
    // local car collides with as with a wall (deviation), and simulates only
    // the trailer behind it.
    void updateRemoteCars(Context& ctx) {
        if (!multiplayer(ctx) || !m_world)
            return;
        const auto mode = m_result.config.mode;
        const bool towing = mode != game::GameMode::Cruise && mode != game::GameMode::CopsAndRobbers;
        std::vector<std::uint8_t> present;
        for (const auto& rc : ctx.netGame->remoteCars()) {
            if (!rc.hasState)
                continue;
            present.push_back(rc.id);
            RemoteVehicle& rv = m_remotes[rc.id];
            if (rv.base != rc.car.vehicle || rv.color != rc.car.color || !rv.renderer) {
                if (rv.sim)
                    rv.sim->removeFrom(*m_world);
                rv = {};
                rv.base = rc.car.vehicle;
                rv.color = rc.car.color;
                std::string error;
                rv.sim = game::SimVehicle::load(ctx.game->vfs, rv.base, &error, {}, true, towing);
                if (!rv.sim) {
                    log::warn("race: network car {}: {}", rv.base, error);
                    continue;
                }
                auto& sim = rv.sim->sim();
                sim.options.weatherFriction = weatherFriction();
                sim.setPolygonalBound(true); // vehCar::Init(..., true) in mmNetObject::Init
                sim.body.kinematic = true;
                sim.body.resetCollider();
                rv.sim->addTo(*m_world);
                rv.sim->reset(rc.transform);
                rv.renderer = std::make_unique<game::VehicleRenderer>(ctx.device(), *m_textures, *m_models,
                                                                      rv.sim->model(), rv.color);
                setupVehicleRenderer(ctx, *rv.renderer);
                if (const auto* trailer = rv.sim->trailerModel()) {
                    rv.trailer = std::make_unique<game::VehicleRenderer>(ctx.device(), *m_textures, *m_models,
                                                                         *trailer, rv.color, "TRAILER", "TWHL");
                    setupVehicleRenderer(ctx, *rv.trailer);
                }
                // mmNetObject::Init -> vehCar::Init -> vehSiren::vehSiren.
                game::VehicleRenderer::setLightGlowScales(0.2f, 0.6f);
            }
            auto& sim = rv.sim->sim();
            // The snapshot is the model matrix; the body is at the centre of
            // mass (vehCarSim::SetWorldMatrix's offset).
            Mat34 ics = rc.transform;
            ics.m3 = rc.transform.m3 - rc.transform.transformDir(sim.centerOfGravity);
            const bool jumped = sim.body.ics.matrix.m3.dist2(ics.m3) > 20.0f * 20.0f;
            sim.body.place(ics);
            sim.body.ics.linearVelocity = rc.velocity;
            sim.body.ics.angularVelocity = rc.angularVelocity;
            sim.setInputs(rc.controls.throttle, rc.controls.brake, rc.controls.steering, rc.controls.handbrake);
            sim.body.declare(3, 0x1b); // mmNetObject::Update
            if (auto* trailer = rv.sim->trailer()) {
                trailer->body.declare(3, 0x1b);
                if (jumped)
                    trailer->reset(); // respawned: hitched again behind it
            }
        }
        for (auto it = m_remotes.begin(); it != m_remotes.end();) {
            if (std::find(present.begin(), present.end(), it->first) == present.end()) {
                if (it->second.sim)
                    it->second.sim->removeFrom(*m_world);
                it = m_remotes.erase(it);
            } else {
                ++it;
            }
        }
    }

    void drawRemoteCars(Context& ctx, float dt, const game::Camera& camera) {
        if (!multiplayer(ctx))
            return;
        for (const auto& rc : ctx.netGame->remoteCars()) {
            const auto it = m_remotes.find(rc.id);
            if (!rc.hasState || it == m_remotes.end() || !it->second.renderer || !it->second.sim)
                continue;
            RemoteVehicle& rv = it->second;
            game::VehiclePose pose;
            pose.body = rc.transform;
            // Wheels roll with the forward speed (radius from the model).
            const float forward = -rc.velocity.dot(rc.transform.m2);
            for (const auto& w : rv.sim->model().wheels) {
                const auto i = static_cast<std::size_t>(std::clamp(w.index, 0, 5));
                rv.spin[i] -= forward / std::max(w.radius, 0.1f) * dt;
                pose.wheelSpin[i] = rv.spin[i];
                pose.wheelSteer[i] = w.index < 2 ? -rc.controls.steering * 0.5f : 0.0f;
            }
            pose.headlights = (rc.flags & net::kVehicleHeadlights) != 0;
            pose.brakeLights = (rc.flags & net::kVehicleBrakeLights) != 0;
            pose.reverseLights = rc.controls.gear < 0;
            rv.renderer->draw(pose, camera.transform);
            if (rv.trailer)
                rv.trailer->draw(rv.sim->trailerPose(), camera.transform);
        }
    }

    void updatePlayer(Context& ctx, float dt) {
        if (!m_player)
            return;
        phys::PedalInput pedals;
        float keyTarget = 0.0f;
        std::optional<float> analog;      // through FilterGamepadSteering
        std::optional<float> deviceAxis;  // through mmPlayer::FilterSteering
        const controls::Controller controller = m_controlOptions.controller;
        if (!m_flyCamera && m_popup == Popup::None)
            readController(ctx, controller, pedals, keyTarget, analog, deviceAxis);
        // Development aid: constant pedal input "accel,brake,steer,handbrake"
        // (the steering as an analog device's).
        if (const char* dbg = std::getenv("OPENMM2_DEBUG_INPUT")) {
            const auto parts = str::split(dbg, ',');
            auto f = [&](std::size_t i) {
                return i < parts.size() ? static_cast<float>(str::parseDouble(parts[i]).value_or(0.0)) : 0.0f;
            };
            pedals.accelerator = f(0);
            pedals.brake = f(1);
            analog = f(2);
            pedals.handbrake = f(3);
        }
        // mmInput::GetSteering: the keyboard and the gamepad through
        // FilterDiscreteSteering / FilterGamepadSteering, the mouse, joystick
        // and wheel axes through mmPlayer::FilterSteering, with the
        // speed-sensitive parameters mmPlayer::Update sets (from the last
        // frame's speed).
        if (deviceAxis && !analog)
            pedals.steering = m_analogSteering.filter(controller, *deviceAxis, dt);
        else
            pedals.steering = m_steering.filter(analog ? clampf(*analog, -1.0f, 1.0f) : keyTarget, dt);
        m_steering.setSpeed(m_player->sim().speed());
        m_analogSteering.setSpeed(m_player->sim().speed(), m_controlOptions.sensitivity);
        // Countdown: the car is held until "Go!" (and during wreck
        // penalties, and after a wreck or a multiplayer finish), and until
        // the shared start time in multiplayer.
        // mmPlayer +0x2258: after the other endings the car brakes with the
        // wheel turned full left (CarSim applies it for the player); after
        // the water it is left alone.
        const bool over = (m_session && m_session->playerHold() == game::session::PlayerHold::FinishBrake) ||
                          m_crFinished;
        // mmGame::UpdateSteeringBrakes, network games: in a forward gear the
        // throttle is capped by +0x40c (the gold's weight; 1 otherwise).
        if (multiplayer(ctx) && m_player->sim().trans.getCurrentGear() > 0)
            pedals.accelerator = std::clamp(pedals.accelerator, 0.0f, m_throttleCap);
        m_player->sim().raceFinished = over;
        if (over) {
            pedals = {};
            m_player->drive(pedals);
        } else if ((m_session && m_session->playerHeld()) ||
                   (multiplayer(ctx) && ctx.netGame->secondsToStart() > 0.0)) {
            m_player->hold(pedals); // vehCar::SetDrivable(0, 1)
            pedals.brake = 1.0f;
        } else {
            m_player->drive(pedals);
        }
        m_lastPedals = pedals;
        // Falling out of the city is the session's rule
        // (mmGame::DropThruCityHandler below y = -50); this OpenMM2 safety net
        // catches only cities whose geometry lies far below that.
        if (m_player->sim().modelMatrix().m3.y < std::min(-50.0f, m_city->psdl.bounds.min.y) - 30.0f)
            m_player->reset(m_spawn);
    }

    // The driving inputs of the chosen controller (mmInput::SetDefaultConfig's
    // binding set for it; GetThrottleVal / GetBrakesVal / GetHandBrake /
    // GetSteering). The joystick's X and Y (and the wheel's) go through the
    // CONTROLLER DEAD ZONE as mmJoystick::SetDeadZone sets it on DirectInput.
    void readController(Context& ctx, controls::Controller controller, phys::PedalInput& pedals, float& keyTarget,
                        std::optional<float>& analog, std::optional<float>& deviceAxis) {
        using controls::Action;
        using controls::Controller;
        auto& in = ctx.input;
        const float deadZone = m_controlOptions.deadZone;
        auto keyHandbrake = [&] { return m_bindings.down(in, Action::Handbrake) ? 1.0f : 0.0f; };
        switch (controller) {
        case Controller::Keyboard:
        default:
            // A bound key gives 1; Steer Left wins over Steer Right.
            pedals.accelerator = m_bindings.down(in, Action::Throttle) ? 1.0f : 0.0f;
            pedals.brake = m_bindings.down(in, Action::Brakes) ? 1.0f : 0.0f;
            pedals.handbrake = keyHandbrake();
            if (m_bindings.down(in, Action::SteerLeft))
                keyTarget = -1.0f;
            else if (m_bindings.down(in, Action::SteerRight))
                keyTarget = 1.0f;
            // OpenMM2 extra: a connected gamepad drives beside the keyboard
            // (triggers, left stick, South for the handbrake).
            for (const auto& pad : in.gamepads()) {
                using platform::GamepadAxis;
                const auto axis = [&](GamepadAxis a) { return pad.axes[static_cast<std::size_t>(a)]; };
                pedals.accelerator = std::max(pedals.accelerator, axis(GamepadAxis::RightTrigger));
                pedals.brake = std::max(pedals.brake, axis(GamepadAxis::LeftTrigger));
                const float x = controls::applyDeadZone(axis(GamepadAxis::LeftX), deadZone);
                if (x != 0.0f)
                    analog = x;
                if (pad.buttons.test(static_cast<std::size_t>(platform::GamepadButton::South)))
                    pedals.handbrake = 1.0f;
            }
            return;
        case Controller::Mouse: {
            // The cursor's place across the window steers; buttons 1 and 2
            // (left, right) are throttle and brakes; the handbrake stays a key.
            const auto size = ctx.window().size();
            deviceAxis = m_analogSteering.mouseAxis(in.mousePosition().x, static_cast<float>(size.width));
            pedals.accelerator = in.mouseDown(platform::MouseButton::Left) ? 1.0f : 0.0f;
            pedals.brake = in.mouseDown(platform::MouseButton::Right) ? 1.0f : 0.0f;
            pedals.handbrake = keyHandbrake();
            return;
        }
        case Controller::GamePad:
            // The stick through FilterGamepadSteering; buttons 0 and 1
            // throttle and brakes, button 3 the handbrake (South, East and
            // North on an SDL gamepad, inferred from DirectInput's order).
            for (const auto& pad : in.gamepads()) {
                using platform::GamepadAxis;
                using platform::GamepadButton;
                const auto button = [&](GamepadButton b) { return pad.buttons.test(static_cast<std::size_t>(b)); };
                analog = controls::applyDeadZone(pad.axes[static_cast<std::size_t>(GamepadAxis::LeftX)], deadZone);
                pedals.accelerator = button(GamepadButton::South) ? 1.0f : 0.0f;
                pedals.brake = button(GamepadButton::East) ? 1.0f : 0.0f;
                pedals.handbrake = button(GamepadButton::North) ? 1.0f : 0.0f;
                return;
            }
            pedals.handbrake = keyHandbrake();
            return;
        case Controller::Joystick:
        case Controller::Wheel: {
            // X steers; Y forward is the throttle and back the brakes
            // (mmJoystick::GetAxis codes 0x13 / 0x14). The wheel's handbrake
            // is button 0, the joystick's button 5 (when it has one; else
            // the key).
            float x = 0.0f, y = 0.0f;
            bool handbrake = false;
            if (!in.joysticks().empty()) {
                const auto& j = in.joysticks().front();
                x = j.axes.size() > 0 ? j.axes[0] : 0.0f;
                y = j.axes.size() > 1 ? j.axes[1] : 0.0f;
                const std::size_t b = controller == Controller::Wheel ? 0 : 5;
                handbrake = b < j.buttons.size() ? j.buttons[b] : keyHandbrake() != 0.0f;
            } else if (!in.gamepads().empty()) {
                // A pad SDL recognises as a gamepad: its left stick (OpenMM2).
                using platform::GamepadAxis;
                const auto& pad = in.gamepads().front();
                x = pad.axes[static_cast<std::size_t>(GamepadAxis::LeftX)];
                y = pad.axes[static_cast<std::size_t>(GamepadAxis::LeftY)];
                handbrake = keyHandbrake() != 0.0f;
            }
            x = controls::applyDeadZone(x, deadZone);
            y = controls::applyDeadZone(y, deadZone);
            deviceAxis = x;
            pedals.accelerator = y < 0.0f ? -y : 0.0f;
            pedals.brake = y > 0.0f ? y : 0.0f;
            pedals.handbrake = handbrake ? 1.0f : 0.0f;
            return;
        }
        }
    }

    // The horn key (mmGame::UpdateHorn), not in the free camera.
    bool hornDown(Context& ctx) const {
        return !m_flyCamera && m_bindings.down(ctx.input, controls::Action::Horn);
    }

    // mmGame::UpdateGameInput: the discrete in-race keys, handled while the
    // game is paused too.
    void updateGameInput(Context& ctx) {
        using controls::Action;
        const auto& in = ctx.input;
        auto pressed = [&](Action a) { return m_bindings.pressed(in, a); };
        bool viewChanged = false;
        if (m_hud) {
            auto& hud = *m_hud;
            if (pressed(Action::MapToggle)) {
                m_cams.cycleMap();
                viewChanged = true;
            }
            if (pressed(Action::FullScreenMap)) {
                // In single player the full-screen map pauses the game and
                // leaving it resumes (mmReplayManager flags 0x1c / 0x1d).
                if (!multiplayer(ctx))
                    m_paused = m_cams.mapMode() != game::MapMode::FullScreen;
                m_cams.toggleFullScreenMap();
                viewChanged = true;
            }
            hud.options().mapMode = m_cams.mapMode();
            if (pressed(Action::MapZoom)) {
                hud.toggleMapZoom();
                viewChanged = true;
            }
            if (pressed(Action::RotatingMap)) {
                hud.toggleMapRotation();
                viewChanged = true;
            }
            if (pressed(Action::HudToggle)) {
                hud.toggleCluster();
                viewChanged = true;
            }
            if (pressed(Action::OpponentPosition)) {
                hud.toggleOpponentIcons();
                viewChanged = true;
            }
        }
        // While paused, mmGame::UpdatePaused also takes the C and V keys
        // themselves (key events 0x2E and 0x2F), whatever the two camera
        // actions are bound to.
        auto pausedKey = [&](Action a, platform::Key k) {
            return m_paused && m_bindings.key(a) != k && in.keyPressed(k);
        };
        if (pressed(Action::ChangeCamera) || pausedKey(Action::ChangeCamera, platform::Key::C))
            m_cams.toggleCamera();
        // mmViewMgr::SetViewSetting(2), input event 0x0C (Thrill Cam): the
        // XCam, orbiting the car under the keyboard (CameraInput::orbit).
        if (pressed(Action::ThrillCam) || pausedKey(Action::ThrillCam, platform::Key::V))
            m_cams.toggleXCam();
        if (pressed(Action::WideAngle))
            m_cams.toggleWideAngle();
        if (pressed(Action::Dashboard))
            m_cams.toggleDashboard();
        // mmViewMgr::SetViewSetting(9), input event 0x1E: the rear-view mirror.
        if (pressed(Action::RearViewMirror)) {
            m_mirror.toggle();
            m_cams.setViewSetting(game::ViewSetting::Mirror);
        }
        for (const auto& pad : in.gamepads())
            if (pad.pressed.test(static_cast<std::size_t>(platform::GamepadButton::North)))
                m_cams.toggleCamera();
        if (m_player) {
            auto& trans = m_player->sim().trans;
            auto& pedals = m_player->controls();
            if (pressed(Action::Transmission)) {
                // Automatic <-> manual; a reverse taken with the swapped
                // pedals goes back to drive first.
                if (pedals.swapThrottle)
                    trans.setDrive();
                pedals.swapThrottle = false;
                trans.automatic(!trans.isAutomatic);
            }
            if (pressed(Action::ShiftUp) && !trans.isAutomatic)
                trans.upshift();
            if (pressed(Action::ShiftDown) && !trans.isAutomatic)
                trans.downshift();
            if (pressed(Action::Reverse)) {
                // Reverse, or first gear from reverse, in either box.
                pedals.swapThrottle = false;
                trans.setCurrentGear(trans.currentGear != phys::Transmission::kReverse
                                         ? phys::Transmission::kReverse
                                         : phys::Transmission::kFirst);
            }
        }
        // mmSingleRace::UpdateGameInput: the target checkpoint.
        if (m_session && pressed(Action::NextCheckpoint))
            m_session->cycleTarget(true);
        if (m_session && pressed(Action::PrevCheckpoint))
            m_session->cycleTarget(false);
        if (viewChanged)
            saveViewSettings(ctx);
    }

    // The driver the frontend last selected (mmPlayerData), for the view
    // settings it keeps.
    void loadProfile() {
        game::ProfileStore store(game::ProfileStore::defaultDir());
        const std::string name = store.lastUsed();
        if (name.empty())
            return;
        for (auto& p : store.list())
            if (p.name == name) {
                m_profile = std::move(p);
                return;
            }
    }

    // The view settings MM2 keeps per driver (mmPlayerConfig): OpenMM2 keeps
    // them in [HUD] of its settings. Defaults: mmStatePack /
    // mmPlayerConfig::DefaultViewSettings.
    void loadViewSettings(Context& ctx) {
        auto& o = m_hud->options();
        const auto& ini = ctx.settings.ini;
        // mmHudMap::Reset applies the kept map mode (the map and the 3D
        // view's place, not the rest of SetViewSetting).
        m_cams.setMapMode(static_cast<game::MapMode>(std::clamp(ini.getInt("HUD", "MapMode", 0), 0LL, 2LL)));
        o.mapMode = m_cams.mapMode();
        o.rotatingMap = ini.getBool("HUD", "RotatingMap", o.rotatingMap);
        o.zoomedIn = ini.getBool("HUD", "MapZoomIn", o.zoomedIn);
        o.opponentIcons = ini.getBool("HUD", "OpponentIcons", o.opponentIcons);
        o.cluster = ini.getBool("HUD", "Cluster", o.cluster);
        m_hudMapBeforeFull = m_cams.mapMode();
    }

    void saveViewSettings(Context& ctx) {
        const auto& o = m_hud->options();
        auto& ini = ctx.settings.ini;
        // The full-screen map is not kept (mmPlayerConfig keeps the mode it
        // was opened from).
        const auto current = m_cams.mapMode();
        const auto mode = current == game::MapMode::FullScreen ? m_hudMapBeforeFull : current;
        if (current != game::MapMode::FullScreen)
            m_hudMapBeforeFull = current;
        ini.setInt("HUD", "MapMode", static_cast<int>(mode));
        ini.setBool("HUD", "RotatingMap", o.rotatingMap);
        ini.setBool("HUD", "MapZoomIn", o.zoomedIn);
        ini.setBool("HUD", "OpponentIcons", o.opponentIcons);
        ini.setBool("HUD", "Cluster", o.cluster);
        ctx.saveSettings();
    }

    // cityLevel::DrawRooms: no rain while the camera's room is subterranean
    // (0x0A), nor in a landmark room (0x20) when something lies over the
    // camera (dgPhysManager::Collide with flags 0x20 from 100 m above it).
    bool rainVisible(const Vec3& eye) const {
        const int flags = levelRoomFlagsAt(eye);
        if (flags & (city::LevelRoomFlag::Subterranean | city::LevelRoomFlag::Covered))
            return false;
        if ((flags & city::LevelRoomFlag::TerrainInstance) && m_world) {
            phys::RayHit hit;
            if (m_world->wheelProbe(eye + Vec3{0.0f, 100.0f, 0.0f}, eye, hit, nullptr, nullptr))
                return false;
        }
        return true;
    }

    game::CameraTarget cameraTarget() const {
        const auto& sim = m_player->sim();
        game::CameraTarget t;
        // camCarCS tracks vehCarSim's world matrix (the model origin).
        t.matrix = sim.modelMatrix();
        t.angularMomentum = sim.body.ics.angularMomentum; // camTrackCS::UpdateCar's spin test
        t.speed = sim.speed(); // vehCarSim: |velocity . Z|
        t.steering = sim.steering;
        t.throttle = sim.engine.throttle;
        t.handBrake = sim.handBrake;
        t.reverseGear = m_player->reversing();
        for (std::size_t i = 0; i < t.wheels.size(); ++i)
            t.wheels[i] = {sim.wheels[i].onGround, sim.wheels[i].intersection.normal};
        // mmPlayer::Update: the lvlRoomInfo flags of the room the car's model
        // is in (0x02 / 0x08 subterranean, 0x20 a terrain-bound instance).
        t.roomFlags = levelRoomFlagsAt(t.matrix.m3);
        return t;
    }

    // The original's car cameras (TrackCamCS / PovCamCS, ported from MM1).
    void updateCarCamera(Context& ctx, float dt) {
        auto& in = ctx.input;
        using controls::Action;
        // mmInput::GetCamPan: the look keys.
        bool left = m_bindings.down(in, Action::LookLeft), right = m_bindings.down(in, Action::LookRight),
             back = m_bindings.down(in, Action::LookBack), forward = m_bindings.down(in, Action::LookForward);
        for (const auto& pad : in.gamepads()) {
            using platform::GamepadAxis;
            using platform::GamepadButton;
            if (pad.pressed.test(static_cast<std::size_t>(GamepadButton::North)))
                m_cams.toggleCamera();
            const float rx = pad.axes[static_cast<std::size_t>(GamepadAxis::RightX)];
            const float ry = pad.axes[static_cast<std::size_t>(GamepadAxis::RightY)];
            left |= rx < -0.5f;
            right |= rx > 0.5f;
            back |= ry > 0.5f;
            forward |= ry < -0.5f;
        }
        game::CameraInput input;
        input.camPan = game::cameraPanFor(left, right, back, forward);
        m_camPan = input.camPan;
        if (const auto extent = ctx.device().sceneExtent(); extent.height)
            input.aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        // camPolarCS::Update reads these keys itself (the XCam and the orbit
        // camera at a multiplayer finish).
        if (!m_flyCamera && m_popup == Popup::None) {
            using platform::Key;
            input.orbit.azimuthDown = in.keyDown(Key::Delete);
            input.orbit.azimuthUp = in.keyDown(Key::PageDown);
            input.orbit.inclineDown = in.keyDown(Key::End);
            input.orbit.inclineUp = in.keyDown(Key::Home);
            input.orbit.closer = in.keyDown(Key::PageUp);
            input.orbit.farther = in.keyDown(Key::Insert);
            input.orbit.fast = in.keyDown(Key::LShift) || in.keyDown(Key::RShift);
        }
        // The cameras' floor, ceiling and wall probes (camTrackCS::MinMax,
        // Collide; mmPlayer::Update's overhead test): dgPhysManager::Collide
        // with mask 0x20, never hitting the player's car.
        const game::CameraProbe probe = [this](const Vec3& from, const Vec3& to, game::CameraHit& out) {
            phys::RayHit hit;
            if (!m_world->wheelProbe(from, to, hit, &m_player->sim().body, nullptr))
                return false;
            out = {hit.position, hit.normal, hit.t};
            return true;
        };
        m_cams.update(dt, cameraTarget(), probe, input);
        m_cams.apply(m_camera);
        // Development aid: fixed orbit "distance,height,angle" around the car.
        if (const char* orbit = std::getenv("OPENMM2_DEBUG_ORBIT")) {
            const auto parts = str::split(orbit, ',');
            if (parts.size() == 3) {
                auto f = [&](int i) { return static_cast<float>(str::parseDouble(parts[i]).value_or(0.0)); };
                const Mat34 car = m_pose.body;
                const Vec3 eye = car.m3 + Mat34::rotationY(f(2)).transformDir({0, f(1), f(0)});
                m_camera.transform = game::Camera::lookAt(eye, car.m3 + Vec3{0, 1.0f, 0});
            }
        }
    }

    void updateFlyCamera(Context& ctx, float dt) {
        auto& in = ctx.input;
        if (in.mouseDown(platform::MouseButton::Right)) {
            m_yaw -= in.mouseDelta().x * 0.004f;
            m_pitch = clampf(m_pitch - in.mouseDelta().y * 0.004f, -1.5f, 1.5f);
        }
        const float turn = 1.6f * dt;
        if (in.keyDown(platform::Key::Left))
            m_yaw += turn;
        if (in.keyDown(platform::Key::Right))
            m_yaw -= turn;
        if (in.keyDown(platform::Key::PageUp))
            m_pitch = clampf(m_pitch + turn, -1.5f, 1.5f);
        if (in.keyDown(platform::Key::PageDown))
            m_pitch = clampf(m_pitch - turn, -1.5f, 1.5f);
        const Mat34 rot = Mat34::rotationX(m_pitch) * Mat34::rotationY(m_yaw);
        const Vec3 forward = -rot.m2, right = rot.m0;
        float speed = in.keyDown(platform::Key::LShift) ? 120.0f : 30.0f;
        Vec3 move;
        if (in.keyDown(platform::Key::W) || in.keyDown(platform::Key::Up))
            move += forward;
        if (in.keyDown(platform::Key::S) || in.keyDown(platform::Key::Down))
            move -= forward;
        if (in.keyDown(platform::Key::D))
            move += right;
        if (in.keyDown(platform::Key::A))
            move -= right;
        if (in.keyDown(platform::Key::E))
            move += Vec3::yAxis();
        if (in.keyDown(platform::Key::Q))
            move -= Vec3::yAxis();
        m_position += move * (speed * dt);
        m_camera.transform = rot;
        m_camera.transform.m3 = m_position;
    }

    void drawDebugUi() {
        const auto& s = m_cityRenderer->stats();
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.6f);
        ImGui::Begin("World", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::Text("%.1f fps", ImGui::GetIO().Framerate);
        ImGui::Text("pos %.1f %.1f %.1f  room %d", m_position.x, m_position.y, m_position.z, s.cameraRoom);
        ImGui::Text("rooms %d  instances %d  draws %d", s.roomsDrawn, s.instancesDrawn, s.drawCalls);
        if (m_aiRenderer) {
            const auto& a = m_aiRenderer->stats();
            ImGui::Text("traffic %zu (%d drawn)  peds %zu (%d)  signals %d", m_ai->cars().size(), a.cars,
                        m_ai->peds().size(), a.peds, a.signals);
            const ai::AmbientCar* nearest = nullptr;
            for (const auto& c : m_ai->cars())
                if (!nearest || c.transform.m3.dist2(m_camera.position()) < nearest->transform.m3.dist2(m_camera.position()))
                    nearest = &c;
            if (nearest) {
                ImGui::Text("nearest car %s at %.1f %.1f %.1f (%.1f m/s)", nearest->model.c_str(), nearest->transform.m3.x,
                            nearest->transform.m3.y, nearest->transform.m3.z, nearest->speed);
                if (std::getenv("OPENMM2_DEBUG_NEAREST"))
                    for (const auto& p : m_ai->peds())
                        log::info("ped {} {} at {:.1f} {:.1f} {:.1f} state {} anim {} frame {:.1f}", p.typeName, p.variant,
                                  p.transform.m3.x, p.transform.m3.y, p.transform.m3.z, p.state, p.animFile, p.frame);
                if (std::getenv("OPENMM2_DEBUG_NEAREST_CARS"))
                    for (const auto& c : m_ai->cars()) {
                        phys::RayHit hit;
                        const bool ground = m_world->probe(c.transform.m3 + Vec3{0, 30, 0}, c.transform.m3 - Vec3{0, 30, 0}, hit);
                        log::info("car {} at {:.1f} {:.1f} {:.1f}  ground below/above: {:.1f}", c.model, c.transform.m3.x,
                                  c.transform.m3.y, c.transform.m3.z, ground ? hit.position.y : -999.0f);
                    }
            }
        }
        if (m_player) {
            const auto& sim = m_player->sim();
            ImGui::Text("%.1f mph  gear %d  %.0f rpm  damage %.0f%%", sim.speedMph(), sim.trans.getCurrentGear(),
                        sim.engine.rpm, sim.damage.damage * 100.0f);
        }
        int tod = static_cast<int>(m_result.config.timeOfDay), weather = static_cast<int>(m_result.config.weather);
        bool changed = ImGui::Combo("Time", &tod, "Morning\0Noon\0Evening\0Night\0");
        changed |= ImGui::Combo("Weather", &weather, "Clear\0Cloudy\0Fog\0Rain\0");
        if (changed) {
            m_result.config.timeOfDay = static_cast<game::TimeOfDay>(tod);
            m_result.config.weather = static_cast<game::Weather>(weather);
            applyEnvironment();
        }
        ImGui::Checkbox("PVS", &m_detail.usePvs);
        if (ImGui::SliderInt("Object detail", &m_objectDetail, 0, 3))
            m_detail.objects = game::ObjectDetail::forLevel(m_objectDetail);
        ImGui::TextDisabled("Keys as in Options > Controls; F2 free camera, F3 this panel");
        ImGui::End();
    }

    ui::TextureCache m_ui;
    ui::TextRenderer m_text;
    std::string m_loadingImage;
    game::RaceResult m_result;
    State m_state = State::ShowLoading;
    double m_time = 0.0;
    float m_frameDt = 0.0f;

    std::unique_ptr<city::CityData> m_city;
    std::unique_ptr<game::TextureLibrary> m_textures;
    std::unique_ptr<game::ModelLibrary> m_models;
    std::unique_ptr<game::CityRenderer> m_cityRenderer;
    game::Environment m_env;
    game::DetailSettings m_detail;
    game::EnvironmentOptions m_envOptions;
    int m_objectDetail = 3; // the Object Detail option, 0-3
    game::Camera m_camera;
    std::unique_ptr<game::CityLevel> m_cityLevel;
    std::unique_ptr<phys::World> m_world;
    std::unique_ptr<game::SimVehicle> m_player;
    std::unique_ptr<game::VehicleRenderer> m_vehicle;
    std::unique_ptr<game::VehicleRenderer> m_trailer;
    game::VehiclePose m_pose;
    game::VehiclePose m_trailerPose;
    Mat34 m_spawn;
    bool m_flyCamera = std::getenv("OPENMM2_DEBUG_FLY") != nullptr;
    bool m_showDebugOnly = std::getenv("OPENMM2_DEBUG_NOHUD") != nullptr;
    bool m_showDebug = std::getenv("OPENMM2_DEBUG_HUD") != nullptr;
    phys::SteeringFilter m_steering;
    // The player's controls: [Controls] bindings and options (mmInput).
    controls::Bindings m_bindings;
    controls::Options m_controlOptions;
    // The game is paused (asRoot): the full-screen map or the popup in
    // single player.
    bool m_paused = false;
    // The in-race popup (mmPopup).
    enum class Popup : std::uint8_t { None, Main, ConfirmExit, Chat };
    Popup m_popup = Popup::None;
    std::unique_ptr<ui::Menu> m_popupMenu;
    std::vector<std::unique_ptr<ui::Menu>> m_popupGraveyard;
    ui::NavReader m_nav;
    bool m_popupPaused = false;
    float m_camPan = 0.0f; // mmInput::GetCamPan, kept at mmPlayer +0x1D6C
    game::session::MapMode m_hudMapBeforeFull = game::session::MapMode::Off;
    game::PlayerCameras m_cams;
    // The rear-view mirror's camera (camera-props); drawMirror draws it. Its
    // on/off switch and the profile flag are the session's to wire.
    game::RearViewMirror m_mirror;
    std::optional<game::Profile> m_profile; // the driver, for the view settings and rewards
    std::string m_chatText;                  // PUChat's text field
    std::unique_ptr<game::session::CopsAndRobbers> m_cr; // mmMultiCR's rules
    std::vector<game::session::CopsAndRobbers::Impact> m_crImpacts; // the local car's hits on network cars
    int m_crSelf = -1;
    game::session::CrTeam m_crMyTeam = game::session::CrTeam::Robber;
    std::uint32_t m_crRng = 1;  // the places' intersection draws
    float m_crEnd = -1.0f;      // UpdateLimit's wait before the results
    bool m_crFinished = false;
    bool m_regen = false;       // mmPlayer::EnableRegen
    float m_throttleCap = 1.0f; // mmGame +0x40c
    controls::AnalogSteering m_analogSteering; // mmPlayer::FilterSteering
    std::size_t m_chatSeen = 0;              // chat lines already posted on the HUD
    bool m_textInput = false;                // SDL text input on for the chat line
    std::optional<game::Progress> m_progress; // the reward rules (loaded at the first finish)
    const vfs::Vfs* m_vfs = nullptr;
    std::unique_ptr<ai::World> m_ai;

    // Race rules, opponents and HUD (src/game/session).
    std::unique_ptr<game::session::Session> m_session;
    std::unique_ptr<game::session::Hud> m_hud;
    // MM2's positioned-sound slots (Aud3DObjectManager); declared before every
    // sound that uses it so it outlives them.
    audio::game::Object3DManager m_audioSlots;
    std::vector<std::unique_ptr<AmbientAudio>> m_ambientAudio; // by ambient car id
    std::map<std::string, std::optional<audio::game::CreatureVoiceDef>> m_voiceDefs;
    std::string m_voiceCategory;
    int m_voiceFileNum = -1;
    std::vector<game::TrafficImpact> m_trafficImpacts;
    struct Opponent {
        std::size_t sessionIndex = 0; // in Session::opponents() (cars that fail to load are skipped)
        Mat34 spawn;                  // grid place on the ground (session Restart)
        std::unique_ptr<game::SimVehicle> sim;
        std::unique_ptr<game::VehicleRenderer> renderer;
        std::unique_ptr<ai::Opponent> driver;
        std::unique_ptr<audio::game::OpponentCarAudio> audio;
        std::unique_ptr<game::fx::VehicleEffects> fx;
        // Impact sounds since the last audio update (CarSim's impact events).
        std::shared_ptr<std::vector<audio::game::ImpactInput>> impacts =
            std::make_shared<std::vector<audio::game::ImpactInput>>();
    };
    std::vector<Opponent> m_opponents;
    struct Cop {
        std::unique_ptr<game::SimVehicle> sim;
        std::unique_ptr<game::VehicleRenderer> renderer;
        ai::PoliceCar* driver = nullptr; // owned by m_police
        std::unique_ptr<audio::game::OpponentCarAudio> audio;
        std::unique_ptr<game::fx::VehicleEffects> fx;
        std::shared_ptr<std::vector<audio::game::ImpactInput>> impacts =
            std::make_shared<std::vector<audio::game::ImpactInput>>();
        float sirenAngle = 0.0f; // vehSiren::Update: 2.5 pi rad/s while on
    };
    std::unique_ptr<ai::PoliceSquad> m_police;
    std::vector<Cop> m_cops;
    int m_lastImpacts = 0;
    game::session::PlayerState m_playerState;
    int m_vehicleImpacts = 0, m_objectImpacts = 0;
    bool m_resultsShown = false;

    // Props that can be knocked over, and particle effects.
    std::unique_ptr<game::bangers::BangerDataLibrary> m_bangerData;
    std::unique_ptr<game::bangers::BangerSet> m_bangers;
    game::bangers::RoadDecals m_roadDecals;
    std::optional<game::fx::SparkLut> m_sparkColors;
    game::fx::Rand m_ejectRand{0xB4EAu};
    game::fx::EffectLibrary m_effects;
    std::unique_ptr<game::fx::VehicleEffects> m_vehicleFx;
    std::unique_ptr<game::fx::Weather> m_weather;
    game::fx::ParticleRenderer m_cards;
    game::fx::SkidRenderer m_skids;

    // Multiplayer: other players' cars, drawn from the interpolated snapshots.
    struct RemoteVehicle {
        std::string base;
        int color = -1;
        std::unique_ptr<game::SimVehicle> sim; // kinematic body (and its trailer)
        std::unique_ptr<game::VehicleRenderer> renderer, trailer;
        std::array<float, 6> spin{};
    };
    std::map<std::uint8_t, RemoteVehicle> m_remotes;
    bool multiplayer(Context& ctx) const { return m_result.config.multiplayer && ctx.netGame; }
    std::unique_ptr<game::AiRenderer> m_aiRenderer;
    std::unique_ptr<game::TrafficBodies> m_trafficBodies;

    // Sound: the player's car, city ambience and rain (src/audio/game).
    std::unique_ptr<audio::SoundBank> m_bank;
    audio::game::PlayerCarAudio m_carAudio;
    audio::game::CityAmbience m_ambience;
    audio::game::RainAudio m_rain;
    audio::game::PedestrianAudio m_pedAudio;
    std::vector<audio::game::PedestrianSoundInput> m_pedSounds;
    audio::game::Announcer m_announcer;
    bool m_announcerOk = false;
    bool m_carAudioOk = false;
    bool m_tunnel = false; // the audio's tunnel flag (mmPlayer::Update, audio flag 0x80)
    bool m_ambienceStopped = false; // MMDMusicManager +0x53: the ambience segment stopped underground
    audio::Mixer* m_ctxMixer = nullptr;
    std::vector<audio::game::ImpactInput> m_impacts;
    std::map<std::string, audio::game::SoundSlot> m_gameSounds; // the session's sounds by name
    phys::PedalInput m_lastPedals;
    std::unique_ptr<audio::MusicDirector> m_musicDirector;
    bool m_musicFinished = false, m_musicResults = false;
    Vec3 m_position;
    float m_yaw = 0.0f, m_pitch = 0.0f;
};

} // namespace

std::unique_ptr<Screen> makeRaceScreen(Context& ctx, const game::RaceConfig& config) {
    log::info("race: mode {} in {}, vehicle {}", static_cast<int>(config.mode), config.city, config.vehicle);
    return std::make_unique<RaceScreen>(ctx, config);
}

} // namespace mm2::app
