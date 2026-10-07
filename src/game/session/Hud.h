#pragma once

// In-race HUD: instrument cluster, damage meter, race clock, place / check /
// lap readouts and lap times, messages, the checkpoint arrow, the checkpoint
// stands, opponent and checkpoint icons, the overhead map and the in-car
// dashboard.
//
// Behaviour and layout follow MM2's own classes (MM2Recomp, build 3393):
// mmHUD, mmExternalView with mmLinearGauge / mmSlidingGauge /
// mmGearIndicator / mmSpeedIndicator, mmWPHUD, mmCircuitHUD, mmCollideHUD,
// mmTextNode, mmArrow, mmIcons, mmHudMap, mmDashView with RadialGauge, and
// mmWaypointObject / mmCheckpointInstance for the stands. See the HUD section
// of docs/gamemodes.md.
//
// MM2 draws its 2D HUD in screen pixels (bitmaps 1:1, fonts at a pixel
// height). OpenMM2 lays it out in the 640x480 virtual space and scales it to
// the window, i.e. it shows the game as it looks at 640x480
// (HudOptions::pixelSize changes the reference resolution). Elements MM2
// anchors to the screen edges use the edges of the whole output, so widescreen
// windows keep them in the corners.
//
// The first version was ported from MM1's mmHUD, mmArrow and RadialGauge:
// Open1560 - An Open Source Re-Implementation of Midtown Madness 1 Beta
// Copyright (C) 2020 Brick. GPL-3.0-or-later; OpenMM2 port under the same licence.
//
// Drawing order each frame:
//   scene pass:   world ... hud.drawWorld(camera)   // stands, icons, arrow, dashboard
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

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mm2::game::session {

// tune/<city>.mmhudmap (mmHudMap::FileIO; defaults from the constructor).
struct HudMapParams {
    Vec2 size{0.21f, 0.25f}; // fraction of the screen
    Vec2 pos{0.78f, 0.75f};
    bool zoomIn = false;
    float approachRate = 1.2f;
    float zoomInDist = 577.0f, zoomOutDist = 1195.0f;     // camera height, metres
    float iconScaleMin = 34.0f, iconScaleMax = 52.5f;     // icon size factor
    float zoomInDistFS = 786.0f, zoomOutDistFS = 1580.0f; // full-screen map
    float iconScaleMinFS = 15.0f, iconScaleMaxFS = 18.0f;
    Vec3 oceanColor{0.204f, 0.667f, 0.808f};
};
// Reads the file; Ocean Color is then replaced by the colour mmHudMap::Init
// hard-codes for the city (the file's value is never used).
HudMapParams loadHudMapParams(const vfs::Vfs& vfs, const std::string& city);

// tune/<car>_dash.asnode (mmDashView::FileIO; defaults from the
// constructor and RadialGauge::Init) and the needle pivots.
struct DashParams {
    Vec3 dashPos{0.0f, -0.707f, -1.216f};
    Vec3 roofPos{0.0f, 0.189f, 0.0f};
    Vec3 wheelPos;
    Vec3 dmgOffset, speedOffset, tachOffset;
    Vec3 dmgPivotOffset, speedPivotOffset, tachPivotOffset, wheelPivotOffset, gearPivotOffset;
    float wheelFact = 0.9f;
    float rpmRotMin = -0.907f, rpmRotMax = 7.0f;
    float speedRotMin = -0.907f, speedRotMax = 7.0f;
    float damageRotMin = -0.907f, damageRotMax = 7.0f;
    // Centres of the parts' boxes in geometry/<car>_dash_<part>.mtx
    // (mmDashView::LoadPivotInfo); zero when a file is missing.
    Vec3 dmgPivot, speedPivot, tachPivot, wheelPivot;
};
std::optional<DashParams> loadDashParams(const vfs::Vfs& vfs, const std::string& car);

// Other cars shown on the map and marked by the opponent icons.
struct MapBlip {
    Mat34 transform;
    // Ambient traffic is accepted but not drawn (MM2's map shows only the
    // player, opponents and police). Police should be passed while they
    // chase (mmHudMap::DrawCops).
    enum class Kind : std::uint8_t { Opponent, Police, Ambient, Teammate } kind = Kind::Opponent;
};

// mmHudMap map modes 0-3: "Map Toggle" cycles Off, Small, Split;
// "Full Screen Map" switches to FullScreen and back.
enum class MapMode : std::uint8_t {
    Off,
    Small,      // tune/<city>.mmhudmap Pos/Size, minus 10 pixels
    Split,      // the bottom half of the screen (the 3D view takes the top half in MM2)
    FullScreen, // the whole screen (the 3D view becomes a picture-in-picture in MM2)
};

struct HudOptions {
    bool visible = true;              // "HUD Toggle" (mmHUD::Toggle): clock, map and dashboard stay
    MapMode mapMode = MapMode::Small; // MM2 starts new players with the map off (mmStatePack)
    bool rotatingMap = true;          // "Rotating Map" (on in mmStatePack)
    bool zoomedIn = false;            // "Map Zoom" (tune/<city>.mmhudmap ZoomIn)
    bool opponentIcons = true;        // "Opponent Position" (mmIcons; on for new players)
    bool metric = false;              // km/h instead of mph (OpenMM2 option)
    bool dashboard = false;           // in-car dashboard view active
    // Virtual units per original screen pixel: 1 shows the HUD as the game
    // draws it at 640x480, 0.5 as at 1280x960.
    float pixelSize = 1.0f;
    render::UiScaleMode uiScale = render::UiScaleMode::Fit;
};

// Pure pieces of the HUD logic, exposed for tests.
namespace hud {

// mmHUD::Update: the race clock as "MM:SS:HH" (minutes, seconds, hundredths;
// negative times show zero).
std::string clockText(float seconds);
// GetLocTime: "M:SS:HH", or "  ---  " for no time.
std::string lapTimeText(float seconds);
// mmSpeedIndicator::Draw: hundreds, tens and units of the truncated speed;
// ' ' for leading zeros, which are not drawn.
std::array<char, 3> speedDigits(float speed);
// mmGearIndicator::Draw: the digitac_gear_<x>.tga suffix for a gear
// (-1 reverse, 0 neutral, 1.. forward). Neutral uses the "p" art (which shows N).
std::string gearArt(int gear, bool automatic);
// mmLinearGauge::Draw: lit length of a gauge bitmap (0..length).
int linearGaugeLength(float value, float maxValue, int length);
// mmSlidingGauge::Draw: source offset of the window into a wider bitmap.
int slidingGaugeOffset(float value, float maxValue, int bitmapLength, int window);
// RadialGauge::GetArrowAngle: needle angle, clamped to [rotMin, rotMax].
float gaugeAngle(float value, float floorValue, float maxValue, float rotMin, float rotMax);

// mmArrow::Update: the arrow's matrix in camera space and whether the
// target is behind the camera (yellow paint job); nullopt when the target
// is level with the camera, which keeps the previous colour.
struct ArrowPose {
    Mat34 local;
    std::optional<bool> behind;
};
ArrowPose arrowPose(const Mat34& camera, const Vec3& target);

// mmWaypointObject + mmCheckpointInstance::Draw: world matrix of a
// checkpoint stand (pt_check / pt_finish).
Mat34 standMatrix(const Checkpoint& cp);

// mmHudMap::Cull: the overhead camera (looks down -Y from `height`).
// Rotating: the car's heading points up; otherwise -Z points up.
Mat34 mapCamera(const Mat34& car, float height, bool rotating);
// mmHudMap::DrawIcon: world matrix of a car arrow on the map.
Mat34 mapIconMatrix(const Mat34& car, float iconScale);
// mmHudMap::SetMapMode: the map's virtual-space rectangle (x, y, w, h) in a
// layout; rightHandDrive is vehicle Flags 0x40.
Vec4 mapRect(const render::UiLayout& layout, const HudMapParams& params, const HudOptions& options,
             bool rightHandDrive);
// Linear approach used for the map zoom and icon size.
float approach(float current, float target, float rate, float dt);

// Which parts of the HUD a mode shows (mmHUD::Init, mmSingleStunt::InitHUD).
bool arrowShown(GameMode mode, const LessonEvent* lesson);
bool clockShown(GameMode mode, const LessonEvent* lesson);
bool checkReadoutShown(GameMode mode, const LessonEvent* lesson);

// mmHudMap::IconType: colours of the car arrows on the map.
enum class MapIcon : std::uint8_t {
    Outline = 0,  // black, behind the player's arrow
    Police = 1,   // red
    Teammate = 3, // green (OpenMM2 multiplayer; MM2 colours network players by index)
    Player = 5,   // yellow
    Opponent = 7, // violet (single player)
};
// The icon's colour as 0xAARRGGBB (the table DrawIcon indexes).
std::uint32_t mapIconColor(MapIcon icon);

} // namespace hud

class Hud {
public:
    Hud(render::Device& device, TextureLibrary& textures, ModelLibrary& models, const vfs::Vfs& vfs,
        const Strings& strings, const std::string& city, const std::string& vehicle);

    HudOptions& options() { return m_options; }

    // Loads every model and texture the HUD uses. Call while loading the
    // race, outside a frame (creating GPU resources mid-frame is avoided).
    void preload(ui::TextureCache* art = nullptr);

    // Scene pass, with the world's frame constants set: checkpoint stands,
    // opponent / checkpoint icons, the checkpoint arrow (camera space) and,
    // in dashboard view, the dash. `blips` are the opponents for the icons.
    void drawWorld(const Session& session, const Camera& camera, const PlayerState& player,
                   float steering = 0.0f, std::span<const MapBlip> blips = {});

    // Scene pass, after everything else: the overhead map in its own
    // viewport. Leaves viewport, scissor and frame constants changed.
    void drawMap(const Session& session, const PlayerState& player, std::span<const MapBlip> blips, float dt);

    // Overlay pass, outside overlay.begin()/end().
    void drawOverlay(render::Overlay2D& overlay, ui::TextRenderer& text, ui::TextureCache& art,
                     const Session& session, const PlayerState& player);

    // Virtual-space rectangle of the map (x, y, w, h) for the current options.
    Vec4 mapRect(const render::UiLayout& layout) const;

private:
    void drawStands(const Session& session);
    void drawIcons(const Session& session, const Camera& camera, std::span<const MapBlip> blips);
    void drawArrow(const Session& session, const Camera& camera);
    void drawDash(const Camera& camera, const PlayerState& player, float steering);
    void drawCluster(render::Overlay2D& ov, ui::TextureCache& art, const PlayerState& player, float x,
                     float y);
    void drawClock(render::Overlay2D& ov, ui::TextureCache& art, float seconds, float centerX, float y);
    void drawReadouts(render::Overlay2D& ov, ui::TextRenderer& text, const Session& session);
    void drawMessage(render::Overlay2D& ov, ui::TextRenderer& text, const HudMessage& message, float drop = 0.0f);
    void drawTriangle(const Vec3& a, const Vec3& b, const Vec3& c, std::uint32_t argb);
    void trackLapTimes(const Session& session);
    ui::FontSpec font(std::uint32_t id, const char* fallback) const;
    float px(float pixels) const { return pixels * m_options.pixelSize; }

    render::Device& m_device;
    TextureLibrary& m_textures;
    ModelLibrary& m_models;
    const Strings& m_strings;
    std::string m_city;
    std::string m_vehicle;
    bool m_rightHandDrive = false; // tune/<car>.info Flags 0x40 (British cars)
    HudOptions m_options;
    HudMapParams m_map;
    std::optional<DashParams> m_dash;
    // mmHudMap: current camera height and icon size, approaching the targets.
    float m_mapZoom = 0.0f, m_mapIconScale = 0.0f;
    std::optional<MapMode> m_mapModeApplied; // snaps zoom and icon size on change
    int m_arrowPaint = 0;                    // mmArrow colour state
    std::vector<float> m_lapTimes;           // completed laps (mmCircuitHUD::SetLapTime)
    float m_lastLapSeen = 0.0f;
};

} // namespace mm2::game::session
