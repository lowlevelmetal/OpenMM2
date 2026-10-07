#include "city/Inst.h"

#include "city/Reader.h"

#include <format>

namespace mm2::city {

std::optional<std::vector<Instance>> parseInst(std::span<const std::byte> data, std::string* error) {
    // Records: u16 room, u16 flags, u8 nameLength (0x80 = compact rotY form),
    // name (length includes the NUL), then 5 or 12 floats.
    std::vector<Instance> out;
    detail::Reader r(data);
    while (r.remaining() > 0) {
        const std::size_t start = r.pos();
        Instance inst;
        inst.room = r.u16();
        inst.flags = r.u16();
        const std::uint8_t lenByte = r.u8();
        inst.rotY = (lenByte & 0x80) != 0;
        const std::size_t nameLen = lenByte & 0x7F;
        inst.name = r.fixedString(nameLen);
        if (inst.rotY) {
            // X axis (x, z), then position. Y stays up; Z = X cross Y.
            const float ax = r.f32(), az = r.f32();
            inst.transform.m0 = {ax, 0, az};
            inst.transform.m1 = {0, 1, 0};
            inst.transform.m2 = {-az, 0, ax};
            inst.transform.m3 = r.vec3();
        } else {
            inst.transform.m0 = r.vec3();
            inst.transform.m1 = r.vec3();
            inst.transform.m2 = r.vec3();
            inst.transform.m3 = r.vec3();
        }
        if (!r.ok() || nameLen == 0) {
            if (error)
                *error = std::format("truncated or malformed record at offset {}", start);
            return std::nullopt;
        }
        out.push_back(std::move(inst));
    }
    return out;
}

} // namespace mm2::city
