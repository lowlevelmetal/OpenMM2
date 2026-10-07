// A session in the city.
#include "app/Screens.h"
#include "city/CityData.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "asset/VehicleModel.h"
#include "audio/MusicDirector.h"
#include "audio/SoundBank.h"
#include "audio/game/Ambience.h"
#include "audio/game/CarAudio.h"
#include "audio/game/Object3D.h"
#include "data/DatFile.h"
#include "data/TextTables.h"
#include "ai/Opponent.h"
#include "ai/Police.h"
#include "ai/World.h"
#include "game/AiRenderer.h"
#include "game/session/Hud.h"
#include "game/session/Session.h"
#include "game/TrafficBodies.h"
#include "game/bangers/BangerSet.h"
#include "game/bangers/PropPlacement.h"
#include "game/fx/EffectLibrary.h"
#include "game/fx/ParticleRenderer.h"
#include "game/fx/SkidMarks.h"
#include "game/fx/VehicleEffects.h"
#include "game/fx/Weather.h"
#include "game/net/NetGame.h"
#include "game/CamPlayer.h"
#include "game/CityRenderer.h"
#include "game/CityCollision.h"
#include "game/PlayerVehicle.h"
#include "game/VehicleRenderer.h"
#include "phys/World.h"
#include "render/Projection.h"
#include "ui/Text.h"
#include "ui/TextureCache.h"

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
        m_carAudio.stop();
        for (auto& o : m_opponents)
            if (o.audio)
                o.audio->stop();
        for (auto& c : m_cops)
            if (c.audio)
                c.audio->stop();
        m_rain.stop();
        if (m_ctxMixer)
            m_ctxMixer->stopAll();
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
        if (ctx.input.keyPressed(platform::Key::Escape)) {
            ctx.nextScreen = makeFrontendScreen(ctx, m_result);
            return;
        }
        if (multiplayer(ctx)) {
            ctx.netGame->update();
            if (ctx.netGame->takeReturnToLobby() || !ctx.netGame->inSession()) {
                ctx.nextScreen = makeFrontendScreen(ctx, m_result);
                return;
            }
        }
        if (ctx.input.keyPressed(platform::Key::F2))
            m_flyCamera = !m_flyCamera;
        updatePlayer(ctx, static_cast<float>(dt));
        updateAiDrivers(static_cast<float>(dt));
        if (m_player) {
            std::vector<phys::Body*> vehicles{&m_player->sim().body};
            for (auto& o : m_opponents)
                vehicles.push_back(&o.sim->sim().body);
            for (auto& c : m_cops)
                vehicles.push_back(&c.sim->sim().body);
            if (m_bangers)
                m_bangers->update(static_cast<float>(dt), vehicles);
            if (m_trafficBodies)
                m_trafficBodies->beforeStep(vehicles);
        }
        if (m_world)
            m_world->advanceFixed(static_cast<float>(dt));
        if (m_trafficBodies && m_player)
            m_trafficBodies->afterStep(m_player->sim().modelMatrix().m3);
        if (m_ai && m_player) {
            const auto& ics = m_player->sim().body.ics;
            m_ai->update(static_cast<float>(dt), ics.matrix.m3, ics.frameVelocity);
        }
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
        dev.beginScene(clear);

        const auto extent = dev.sceneExtent();
        const float aspect = extent.height ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
        const auto proj = render::computeProjection(m_camera.horizontalFov, aspect, ctx.display.fovMode,
                                                    ctx.display.maxAspect);
        m_camera.farPlane = m_env.farClip;
        render::FrameConstants frame = m_env.frame;
        frame.view = m_camera.view();
        frame.proj = Mat44::perspective(proj.fovY, proj.aspect, m_camera.nearPlane, m_camera.farPlane, true);
        frame.cameraPosition = m_camera.position();
        dev.setFrameConstants(frame);
        const game::Frustum frustum(frame.view * frame.proj);
        m_cityRenderer->draw(m_camera, frustum, m_env, m_detail);
        if (m_ai && m_aiRenderer)
            m_aiRenderer->draw(*m_ai, m_camera, frustum, m_result.config.timeOfDay == game::TimeOfDay::Night,
                               [this](int id) { return m_trafficBodies ? m_trafficBodies->transformOf(id) : nullptr; });
        drawRemoteCars(ctx, m_frameDt);
        const bool night = m_result.config.timeOfDay == game::TimeOfDay::Night;
        if (m_bangers)
            m_bangers->draw(dev, *m_models, *m_textures, m_cards, frustum, m_camera,
                            {1.0f, m_env.fogEnd + 50.0f, night});
        const bool lights = carLights();
        if (m_vehicle && (m_flyCamera || m_cams.display() == game::CarDisplay::Body)) {
            m_pose.headlights = lights;
            m_vehicle->draw(m_pose, m_camera.transform);
            if (m_trailer) {
                m_trailerPose.headlights = lights;
                m_trailer->draw(m_trailerPose, m_camera.transform);
            }
        }
        for (const auto& o : m_opponents) {
            game::VehiclePose pose = o.sim->pose();
            pose.headlights = lights;
            o.renderer->draw(pose, m_camera.transform);
        }
        for (const auto& c : m_cops) {
            game::VehiclePose pose = c.sim->pose();
            pose.headlights = lights;
            pose.siren = c.driver->siren();
            pose.sirenAngle = c.sirenAngle;
            c.renderer->draw(pose, m_camera.transform);
        }
        if (m_vehicleFx)
            m_vehicleFx->draw(dev, *m_textures, m_cards, m_skids, m_camera.transform);
        for (const auto& o : m_opponents)
            if (o.fx)
                o.fx->draw(dev, *m_textures, m_cards, m_skids, m_camera.transform);
        for (const auto& c : m_cops)
            if (c.fx)
                c.fx->draw(dev, *m_textures, m_cards, m_skids, m_camera.transform);
        // cityLevel::DrawRooms draws no rain while the camera is underground
        // (PSDL room flag 0x02).
        const int cameraRoom = m_cityRenderer->stats().cameraRoom;
        const bool underground = cameraRoom > 0 && static_cast<std::size_t>(cameraRoom) < m_city->psdl.rooms.size() &&
                                 (m_city->psdl.rooms[static_cast<std::size_t>(cameraRoom)].flags & city::RoomFlag::Subterranean);
        if (m_weather && !underground)
            m_weather->draw(dev, *m_textures, m_cards, m_camera.transform);
        if (m_hud && m_session && m_player) {
            m_hud->options().dashboard = !m_flyCamera && m_cams.display() == game::CarDisplay::Dash;
            std::vector<game::session::MapBlip> blips;
            for (const auto& o : m_opponents)
                blips.push_back({o.sim->sim().modelMatrix(), game::session::MapBlip::Kind::Opponent});
            for (const auto& c : m_cops)
                blips.push_back({c.sim->sim().modelMatrix(), game::session::MapBlip::Kind::Police});
            m_hud->drawWorld(*m_session, m_camera, m_playerState, m_lastPedals.steering, blips);
            m_hud->drawMap(*m_session, m_playerState, blips, m_frameDt);
        }
        dev.endScene();
    }

    void drawOverlay(Context& ctx) override {
        if (m_state == State::Running && m_hud && m_session && m_player && !m_showDebugOnly) {
            m_hud->drawOverlay(*ctx.overlay, m_text, m_ui, *m_session, m_playerState);
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
        m_textures = std::make_unique<game::TextureLibrary>(ctx.device(), ctx.game->vfs);
        m_models = std::make_unique<game::ModelLibrary>(ctx.device(), ctx.game->vfs);
        m_bangerData = std::make_unique<game::bangers::BangerDataLibrary>(ctx.game->vfs);
        m_cityRenderer = std::make_unique<game::CityRenderer>(ctx.device(), *m_textures, *m_models, *m_city,
                                                              [this](std::string_view n) { return m_bangerData->has(n); });
        m_objectDetail = std::clamp(static_cast<int>(ctx.settings.ini.getInt("Graphics", "ObjectDetail", 3)), 0, 3);
        m_detail.objects = game::ObjectDetail::forLevel(m_objectDetail);
        // Development aid for screenshots: "<timeOfDay 0-3>,<weather 0-3>".
        if (const char* env = std::getenv("OPENMM2_DEBUG_ENV"); env && std::strlen(env) >= 3) {
            m_result.config.timeOfDay = static_cast<game::TimeOfDay>(std::clamp(env[0] - '0', 0, 3));
            m_result.config.weather = static_cast<game::Weather>(std::clamp(env[2] - '0', 0, 4));
        }
        {
            // The Lighting slider (0-1) picks MM2's light quality 0-3; the
            // Visibility slider maps to the Far Clip range 100-1000 m (inferred).
            const double lighting = ctx.settings.ini.getDouble("Graphics", "Lighting", 1.0);
            const double visibility = ctx.settings.ini.getDouble("Graphics", "Visibility", 1.0);
            m_envOptions.lightQuality = static_cast<int>(std::lround(std::clamp(lighting, 0.0, 1.0) * 3.0));
            m_envOptions.farClip = 100.0f + 900.0f * static_cast<float>(std::clamp(visibility, 0.0, 1.0));
        }
        applyEnvironment();
        m_position = m_city->psdl.sphereCenter + Vec3{0, 3, 0};
        m_yaw = 0.0f;
        m_pitch = -0.15f;
        createSession(ctx);
        loadVehicle(ctx); // places the camera behind the car
        loadAi(ctx);
        loadEffects(ctx);
        spawnOpponents(ctx);
        spawnPolice(ctx);
        m_hud = std::make_unique<game::session::Hud>(ctx.device(), *m_textures, *m_models, ctx.game->vfs,
                                                     ctx.game->strings, m_result.config.city, m_result.config.vehicle);
        m_hud->options().metric = ctx.settings.metricUnits;
        m_hud->options().uiScale = ctx.display.uiScale;
        m_hud->preload(&m_ui);
        if (m_session) {
            m_session->start();
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
        // Physics world: the city's static collision and its materials.
        auto collision = game::buildCityCollision(*m_city, ctx.game->vfs,
                                                  [this](std::string_view n) { return m_bangerData->has(n); });
        m_world = std::make_unique<phys::World>(std::move(collision.materials));
        m_world->setStatic(std::move(collision.soup));

        std::string error;
        m_player = game::SimVehicle::load(ctx.game->vfs, m_result.config.vehicle, &error);
        if (!m_player) {
            log::error("race: vehicle '{}': {}", m_result.config.vehicle, error);
            return;
        }
        m_player->sim().options.player = true; // mmPlayer::Update's input overrides
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
        // Drop the spawn point onto the surface below it.
        phys::RayHit hit;
        if (m_world->probe(pos + Vec3{0, 5, 0}, pos - Vec3{0, 30, 0}, hit))
            m_spawn.m3 = hit.position;
        m_player->addTo(*m_world);
        m_player->reset(m_spawn);
        m_pose = m_player->pose();
        std::vector<std::string> missing;
        m_cams.load(ctx.game->vfs, m_result.config.vehicle, &missing);
        if (const auto* info = ctx.game->catalog.vehicle(m_result.config.vehicle))
            m_cams.setVehicleFlags(static_cast<int>(info->flags));
        for (const auto& m : missing)
            log::debug("race: camera file {} missing (engine defaults)", m);
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
        car->addTo(*m_world);
        phys::RayHit hit;
        if (m_world->probe(spawn.m3 + Vec3{0, 5, 0}, spawn.m3 - Vec3{0, 30, 0}, hit))
            spawn.m3 = hit.position;
        car->reset(spawn);
        return car;
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
            opp.fx = loadVehicleFx(ctx, s.vehicle, opp.sim->model());
            if (m_ai) {
                std::string error;
                opp.driver = ai::Opponent::create(m_ai->network(), opp.sim->sim(), s.path, s.params, m_session->laps(),
                                                  1 + static_cast<int>(i), &error, m_world.get());
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
    // trunc(count * cop density) of them, the density being the menu's in
    // cruise, the race table's cop count (0, or 1+ for all) in races, 1 in
    // the crash course. Cops stay for the whole race.
    void spawnPolice(Context& ctx) {
        if (!m_session || !m_world || !m_ai)
            return;
        const auto& posts = m_session->police();
        const auto mode = m_result.config.mode;
        float density = 1.0f;
        if (mode == game::GameMode::Cruise)
            density = m_result.config.copDensity;
        else if (mode == game::GameMode::Blitz || mode == game::GameMode::Circuit ||
                 mode == game::GameMode::Checkpoint)
            density = static_cast<float>(m_session->setup().settings.cops);
        const std::size_t count = ai::PoliceSquad::countForDensity(posts.size(), density);
        std::optional<float> chaseDistance;
        if (m_session->setup().aiMap)
            chaseDistance = m_session->setup().aiMap->copChaseDistance;
        m_police = std::make_unique<ai::PoliceSquad>(m_ai->network());
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
            cop.driver = &m_police->add(cop.sim->sim(), post, 100 + static_cast<int>(m_cops.size()), settings);
            // vpcop paint job 0 is the California livery (vpcop_ca_*), 1 the
            // London one (vpcop_ln_*); picked by city (inferred).
            const int livery = str::iequals(m_result.config.city, "london") ? 1 : 0;
            cop.renderer = std::make_unique<game::VehicleRenderer>(ctx.device(), *m_textures, *m_models,
                                                                    cop.sim->model(), livery);
            setupVehicleRenderer(ctx, *cop.renderer);
            cop.audio = loadAiCarAudio(ctx, p.vehicle, true);
            cop.fx = loadVehicleFx(ctx, p.vehicle, cop.sim->model());
            m_cops.push_back(std::move(cop));
        }
        log::info("race: {} police cars", m_cops.size());
    }

    static ai::TrackedCar trackedCar(const phys::CarSim& sim, int id) {
        const Mat34 m = sim.modelMatrix();
        ai::TrackedCar t;
        t.id = id;
        t.position = m.m3;
        t.forward = -m.m2;
        t.velocity = sim.body.ics.linearVelocity;
        t.halfWidth = sim.body.shape.half.x;
        t.halfLength = sim.body.shape.half.z;
        t.body = &sim.body;
        return t;
    }

    // Opponent and police AI: reads every car, writes the AI cars' inputs.
    void updateAiDrivers(float dt) {
        if (!m_player || (m_opponents.empty() && m_cops.empty()))
            return;
        std::vector<ai::TrackedCar> cars;
        ai::TrackedCar player = trackedCar(m_player->sim(), 0);
        player.isPlayer = true;
        player.suspect = true;
        player.reversing = m_player->sim().trans.getCurrentGear() == -1;
        const int impacts = m_vehicleImpacts + m_objectImpacts;
        player.collided = impacts != m_lastImpacts;
        m_lastImpacts = impacts;
        cars.push_back(player);
        for (const auto& o : m_opponents) {
            ai::TrackedCar t = trackedCar(o.sim->sim(), 1 + static_cast<int>(o.sessionIndex));
            t.suspect = true;
            cars.push_back(t);
        }
        for (const auto& c : m_cops) {
            ai::TrackedCar t = trackedCar(c.sim->sim(), c.driver->selfId());
            t.isPolice = true;
            cars.push_back(t);
        }
        if (m_ai) {
            for (const ai::AmbientCar& c : m_ai->cars()) {
                ai::TrackedCar t;
                t.id = 10000 + c.id;
                t.position = c.transform.m3;
                t.forward = -c.transform.m2;
                t.velocity = c.velocity;
                if (c.data) {
                    t.halfWidth = 0.5f * c.data->width();
                    t.halfLength = 0.5f * c.data->length();
                }
                cars.push_back(t);
            }
        }
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

    // Engine, tyre and siren sounds of the opponents and police, positioned.
    void updateAiAudio(float dt) {
        const Mat34& listener = m_camera.transform;
        auto feed = [&](audio::game::OpponentCarAudio& audio, const phys::CarSim& sim, bool siren) {
            audio::game::CarAudioInputs in = carAudioInputs(sim);
            in.throttle = sim.engine.throttle;
            in.brake = sim.brakes;
            in.transform = sim.modelMatrix();
            in.siren = siren;
            audio.update(in, dt, listener);
        };
        for (auto& o : m_opponents)
            if (o.audio)
                feed(*o.audio, o.sim->sim(), false);
        for (auto& c : m_cops)
            if (c.audio)
                feed(*c.audio, c.sim->sim(), c.driver->siren());
    }

    game::session::PlayerState playerState() const {
        game::session::PlayerState ps;
        const auto& sim = m_player->sim();
        ps.transform = m_pose.body;
        ps.velocity = sim.body.ics.frameVelocity;
        ps.speedMph = sim.speedMph();
        ps.rpm = std::max(sim.engine.rpm, sim.params.engine.idleRPM);
        ps.maxRpm = sim.params.engine.maxRPM;
        ps.gear = sim.trans.getCurrentGear();
        ps.automatic = m_result.config.automatic;
        ps.throttle = m_lastPedals.accelerator;
        ps.damage01 = sim.damage.damage;
        ps.wrecked = sim.damage.wrecked();
        if (m_city->water) {
            const int room = m_cityRenderer->roomAt(ps.transform.m3);
            const auto& rooms = m_city->water->rooms;
            ps.inWater = ps.transform.m3.y < m_city->water->height &&
                         std::find(rooms.begin(), rooms.end(), room) != rooms.end();
        }
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
            s.transform = sim.modelMatrix();
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
        m_session->update(dt, m_playerState, opps, cops);
        // mmPlayer::SetPostRaceCam when the race is over (not in cruise).
        if (phaseBefore != game::session::Phase::PostRace && m_session->phase() == game::session::Phase::PostRace &&
            m_result.config.mode != game::GameMode::Cruise)
            m_cams.startPostRace();
        for (const auto& e : m_session->takeEvents()) {
            using game::session::EventType;
            if (e.type == EventType::HitWater)
                m_cams.startWaterCam();
            if (e.type == EventType::Respawn) {
                m_player->reset(m_session->respawnTransform());
                if (m_vehicleFx)
                    m_vehicleFx->reset(); // vehCar::Reset
                m_cams.reset(cameraTarget());
            } else if (e.type == EventType::Restart) {
                // The race starts over (mmGame::Reset): every car to its start.
                m_player->reset(m_spawn);
                if (m_vehicleFx)
                    m_vehicleFx->reset();
                for (auto& o : m_opponents) {
                    o.sim->reset(o.spawn);
                    if (o.fx)
                        o.fx->reset();
                    if (o.driver)
                        o.driver->reset();
                }
                for (auto& c : m_cops)
                    c.driver->reset();
                m_cams.reset(cameraTarget());
            } else if (e.type == EventType::DamageReset) {
                m_player->sim().damage.reset();
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
            } else if (e.type == EventType::OpponentFinished) {
                for (auto& o : m_opponents)
                    if (o.sessionIndex == static_cast<std::size_t>(e.index) && o.driver)
                        o.driver->finish();
            }
        }
        if (auto* music = ctx.music(); music && m_musicDirector) {
            using game::session::Phase;
            auto& director = *m_musicDirector;
            const Phase phase = m_session->phase();
            if (phase != Phase::Countdown)
                director.raceStarted();
            if (phase == Phase::PostRace && !m_musicFinished) {
                director.finish(); // the race modes stop the music at the finish
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
            ctx.nextScreen = makeFrontendScreen(ctx, m_result);
        }
    }

    void loadAi(Context& ctx) {
        ai::Settings settings;
        settings.trafficDensity = m_result.config.trafficDensity;
        settings.pedestrianDensity = m_result.config.pedestrianDensity;
        std::string error;
        m_ai = ai::World::create(*m_city, ctx.game->vfs, settings, nullptr, &error);
        if (!m_ai) {
            log::warn("race: AI unavailable: {}", error);
            return;
        }
        m_aiRenderer = std::make_unique<game::AiRenderer>(ctx.device(), *m_textures, *m_models, ctx.game->vfs);
        if (m_world)
            m_trafficBodies = std::make_unique<game::TrafficBodies>(*m_ai, *m_world);
    }

    void loadEffects(Context& ctx) {
        m_effects.load(ctx.game->vfs);
        if (m_world) {
            m_bangers = std::make_unique<game::bangers::BangerSet>(*m_bangerData);
            m_bangers->add(game::bangers::placeCityProps(*m_city, ctx.game->vfs, *m_bangerData));
            m_bangers->setWorld(m_world.get());
        }
        if (m_player)
            m_vehicleFx = loadVehicleFx(ctx, m_result.config.vehicle, m_player->model());
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
                                                            const asset::VehicleModel& model) {
        game::fx::VehicleFxSetup setup;
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

    // vehCar::UpdateTrack lays no tracks in rooms flagged by gizBridge (the
    // opening bridges, not ported): everywhere else they are allowed.
    game::fx::VehicleFxContext vehicleFxContext(const phys::CarSim&) const { return {}; }

    void updateEffects(float dt) {
        if (m_player && m_vehicleFx)
            m_vehicleFx->update(dt, m_player->sim(), vehicleFxContext(m_player->sim()));
        for (auto& o : m_opponents)
            if (o.fx)
                o.fx->update(dt, o.sim->sim(), vehicleFxContext(o.sim->sim()));
        for (auto& c : m_cops) {
            if (c.fx)
                c.fx->update(dt, c.sim->sim(), vehicleFxContext(c.sim->sim()));
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
        std::string error;
        m_carAudioOk = m_carAudio.load(ctx.game->vfs, *m_bank, *ctx.mixer, m_result.config.vehicle, opts, &error);
        if (!m_carAudioOk)
            log::warn("race: car audio: {}", error);
        m_ambience.load(ctx.game->vfs, *m_bank, *ctx.mixer, m_result.config.city, &m_audioSlots);
        m_rain.load(*m_bank, *ctx.mixer, m_result.config.timeOfDay == game::TimeOfDay::Night);
        // Impacts reported by the simulation feed the impact sounds.
        m_player->sim().onImpactCallback = [this](const phys::Impact& impact) {
            m_impacts.push_back({audio::game::impactStrength(impact.normal * impact.impulse), 0, impact.point});
            if (impact.speed > 1.0f)
                ++(impact.other ? m_vehicleImpacts : m_objectImpacts);
        };
    }

    audio::game::SurfaceWeather surfaceWeather() const {
        const auto w = m_result.config.weather;
        return w == game::Weather::Snow   ? audio::game::SurfaceWeather::Snow
               : w == game::Weather::Rain ? audio::game::SurfaceWeather::Wet
                                          : audio::game::SurfaceWeather::Dry;
    }

    // Audio inputs shared by every simulated car (engine, gears, tyres).
    static audio::game::CarAudioInputs carAudioInputs(const phys::CarSim& sim) {
        audio::game::CarAudioInputs in;
        in.rpm = sim.engine.rpm;
        in.idleRpm = sim.params.engine.idleRPM;
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
        const float damageRange = sim.damage.maxScaled() - sim.damage.medScaled();
        in.tireWobble = damageRange > 0.0f ? (sim.damage.currentDamage - sim.damage.medScaled()) / damageRange : 0.0f;
        in.wheelRadius = sim.wheels[2].radius;
        in.wrecked = sim.damage.wrecked();
        in.velocity = sim.body.ics.frameVelocity;
        in.inTunnel = false; // TODO: room flags (subterranean)
        return in;
    }

    void updateAudio(Context& ctx, float dt) {
        if (!m_player)
            return;
        const auto& sim = m_player->sim();
        if (m_carAudioOk) {
            audio::game::CarAudioInputs in = carAudioInputs(sim);
            in.throttle = m_lastPedals.accelerator;
            in.brake = m_lastPedals.brake;
            in.impacts = std::move(m_impacts);
            m_impacts.clear();
            in.horn = !m_flyCamera && ctx.input.keyDown(platform::Key::H);
            in.transform = m_pose.body;
            // vehSurfaceAudio::UpdateAir: ground within 33 m below ("big air").
            phys::RayHit hit;
            const Vec3 at = sim.modelMatrix().m3;
            if (m_world && m_world->probe(at, at - Vec3{0, 33, 0}, hit))
                in.groundBelow = at.y - hit.position.y;
            m_carAudio.update(in, dt);
        }
        // The listener follows the camera.
        ctx.mixer->setListener(m_camera.transform, m_player->sim().body.ics.frameVelocity);
        updateAiAudio(dt);
        m_ambience.update(m_camera.transform, dt);
        m_rain.update(m_result.config.weather == game::Weather::Rain, false, false, dt);
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
        if (!m_flyCamera && ctx.input.keyDown(platform::Key::H))
            flags |= net::kVehicleHorn;
        ctx.netGame->submitLocalState(m_pose.body, sim.body.ics.frameVelocity, sim.body.ics.angularVelocity, controls,
                                      sim.damage.damage, flags);
    }

    void drawRemoteCars(Context& ctx, float dt) {
        if (!multiplayer(ctx))
            return;
        for (const auto& rc : ctx.netGame->remoteCars()) {
            if (!rc.hasState)
                continue;
            RemoteVehicle& rv = m_remotes[rc.id];
            if (rv.base != rc.car.vehicle || rv.color != rc.car.color || !rv.renderer) {
                rv = {};
                rv.base = rc.car.vehicle;
                rv.color = rc.car.color;
                auto read = [&](std::string_view path) { return ctx.game->vfs.readAll(path); };
                if (auto model = asset::loadVehicleModel(rv.base, read, nullptr)) {
                    rv.model = std::make_unique<asset::VehicleModel>(std::move(*model));
                    rv.renderer = std::make_unique<game::VehicleRenderer>(ctx.device(), *m_textures, *m_models,
                                                                          *rv.model, rv.color);
                    setupVehicleRenderer(ctx, *rv.renderer);
                }
            }
            if (!rv.renderer)
                continue;
            game::VehiclePose pose;
            pose.body = rc.transform;
            // Wheels roll with the forward speed (radius from the model).
            const float forward = -rc.velocity.dot(rc.transform.m2);
            for (const auto& w : rv.model->wheels) {
                const auto i = static_cast<std::size_t>(std::clamp(w.index, 0, 5));
                rv.spin[i] -= forward / std::max(w.radius, 0.1f) * dt;
                pose.wheelSpin[i] = rv.spin[i];
                pose.wheelSteer[i] = w.index < 2 ? -rc.controls.steering * 0.5f : 0.0f;
            }
            pose.headlights = (rc.flags & net::kVehicleHeadlights) != 0;
            pose.brakeLights = (rc.flags & net::kVehicleBrakeLights) != 0;
            pose.reverseLights = rc.controls.gear < 0;
            rv.renderer->draw(pose, m_camera.transform);
        }
    }

    void updatePlayer(Context& ctx, float dt) {
        if (!m_player)
            return;
        auto& in = ctx.input;
        using platform::Key;
        phys::PedalInput pedals;
        float steerTarget = 0.0f;
        if (!m_flyCamera) {
            pedals.accelerator = (in.keyDown(Key::Up) || in.keyDown(Key::W)) ? 1.0f : 0.0f;
            pedals.brake = (in.keyDown(Key::Down) || in.keyDown(Key::S)) ? 1.0f : 0.0f;
            pedals.handbrake = in.keyDown(Key::Space) ? 1.0f : 0.0f;
            if (in.keyDown(Key::Left) || in.keyDown(Key::A))
                steerTarget -= 1.0f;
            if (in.keyDown(Key::Right) || in.keyDown(Key::D))
                steerTarget += 1.0f;
            for (const auto& pad : in.gamepads()) {
                using platform::GamepadAxis;
                const auto axis = [&](GamepadAxis a) { return pad.axes[static_cast<std::size_t>(a)]; };
                pedals.accelerator = std::max(pedals.accelerator, axis(GamepadAxis::RightTrigger));
                pedals.brake = std::max(pedals.brake, axis(GamepadAxis::LeftTrigger));
                const float x = axis(GamepadAxis::LeftX);
                if (std::abs(x) > 0.15f)
                    steerTarget = x;
                if (pad.buttons.test(static_cast<std::size_t>(platform::GamepadButton::South)))
                    pedals.handbrake = 1.0f;
            }
        }
        // Development aid: constant pedal input "accel,brake,steer,handbrake".
        if (const char* dbg = std::getenv("OPENMM2_DEBUG_INPUT")) {
            const auto parts = str::split(dbg, ',');
            auto f = [&](std::size_t i) {
                return i < parts.size() ? static_cast<float>(str::parseDouble(parts[i]).value_or(0.0)) : 0.0f;
            };
            pedals.accelerator = f(0);
            pedals.brake = f(1);
            steerTarget = f(2);
            pedals.handbrake = f(3);
        }
        // Keyboard steering ramps like a wheel being turned; the rates are
        // inferred (the original's steering sensitivity option scales them).
        const float rate = (std::abs(steerTarget) < std::abs(m_steer) || steerTarget * m_steer < 0) ? 6.0f : 3.0f;
        m_steer += clampf(steerTarget - m_steer, -rate * dt, rate * dt);
        pedals.steering = m_steer;
        // Countdown: the car is held until "Go!" (and during false-start
        // penalties), and until the shared start time in multiplayer.
        // mmPlayer +0x2258: once the race is over the car brakes with the
        // wheel turned full left (CarSim applies it for the player).
        const bool over = m_session && (m_session->phase() == game::session::Phase::PostRace ||
                                        m_session->phase() == game::session::Phase::Done);
        m_player->sim().raceFinished = over;
        if (over) {
            pedals = {};
            m_player->drive(pedals);
        } else if ((m_session && m_session->playerHeld()) ||
                   (multiplayer(ctx) && ctx.netGame->secondsToStart() > 0.0)) {
            pedals.accelerator = 0.0f;
            pedals.brake = 1.0f;
            m_player->hold(pedals.steering);
        } else {
            m_player->drive(pedals);
        }
        m_lastPedals = pedals;
        // R resets the car (OpenMM2 convenience). Falling out of the city is
        // the session's rule (mmGame::DropThruCityHandler below y = -50); this
        // catches only cities whose geometry lies far below that.
        if (in.keyPressed(Key::R) ||
            m_player->sim().modelMatrix().m3.y < std::min(-50.0f, m_city->psdl.bounds.min.y) - 30.0f)
            m_player->reset(m_spawn);
    }

    game::CameraTarget cameraTarget() const {
        const auto& sim = m_player->sim();
        game::CameraTarget t;
        t.matrix = sim.body.ics.matrix;
        t.angularVelocity = sim.body.ics.angularVelocity;
        const Vec3& v = sim.body.ics.frameVelocity;
        t.speed = std::abs((t.matrix.m2.x * v.x + t.matrix.m2.y * v.y) + t.matrix.m2.z * v.z);
        t.steering = sim.steering;
        t.throttle = sim.engine.throttle;
        t.handBrake = sim.handBrake;
        t.reverseGear = m_player->reversing();
        for (std::size_t i = 0; i < t.wheels.size(); ++i)
            t.wheels[i] = {sim.wheels[i].onGround, sim.wheels[i].intersection.normal};
        return t;
    }

    // The original's car cameras (TrackCamCS / PovCamCS, ported from MM1).
    void updateCarCamera(Context& ctx, float dt) {
        auto& in = ctx.input;
        using platform::Key;
        if (in.keyPressed(Key::C))
            m_cams.toggleCamera();
        if (in.keyPressed(Key::V))
            m_cams.setDashboard(!m_cams.dashboard());
        bool left = in.keyDown(Key::Kp4), right = in.keyDown(Key::Kp6), back = in.keyDown(Key::Kp2),
             forward = in.keyDown(Key::Kp8);
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
        if (const auto extent = ctx.device().sceneExtent(); extent.height)
            input.aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const game::CameraProbe probe = [this](const Vec3& from, const Vec3& to, game::CameraHit& out) {
            phys::RayHit hit;
            if (!m_world->probe(from, to, hit))
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
        ImGui::TextDisabled("Arrows/WASD drive, Space handbrake, C camera, V dash, keypad look, R reset, F2 free cam, F3 panel");
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
    float m_steer = 0.0f;
    game::PlayerCameras m_cams;
    std::unique_ptr<ai::World> m_ai;

    // Race rules, opponents and HUD (src/game/session).
    std::unique_ptr<game::session::Session> m_session;
    std::unique_ptr<game::session::Hud> m_hud;
    // MM2's positioned-sound slots (Aud3DObjectManager); declared before every
    // sound that uses it so it outlives them.
    audio::game::Object3DManager m_audioSlots;
    struct Opponent {
        std::size_t sessionIndex = 0; // in Session::opponents() (cars that fail to load are skipped)
        Mat34 spawn;                  // grid place on the ground (session Restart)
        std::unique_ptr<game::SimVehicle> sim;
        std::unique_ptr<game::VehicleRenderer> renderer;
        std::unique_ptr<ai::Opponent> driver;
        std::unique_ptr<audio::game::OpponentCarAudio> audio;
        std::unique_ptr<game::fx::VehicleEffects> fx;
    };
    std::vector<Opponent> m_opponents;
    struct Cop {
        std::unique_ptr<game::SimVehicle> sim;
        std::unique_ptr<game::VehicleRenderer> renderer;
        ai::PoliceCar* driver = nullptr; // owned by m_police
        std::unique_ptr<audio::game::OpponentCarAudio> audio;
        std::unique_ptr<game::fx::VehicleEffects> fx;
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
    game::fx::EffectLibrary m_effects;
    std::unique_ptr<game::fx::VehicleEffects> m_vehicleFx;
    std::unique_ptr<game::fx::Weather> m_weather;
    game::fx::ParticleRenderer m_cards;
    game::fx::SkidRenderer m_skids;

    // Multiplayer: other players' cars, drawn from the interpolated snapshots.
    struct RemoteVehicle {
        std::string base;
        int color = -1;
        std::unique_ptr<asset::VehicleModel> model;
        std::unique_ptr<game::VehicleRenderer> renderer;
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
    bool m_carAudioOk = false;
    audio::Mixer* m_ctxMixer = nullptr;
    std::vector<audio::game::ImpactInput> m_impacts;
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
