#pragma once

// In-race HUD: instrument cluster, damage meter, race clock, position /
// checkpoint / lap readouts, messages, the checkpoint arrow and stands, the
// overhead map and the in-car dashboard.
//
// Art and parameters come from the game data (texture/*.tga, geometry/
// hudarrow*.pkg, pt_*.pkg, hudmap_*.pkg, tune/<city>.mmhudmap,
// tune/<car>_dash.asnode). Behaviour is ported from MM1's mmHUD, mmArrow,
// mmHudMap, RadialGauge and mmDashView (Open1560 game.asm) where they exist;
// MM2's layout is inferred (see docs/gamemodes.md).
//
// Open1560 - An Open Source Re-Implementation of Midtown Madness 1 Beta
// Copyright (C) 2020 Brick. GPL-3.0-or-later; OpenMM2 port under the same licence.
//
// Drawing order each frame:
//   scene pass:   world ... hud.drawWorld(camera)   // stands, arrow, dashboard
//                 hud.drawMap(...)                   // last: sets its own viewport
//   overlay pass: hud.drawOverlay(...)               // outside overlay.begin()/end()

#include "game/Camera.h"
#include "game/ModelLibrary.h"
#include "game/Strings.h"
#include "game/TextureLibrary.h"
#include "game/session/Session.h"
#include "game/session/Types.h"
#include "render/Device.h"
#include "render/DisplaySettings.h"
#include "render/Overlay2D.h"
#include "ui/Font.h"
#include "ui/Text.h"
#include "ui/TextureCache.h"
#include "vfs/Vfs.h"

#include <optional>
#include <span>
#include <string>

namespace mm2::game::session {

// tune/<city>.mmhudmap
struct HudMapParams {
    Vec2 size{0.21f, 0.25f}; // fraction of the screen
    Vec2 pos{0.78f, 0.75f};
    bool zoomIn = false;
    float approachRate = 1.2f;
    float zoomInDist = 577.0f, zoomOutDist = 1195.0f;
    float iconScaleMin = 34.0f, iconScaleMax = 52.5f;
    float zoomInDistFS = 786.0f, zoomOutDistFS = 1580.0f;
    float iconScaleMinFS = 15.0f, iconScaleMaxFS = 18.0f;
    Vec3 oceanColor{0.93f, 0.7f, 0.804f};
};
HudMapParams loadHudMapParams(const vfs::Vfs& vfs, const std::string& city);

// tune/<car>_dash.asnode
struct DashParams {
    Vec3 dashPos{0.13f, -0.6f, -0.78f};
    Vec3 roofPos{0.02f, -0.48f, -0.87f};
    Vec3 wheelPos;
    Vec3 dmgOffset, speedOffset, tachOffset;
    Vec3 dmgPivotOffset, speedPivotOffset, tachPivotOffset, wheelPivotOffset, gearPivotOffset;
    float wheelFact = 0.9f;
    float rpmRotMin = 0, rpmRotMax = 3.4f;
    float speedRotMin = 0, speedRotMax = 3.6f;
    float damageRotMin = 0, damageRotMax = 3.2f;
};
std::optional<DashParams> loadDashParams(const vfs::Vfs& vfs, const std::string& car);

// Other cars shown on the map.
struct MapBlip {
    Mat34 transform;
    enum class Kind : std::uint8_t { Opponent, Police, Ambient, Teammate } kind = Kind::Opponent;
};

struct HudOptions {
    bool showMap = true;         // "Map Toggle"
    bool fullScreenMap = false;  // "Full Screen Map"
    bool rotatingMap = true;     // "Rotating Map"
    bool zoomedIn = false;       // "Map Zoom"
    bool showPosition = true;    // "Opponent Position" / HUD toggle
    bool metric = false;         // km/h instead of mph
    bool dashboard = false;      // in-car dashboard view active
    render::UiScaleMode uiScale = render::UiScaleMode::Fit;
};

class Hud {
public:
    Hud(render::Device& device, TextureLibrary& textures, ModelLibrary& models, const vfs::Vfs& vfs,
        const Strings& strings, const std::string& city, const std::string& vehicle);

    HudOptions& options() { return m_options; }

    // Loads every model and texture the HUD uses. Call while loading the
    // race, outside a frame (creating GPU resources mid-frame is avoided).
    void preload(ui::TextureCache* art = nullptr);

    // Scene pass, with the world's frame constants set: checkpoint stands,
    // the checkpoint arrow (camera space) and, in dashboard view, the dash.
    void drawWorld(const Session& session, const Camera& camera, const PlayerState& player, float steering = 0.0f);

    // Scene pass, after everything else: the overhead map in its own
    // viewport. Leaves viewport, scissor and frame constants changed.
    void drawMap(const Session& session, const PlayerState& player, std::span<const MapBlip> blips, float dt);

    // Overlay pass, outside overlay.begin()/end().
    void drawOverlay(render::Overlay2D& overlay, ui::TextRenderer& text, ui::TextureCache& art, const Session& session,
                     const PlayerState& player);

    // Virtual-space rectangle of the map (x, y, w, h) for the current options.
    Vec4 mapRect(const render::UiLayout& layout) const;

private:
    void drawCluster(render::Overlay2D& ov, ui::TextureCache& art, const PlayerState& player, float x, float y);
    void drawClock(render::Overlay2D& ov, ui::TextureCache& art, float seconds, float right, float y);
    void drawDash(const Camera& camera, const PlayerState& player, float steering);
    ui::FontSpec font(std::uint32_t id, const char* fallback) const;

    render::Device& m_device;
    TextureLibrary& m_textures;
    ModelLibrary& m_models;
    const Strings& m_strings;
    std::string m_city;
    std::string m_vehicle;
    HudOptions m_options;
    HudMapParams m_map;
    std::optional<DashParams> m_dash;
    float m_mapZoom = 0.0f; // current view size (approaches the target)
};

} // namespace mm2::game::session
