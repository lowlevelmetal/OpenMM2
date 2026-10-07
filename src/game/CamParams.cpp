#include "game/CamParams.h"

#include "core/StringUtil.h"
#include "vfs/Vfs.h"

#include <format>

namespace mm2::game {
namespace {

std::optional<data::DatFile> readDat(const vfs::Vfs& vfs, const std::string& path, std::string* error) {
    auto bytes = vfs.readAll(path);
    if (!bytes) {
        if (error)
            *error = std::format("{} not found", path);
        return std::nullopt;
    }
    std::string err;
    auto dat = data::parseDat(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), &err);
    if (!dat || !dat->top()) {
        if (error)
            *error = std::format("{}: {}", path, dat ? "empty" : err);
        return std::nullopt;
    }
    return dat;
}

} // namespace

void loadBaseCamParams(const data::DatNode& n, BaseCamParams& out) {
    n.read("BlendTime", out.blendTime);
    n.read("BlendGoal", out.blendGoal);
    n.read("CameraFOV", out.cameraFov);
    n.read("CameraNear", out.cameraNear);
    n.read("CameraFar", out.cameraFar);
}

void loadAppCamParams(const data::DatNode& n, AppCamParams& out) {
    n.read("ApproachOn", out.approachOn);
    n.read("AppAppOn", out.appAppOn);
    n.read("AppRot", out.appRot);
    n.read("AppXRot", out.appXRot);
    n.read("AppYPos", out.appYPos);
    n.read("AppXZPos", out.appXZPos);
    n.read("AppApp", out.appApp);
    n.read("AppRotMin", out.appRotMin);
    n.read("AppPosMin", out.appPosMin);
    n.read("LookAbove", out.lookAbove);
    n.read("TrackTo", out.trackTo);
    n.read("MaxDist", out.maxDist);
    n.read("MinDist", out.minDist);
    n.read("LookAt", out.lookAt);
}

void TrackCamParams::load(const data::DatNode& n) {
    loadBaseCamParams(n, base);
    loadAppCamParams(n, app);
    n.read("Offset", offset);
    n.read("CollideType", collideType);
    n.read("MinMaxOn", minMaxOn);
    n.read("TrackBreak", trackBreak);
    n.read("MinAppXZPos", minAppXZPos);
    n.read("MaxAppXZPos", maxAppXZPos);
    n.read("MinSpeed", minSpeed);
    n.read("MaxSpeed", maxSpeed);
    n.read("AppInc", appInc);
    n.read("AppDec", appDec);
    n.read("MinHardSteer", minHardSteer);
    n.read("DriftDelay", driftDelay);
    n.read("VertOffset", vertOffset);
    n.read("FrontRate", frontRate);
    n.read("RearRate", rearRate);
    n.read("FlipDelay", flipDelay);
    n.read("SteerOn", steerOn);
    n.read("SteerMin", steerMin);
    n.read("SteerAmt", steerAmt);
    n.read("HillMin", hillMin);
    n.read("HillMax", hillMax);
    n.read("HillLerp", hillLerp);
    n.read("ReverseOn", reverseOn);
    n.read("RevDelay", revDelay);
    n.read("RevOnApp", revOnApp);
    n.read("RevOffApp", revOffApp);
}

void PovCamParams::load(const data::DatNode& n) {
    loadBaseCamParams(n, base);
    loadAppCamParams(n, app);
    n.read("Offset", offset);
    n.read("Pitch", pitch);
    n.read("POVJitterAmp", povJitterAmp);
    if (auto v = n.getVec3("ReverseOffset"))
        reverseOffset = *v;
    // PovCamCS::AfterLoad: CameraNear = 0.1 regardless of the file.
    base.cameraNear = 0.1f;
}

std::optional<TrackCamParams> loadTrackCamParams(const vfs::Vfs& vfs, std::string_view name, std::string* error) {
    auto dat = readDat(vfs, std::format("tune/camera/{}.camtrackcs", str::lower(name)), error);
    if (!dat)
        return std::nullopt;
    TrackCamParams p;
    p.load(*dat->top());
    return p;
}

std::optional<PovCamParams> loadPovCamParams(const vfs::Vfs& vfs, std::string_view name, std::string* error) {
    auto dat = readDat(vfs, std::format("tune/camera/{}.campovcs", str::lower(name)), error);
    if (!dat)
        return std::nullopt;
    PovCamParams p;
    p.load(*dat->top());
    return p;
}

} // namespace mm2::game
