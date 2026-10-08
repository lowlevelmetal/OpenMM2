#pragma once

#include "phys/World.h"

namespace mm2::phys {

// A car's or a trailer's body as the level and dgPhysManager see it: the
// sphere of TrivialCollideInstances, GatherCollidables, CollideProbe and the
// room bookkeeping (vehCar::Update moves the car to the room
// FindRoomId(GetPosition()) returns).
class VehicleBody final : public Body {
public:
    // vehCarModel::GetPosition: one up axis above the centre of mass (the
    // ICS position plus its second row). vehTrailerInstance::GetPosition:
    // the centre of mass itself. Both read the ICS matrix as it stands.
    Vec3 position() const override {
        const Mat34& m = ics.matrix;
        if (!aboveCentreOfMass)
            return m.m3;
        return {m.m3.x + m.m1.x, m.m3.y + m.m1.y, m.m3.z + m.m1.z};
    }
    // lvlInstance::GetRadius: the geometry set's radius (see
    // geometryRadius). Without one, the bound's sphere (OpenMM2's fallback
    // for a body built without its model).
    float radius() const override { return geometryRadius > 0.0f ? geometryRadius : Body::radius(); }

    // lvlInstance::GetGeomSet's radius of the instance's first geometry
    // (vehCarModel "body", vehTrailerInstance "trailer"): the largest
    // distance of a vertex from the model origin over its levels of detail
    // (modGetStatic). Neither vehCarModel::InitBound nor vehTrailer::Init
    // raises it. Set by the owner that has the model.
    float geometryRadius = 0.0f;
    // A car (vehCarModel) rather than a trailer (vehTrailerInstance).
    bool aboveCentreOfMass = true;
};

} // namespace mm2::phys
