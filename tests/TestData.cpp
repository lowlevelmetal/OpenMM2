#include "TestData.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "vfs/GameSource.h"

#include <cstdlib>

namespace mm2::test {

const vfs::Vfs* gameData() {
    static const std::unique_ptr<vfs::Vfs> instance = []() -> std::unique_ptr<vfs::Vfs> {
        const char* env = std::getenv("OPENMM2_GAME_DATA");
        if (!env || !*env)
            return nullptr;
        std::string error;
        auto source = vfs::probeGameSource(str::toPath(env), &error);
        if (!source || !source->usable()) {
            log::error("OPENMM2_GAME_DATA: {}", source ? "missing archives" : error);
            return nullptr;
        }
        auto v = std::make_unique<vfs::Vfs>();
        if (!vfs::mountGameSource(*v, *source, &error)) {
            log::error("OPENMM2_GAME_DATA: {}", error);
            return nullptr;
        }
        return v;
    }();
    return instance.get();
}

} // namespace mm2::test
