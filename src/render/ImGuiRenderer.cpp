#include "render/ImGuiRenderer.h"

#include "render/Device.h"

#include <imgui.h>

#include <algorithm>
#include <vector>

namespace mm2::render {

static_assert(sizeof(ImDrawVert) == sizeof(Vertex2D), "ImDrawVert must match Vertex2D");
static_assert(offsetof(ImDrawVert, pos) == offsetof(Vertex2D, position));
static_assert(offsetof(ImDrawVert, uv) == offsetof(Vertex2D, uv));
static_assert(offsetof(ImDrawVert, col) == offsetof(Vertex2D, color));
static_assert(sizeof(ImDrawIdx) == 2, "renderer assumes 16-bit ImGui indices");

ImGuiRenderer::ImGuiRenderer(Device& device) : m_device(device) {
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "openmm2_rhi";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
}

ImGuiRenderer::~ImGuiRenderer() {
    // Release textures ImGui still owns (font atlas).
    if (ImGui::GetCurrentContext()) {
        for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
            if (tex->RefCount == 1 && tex->TexID != ImTextureID_Invalid) {
                m_device.destroyTexture(TextureHandle{static_cast<std::uint32_t>(tex->TexID)});
                tex->SetTexID(ImTextureID_Invalid);
                tex->SetStatus(ImTextureStatus_Destroyed);
            }
        }
        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererName = nullptr;
        io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    }
}

void ImGuiRenderer::updateTextures(ImDrawData* dd) {
    if (!dd->Textures)
        return;
    for (ImTextureData* tex : *dd->Textures) {
        switch (tex->Status) {
        case ImTextureStatus_WantCreate: {
            std::vector<std::uint8_t> rgba;
            const void* pixels = tex->GetPixels();
            if (tex->Format == ImTextureFormat_Alpha8) {
                rgba.resize(static_cast<std::size_t>(tex->Width) * tex->Height * 4);
                const auto* a = static_cast<const std::uint8_t*>(pixels);
                for (int i = 0; i < tex->Width * tex->Height; ++i) {
                    rgba[static_cast<std::size_t>(i) * 4 + 0] = 255;
                    rgba[static_cast<std::size_t>(i) * 4 + 1] = 255;
                    rgba[static_cast<std::size_t>(i) * 4 + 2] = 255;
                    rgba[static_cast<std::size_t>(i) * 4 + 3] = a[i];
                }
                pixels = rgba.data();
            }
            TextureDesc desc;
            desc.width = static_cast<std::uint32_t>(tex->Width);
            desc.height = static_cast<std::uint32_t>(tex->Height);
            desc.debugName = "imgui";
            const TextureData data{pixels, 0};
            const TextureHandle h = m_device.createTexture(desc, std::span(&data, 1));
            tex->SetTexID(static_cast<ImTextureID>(h.id));
            tex->SetStatus(ImTextureStatus_OK);
            break;
        }
        case ImTextureStatus_WantUpdates: {
            const TextureHandle h{static_cast<std::uint32_t>(tex->TexID)};
            std::vector<std::uint8_t> rgba;
            for (const ImTextureRect& r : tex->Updates) {
                const void* src = tex->GetPixelsAt(r.x, r.y);
                std::uint32_t pitch = static_cast<std::uint32_t>(tex->GetPitch());
                if (tex->Format == ImTextureFormat_Alpha8) {
                    rgba.assign(static_cast<std::size_t>(r.w) * r.h * 4, 255);
                    for (int y = 0; y < r.h; ++y) {
                        const auto* row = static_cast<const std::uint8_t*>(tex->GetPixelsAt(r.x, r.y + y));
                        for (int x = 0; x < r.w; ++x)
                            rgba[(static_cast<std::size_t>(y) * r.w + x) * 4 + 3] = row[x];
                    }
                    src = rgba.data();
                    pitch = 0;
                }
                m_device.updateTexture(h, 0, Rect{r.x, r.y, r.w, r.h}, src, pitch);
            }
            tex->SetStatus(ImTextureStatus_OK);
            break;
        }
        case ImTextureStatus_WantDestroy:
            if (tex->UnusedFrames > 0) {
                m_device.destroyTexture(TextureHandle{static_cast<std::uint32_t>(tex->TexID)});
                tex->SetTexID(ImTextureID_Invalid);
                tex->SetStatus(ImTextureStatus_Destroyed);
            }
            break;
        default: break;
        }
    }
}

void ImGuiRenderer::render(ImDrawData* dd) {
    if (!dd)
        return;
    updateTextures(dd);
    const float fbW = dd->DisplaySize.x * dd->FramebufferScale.x;
    const float fbH = dd->DisplaySize.y * dd->FramebufferScale.y;
    if (fbW <= 0 || fbH <= 0 || dd->TotalVtxCount == 0)
        return;

    // Map ImGui display coordinates (points) to output pixels.
    FrameConstants fc;
    const float l = dd->DisplayPos.x, t = dd->DisplayPos.y;
    const float r = l + dd->DisplaySize.x, b = t + dd->DisplaySize.y;
    fc.proj = Mat44::orthographic(l, r, b, t, -1.0f, 1.0f, true);
    m_device.setFrameConstants(fc);
    m_device.setViewport({0, 0, fbW, fbH, 0, 1});

    DrawCall call;
    call.state.vertexFormat = VertexFormat::Overlay;
    call.state.blend = BlendMode::Alpha;
    call.state.cull = CullMode::None;
    call.state.depthTest = false;
    call.state.depthWrite = false;
    call.indexType = IndexType::U16;
    call.constants.flags = DrawFlag::Texture0;
    call.textures[0].sampler = {Filter::Bilinear, AddressMode::Clamp, AddressMode::Clamp};

    const ImVec2 clipOff = dd->DisplayPos;
    const ImVec2 clipScale = dd->FramebufferScale;
    for (const ImDrawList* list : dd->CmdLists) {
        const BufferSlice vb = m_device.uploadTransient(
            BufferKind::Vertex, std::span<const ImDrawVert>(list->VtxBuffer.Data, static_cast<std::size_t>(list->VtxBuffer.Size)));
        const BufferSlice ib = m_device.uploadTransient(
            BufferKind::Index, std::span<const ImDrawIdx>(list->IdxBuffer.Data, static_cast<std::size_t>(list->IdxBuffer.Size)));
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) {
                if (cmd.UserCallback == ImGui::GetPlatformIO().DrawCallback_ResetRenderState)
                    m_device.setFrameConstants(fc);
                else
                    cmd.UserCallback(list, &cmd);
                continue;
            }
            const float x0 = std::max((cmd.ClipRect.x - clipOff.x) * clipScale.x, 0.0f);
            const float y0 = std::max((cmd.ClipRect.y - clipOff.y) * clipScale.y, 0.0f);
            const float x1 = std::min((cmd.ClipRect.z - clipOff.x) * clipScale.x, fbW);
            const float y1 = std::min((cmd.ClipRect.w - clipOff.y) * clipScale.y, fbH);
            if (x1 <= x0 || y1 <= y0)
                continue;
            const Rect scissor{static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0),
                               static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)};
            m_device.setScissor(&scissor);
            call.vertices = vb;
            call.indices = ib;
            call.count = cmd.ElemCount;
            call.first = cmd.IdxOffset;
            call.baseVertex = static_cast<std::int32_t>(cmd.VtxOffset);
            call.textures[0].texture = TextureHandle{static_cast<std::uint32_t>(cmd.GetTexID())};
            m_device.draw(call);
        }
    }
    m_device.setScissor(nullptr);
}

} // namespace mm2::render
