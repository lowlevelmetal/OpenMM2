// mmGame::UpdateSteeringBrakes from Midtown Madness 2, verified against the
// build 3393 code (MM2Recomp).

#include "phys/vehicle/Controls.h"

#include "phys/vehicle/CarSim.h"

namespace mm2::phys {

void ArcadeControls::apply(CarSim& car, const PedalInput& in) {
    // mmInput swaps the pedals while reversing.
    const float throttle = swapThrottle ? in.brake : in.accelerator;
    const float brakes = swapThrottle ? in.accelerator : in.brake;
    car.setInputs(throttle, brakes, in.steering, in.handbrake);
    if (!car.trans.isAutomatic || !autoReverse)
        return;
    // In a forward gear, nearly stopped, brake held and no throttle: reverse.
    if (Transmission::kFirst <= car.trans.currentGear && car.speed() < autoRevSpeed && autoRevLevel < brakes &&
        car.engine.throttle < 0.1f) {
        swapThrottle = true;
        car.trans.setReverse();
        return;
    }
    // In reverse, letting go of the (swapped) throttle returns to drive.
    if (car.trans.currentGear == Transmission::kReverse && car.engine.throttle < autoRevLevel) {
        swapThrottle = false;
        car.trans.setDrive();
    }
}

} // namespace mm2::phys
