#include "platform/Dialogs.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "platform/Window.h"

#include <SDL3/SDL.h>

#include <deque>
#include <memory>
#include <mutex>

namespace mm2::platform {
namespace {

SDL_MessageBoxFlags boxFlags(MessageKind kind) {
    switch (kind) {
    case MessageKind::Info: return SDL_MESSAGEBOX_INFORMATION;
    case MessageKind::Warning: return SDL_MESSAGEBOX_WARNING;
    case MessageKind::Error: return SDL_MESSAGEBOX_ERROR;
    }
    return SDL_MESSAGEBOX_INFORMATION;
}

// A pending dialog. SDL requires the filter strings to stay valid until the
// callback runs, so they live here.
struct DialogRequest {
    PathCallback callback;
    std::vector<std::string> names, patterns;
    std::vector<SDL_DialogFileFilter> filters;
    std::string startIn;
};

struct Completed {
    std::shared_ptr<DialogRequest> request;
    std::optional<std::filesystem::path> result;
};

std::mutex g_mutex;
std::deque<Completed> g_completed;
std::vector<std::shared_ptr<DialogRequest>> g_pending; // keeps requests alive

// May be called on any thread.
void SDLCALL onDialogDone(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* raw = static_cast<DialogRequest*>(userdata);
    std::optional<std::filesystem::path> result;
    if (!filelist)
        log::warn("platform: file dialog failed: {}", SDL_GetError());
    else if (filelist[0])
        result = str::toPath(filelist[0]);

    std::lock_guard lock(g_mutex);
    for (auto it = g_pending.begin(); it != g_pending.end(); ++it) {
        if (it->get() == raw) {
            g_completed.push_back({*it, std::move(result)});
            g_pending.erase(it);
            break;
        }
    }
}

std::shared_ptr<DialogRequest> makeRequest(PathCallback cb, const std::filesystem::path& startIn) {
    auto req = std::make_shared<DialogRequest>();
    req->callback = std::move(cb);
    req->startIn = str::fromPath(startIn);
    std::lock_guard lock(g_mutex);
    g_pending.push_back(req);
    return req;
}

} // namespace

void showMessage(MessageKind kind, std::string_view title, std::string_view message, Window* parent) {
    const std::string t(title), m(message);
    if (!SDL_ShowSimpleMessageBox(boxFlags(kind), t.c_str(), m.c_str(), parent ? parent->sdl() : nullptr))
        log::error("{}: {}", t, m); // no GUI available; at least log it
}

int askMessage(MessageKind kind, std::string_view title, std::string_view message,
               std::span<const std::string> buttons, Window* parent) {
    const std::string t(title), m(message);
    std::vector<SDL_MessageBoxButtonData> data;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        SDL_MessageBoxButtonData b{};
        b.buttonID = static_cast<int>(i);
        b.text = buttons[i].c_str();
        if (i == 0)
            b.flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
        if (i + 1 == buttons.size())
            b.flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
        data.push_back(b);
    }
    SDL_MessageBoxData box{};
    box.flags = boxFlags(kind);
    box.window = parent ? parent->sdl() : nullptr;
    box.title = t.c_str();
    box.message = m.c_str();
    box.numbuttons = static_cast<int>(data.size());
    box.buttons = data.data();
    int chosen = -1;
    if (!SDL_ShowMessageBox(&box, &chosen))
        return -1;
    return chosen;
}

void openFileDialog(Window* parent, std::span<const FileFilter> filters, const std::filesystem::path& startIn,
                    PathCallback callback) {
    auto req = makeRequest(std::move(callback), startIn);
    for (const auto& f : filters) {
        req->names.push_back(f.name);
        req->patterns.push_back(f.pattern);
    }
    for (std::size_t i = 0; i < req->names.size(); ++i)
        req->filters.push_back({req->names[i].c_str(), req->patterns[i].c_str()});
    SDL_ShowOpenFileDialog(onDialogDone, req.get(), parent ? parent->sdl() : nullptr,
                           req->filters.empty() ? nullptr : req->filters.data(), static_cast<int>(req->filters.size()),
                           req->startIn.empty() ? nullptr : req->startIn.c_str(), false);
}

void openFolderDialog(Window* parent, const std::filesystem::path& startIn, PathCallback callback) {
    auto req = makeRequest(std::move(callback), startIn);
    SDL_ShowOpenFolderDialog(onDialogDone, req.get(), parent ? parent->sdl() : nullptr,
                             req->startIn.empty() ? nullptr : req->startIn.c_str(), false);
}

void dispatchDialogResults() {
    std::deque<Completed> done;
    {
        std::lock_guard lock(g_mutex);
        done.swap(g_completed);
    }
    for (auto& c : done)
        if (c.request->callback)
            c.request->callback(std::move(c.result));
}

} // namespace mm2::platform
