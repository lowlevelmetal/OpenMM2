#pragma once

// Tuning of the car cameras, as stored in tune/camera/<name>.camtrackcs and
// .campovcs ("type: a" files, see data/DatFile.h). Field names and defaults
// are MM2's (camBaseCS::FileIO, camAppCS::FileIO, camTrackCS::FileIO,
// camPovCS::FileIO and the constructors). A camera whose file is missing
// keeps its constructor defaults, as asNode::Load did (e.g. vpvwcup's far
// camera, shipped as "vpvwcup__far"); AfterLoad only runs after a
// successful load.

#include "core/Math.h"
#include "data/DatFile.h"

#include <optional>
#include <string>
#include <string_view>

namespace mm2::vfs {
class Vfs;
}

namespace mm2::game {

// camBaseCS
struct BaseCamParams {
    float blendTime = 1.2f;    // BlendTime (not read by the game code)
    float blendGoal = 1.0f;    // BlendGoal: fraction of a transition that is blended
    float cameraFov = 50.0f;   // CameraFOV: vertical field of view in degrees (gfxViewport::Perspective)
    float cameraNear = 3.0f;   // CameraNear
    float cameraFar = 1600.0f; // CameraFar: a single global in MM2 (the last file loaded wins)
};

// camAppCS: approach (smoothing) of the actual camera towards its goal.
struct AppCamParams {
    int approachOn = 0;      // ApproachOn: smooth (else the camera snaps to its goal)
    int appAppOn = 0;        // AppAppOn: low-pass the approach speeds with AppApp
    float appRot = 0.5f;     // AppRot: rotation approach rate (1/s)
    float appXRot = 0.5f;    // AppXRot: pitch approach rate; 0 = AppRot
    float appYPos = 0.5f;    // AppYPos: vertical position approach rate
    float appXZPos = 0.0f;   // AppXZPos: horizontal position approach rate
    float appApp = 0.1f;     // AppApp: speed filter factor
    float appRotMin = 0.5f;  // AppRotMin: ease-in range for rotation (rad)
    float appPosMin = 0.5f;  // AppPosMin: ease-in range for position (m)
    float lookAbove = 0.0f;  // LookAbove: look-at point height above TrackTo
    Vec3 trackTo{0.0f, 0.8f, 0.0f}; // TrackTo: point on the car (car space) the camera follows
    float maxDist = 0.0f;    // MaxDist: keep the camera within [MinDist, MaxDist] of TrackTo (0 = off)
    float minDist = 0.0f;    // MinDist
    float lookAt = 0.01f;    // LookAt: blend of the orientation towards looking at TrackTo
};

// camTrackCS (chase cameras: <car>_near, _far, _ind).
struct TrackCamParams {
    TrackCamParams();

    BaseCamParams base;
    AppCamParams app;

    Vec3 offset{0.0f, 1.9f, 7.7f}; // Offset: x side, y height, z distance behind TrackTo
    int collideType = 0;   // CollideType: 1 = keep the near plane out of walls, 2 = MM1-style pull-in
    int minMaxOn = 1;      // MinMaxOn: keep between floor and ceiling
    int trackBreak = 0;    // TrackBreak: 1 = stop tracking while upside down, 2 = always
    float minAppXZPos = 1.8f, maxAppXZPos = 12.0f; // speed-dependent AppXZPos (PreApproach)
    float minSpeed = 5.0f, maxSpeed = 35.0f;
    float appInc = 15.0f, appDec = 10.0f;
    float minHardSteer = 0.8f; // MinHardSteer (not read by MM2)
    float driftDelay = 0.3f;   // DriftDelay (not read by MM2)
    float vertOffset = 0.6f;   // VertOffset: LookAbove = (Offset.y - 0.8) * VertOffset
    float frontRate = 0.55f, rearRate = 0.5f, flipDelay = 0.5f; // not read by MM2
    int steerOn = 0;           // SteerOn, SteerMin, SteerAmt (not read by MM2)
    float steerMin = 0.5f, steerAmt = 3.5f;
    float hillMin = -0.56f, hillMax = 0.56f; // HillMin / HillMax: camera pitch on slopes (rad)
    float hillLerp = 0.05f;    // HillLerp: per-update blend of the ground normal
    int reverseOn = 1;         // ReverseOn: 1 = swing to the front when reversing, -1 = always in front
    float revDelay = 2.0f;     // RevDelay (not read by MM2: the delay is a fixed 2 s)
    float revOnApp = 2.0f;     // RevOnApp: swing rate to the front (rad/s)
    float revOffApp = 4.0f;    // RevOffApp: swing rate back behind (rad/s)

    // Reads the fields present in `node`, then camTrackCS::AfterLoad
    // (CameraNear = 0.5 whatever the file says).
    void load(const data::DatNode& node);
};

// camPovCS (point-of-view cameras: <car>, <car>_dash).
struct PovCamParams {
    PovCamParams();

    BaseCamParams base;
    AppCamParams app;

    Vec3 offset{0.0f, 1.6f, 0.7f};         // Offset: eye position in car space
    Vec3 reverseOffset{0.0f, 1.7f, 0.75f}; // ReverseOffset: eye in the reverse mode (see PovCamera)
    float pitch = 0.0f;                    // Pitch (rad)
    float povJitterAmp = 0.0f;             // POVJitterAmp (not read by MM2)

    // Reads the fields present in `node`, then camPovCS::AfterLoad
    // (CameraNear = 0.1 whatever the file says).
    void load(const data::DatNode& node);
};

void loadBaseCamParams(const data::DatNode& node, BaseCamParams& out);
void loadAppCamParams(const data::DatNode& node, AppCamParams& out);

// Reads tune/camera/<name>.camtrackcs / .campovcs. Returns std::nullopt when
// the file is missing or malformed (the caller keeps the defaults, as the
// original did when asNode::Load failed).
std::optional<TrackCamParams> loadTrackCamParams(const vfs::Vfs& vfs, std::string_view name,
                                                 std::string* error = nullptr);
std::optional<PovCamParams> loadPovCamParams(const vfs::Vfs& vfs, std::string_view name,
                                             std::string* error = nullptr);

} // namespace mm2::game
