#include "net/PropState.h"

namespace mm2::net {

std::size_t propSlotBits(const PropSlot& p) {
    WriteStream s;
    PropSlot copy = p;
    serializePropSlot(s, copy);
    return s.writer().bitCount();
}

} // namespace mm2::net
