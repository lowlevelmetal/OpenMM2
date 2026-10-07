// Port of mmGame::UpdateSteeringBrakes from Open1560
// (https://github.com/0x1F9F1/Open1560), GPL-3.0: code/midtown/game.asm.

#include "phys/vehicle/Controls.h"

#include "phys/vehicle/CarSim.h"

namespace mm2::phys {

void ArcadeControls::apply(CarSim& car, const PedalInput& in) {
    const float throttle = swapThrottle ? in.brake : in.accelerator;
    const float brakes = swapThrottle ? in.accelerator : in.brake;
    car.setInputs(throttle, brakes, in.steering, in.handbrake);
    if (car.trans.isAutomatic && car.speed() < autoRevSpeed && brakes > autoRevLevel &&
        static_cast<double>(car.engine.throttle) < 0.1 && autoReverse) {
        swapThrottle = !swapThrottle;
        if (swapThrottle)
            car.trans.setReverse();
        else
            car.trans.setDrive();
    }
}

} // namespace mm2::phys
