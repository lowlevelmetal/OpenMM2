#pragma once

// Tuning of the car cameras, as stored in tune/camera/<name>.camtrackcs and
// .campovcs ("type: a" files, see data/DatFile.h). Field names are MM2's; MM1
// (Open1560 DeclareFields) used the same names with an "m_" prefix. Defaults
// are the values the Angel constructors assign, which is what a camera uses
// when its file is missing (e.g. vpvwcup's far camera, shipped as
// "vpvwcup__far").

#include "core/Math.h"
#include "data/DatFile.h"

#include <optional>
#include <string>
#include <string_view>

namespace mm2::vfs {
class Vfs;
}

namespace mm2::game {

// BaseCamCS
struct BaseCamParams {
    float blendTime = 1.2f;  // BlendTime (unused by the MM1 code)
    float blendGoal = 1.0f;  // BlendGoal: fraction of a transition that is blended
    float cameraFov = 50.0f; // CameraFOV: horizontal degrees for a 4:3 screen
    float cameraNear = 3.0f; // CameraNear
    float cameraFar = 1600.0f; // CameraFar (MM1 ignored it and used the render quality far clip)
};

// AppCamCS: approach (smoothing) of the actual camera towards its goal.
struct AppCamParams {
    int approachOn = 0;   // ApproachOn: smooth (else the camera snaps to its goal)
    int appAppOn = 0;     // AppAppOn: low-pass the approach speeds with AppApp
    float appRot = 0.0f;  // AppRot: rotation approach rate (1/s)
    float appXRot = 0.0f; // AppXRot: pitch approach rate; 0 = AppRot
    float appYPos = 0.0f; // AppYPos: vertical position approach rate
    float appXZPos = 0.0f; // AppXZPos: horizontal position approach rate
    float appApp = 0.0f;  // AppApp: speed filter factor
    float appRotMin = 0.0f; // AppRotMin: ease-in range for rotation (rad)
    float appPosMin = 0.0f; // AppPosMin: ease-in range for position (m)
    float lookAbove = 0.0f; // LookAbove: look-at point height above TrackTo
    Vec3 trackTo{0.0f, 0.8f, 0.0f}; // TrackTo: point on the car (car space) the camera follows
    float maxDist = 0.0f; // MaxDist: keep the camera within [MinDist, MaxDist] of TrackTo (0 = off)
    float minDist = 0.0f; // MinDist
    float lookAt = 0.0f;  // LookAt: blend of the orientation towards looking at TrackTo
};

// TrackCamCS (chase cameras: <car>_near, _far, _ind).
struct TrackCamParams {
    BaseCamParams base{1.2f, 1.0f, 60.0f, 1.0f, 1600.0f};
    AppCamParams app{1, 1, 30.0f, 10.0f, 5.0f, 0.0f, 0.7f, 0.01f, 0.25f, 0.0f, {0.0f, 0.8f, 0.0f}, 11.0f, 7.93f, 1.0f};

    Vec3 offset{0.0f, 1.9f, 7.7f}; // Offset: x side, y height, z distance behind TrackTo
    int collideType = 2;   // CollideType: 2 = pull in front of geometry (MM1). MM2 uses 1, see docs/camera.md
    int minMaxOn = 1;      // MinMaxOn: keep between floor and ceiling
    int trackBreak = 0;    // TrackBreak: 1 = stop tracking while upside down, 2 = always
    float minAppXZPos = 1.8f, maxAppXZPos = 12.0f; // speed-dependent AppXZPos (PreApproach)
    float minSpeed = 5.0f, maxSpeed = 35.0f;
    float appInc = 15.0f, appDec = 10.0f;
    float minHardSteer = 0.8f; // MinHardSteer (declared, unused in MM1)
    float driftDelay = 0.3f;   // DriftDelay
    float vertOffset = 0.6f;   // VertOffset: LookAbove = (Offset.y - 0.8) * VertOffset
    float frontRate = 0.55f, rearRate = 0.5f, flipDelay = 0.5f; // declared, unused in MM1
    int steerOn = 0;           // SteerOn: swing the camera with the steering
    float steerMin = 0.5f, steerAmt = 3.5f;
    // MM2 only (not in MM1): handled as inferred, see docs/camera.md.
    float hillMin = 0.0f, hillMax = 0.0f, hillLerp = 0.0f;
    int reverseOn = 0;
    float revDelay = 0.0f, revOnApp = 0.0f, revOffApp = 0.0f;

    void load(const data::DatNode& node);
};

// PovCamCS (point-of-view cameras: <car>, <car>_dash, <car>_pov).
struct PovCamParams {
    BaseCamParams base{1.2f, 1.0f, 60.0f, 0.1f, 1600.0f}; // AfterLoad forces CameraNear = 0.1
    AppCamParams app{1, 1, 28.0f, 0.0f, 28.0f, 28.0f, 0.7f, 0.0f, 0.0f, 0.0f, {0.0f, 0.8f, 0.0f}, 1.8f, 1.74f, 0.0f};

    Vec3 offset{0.0f, 1.6f, 0.7f}; // Offset: eye position in car space
    float pitch = 0.0f;            // Pitch (rad)
    float povJitterAmp = 0.0f;     // POVJitterAmp (declared, unused in MM1; 0 in every MM2 file)
    // MM2 only: eye position while looking back (inferred, docs/camera.md).
    std::optional<Vec3> reverseOffset;

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
