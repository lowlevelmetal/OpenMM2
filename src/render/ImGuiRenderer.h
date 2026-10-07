#pragma once

#include "render/Types.h"

struct ImDrawData;

namespace mm2::render {

class Device;

// Dear ImGui renderer backend implemented on the RHI, so it works on every
// backend. Supports ImGui 1.92 dynamic textures (font atlas updates) and
// user textures: pass a TextureHandle id as ImTextureID, e.g.
// ImGui::Image(ImTextureID(handle.id), size).
class ImGuiRenderer {
public:
    // The ImGui context must exist. Sets io.BackendRendererName/Flags.
    explicit ImGuiRenderer(Device& device);
    ~ImGuiRenderer();
    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

    // Draws ImGui::GetDrawData() in the current overlay pass.
    void render(ImDrawData* drawData);

private:
    void updateTextures(ImDrawData* drawData);
    Device& m_device;
};

} // namespace mm2::render
