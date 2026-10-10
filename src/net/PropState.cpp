#include "net/PropState.h"

namespace mm2::net {

std::size_t propSlotBits(const PropSlot& p, std::int32_t previous) {
    WriteStream s;
    PropSlot copy = p;
    serializePropSlot(s, copy, previous);
    return s.writer().bitCount();
}

} // namespace mm2::net
