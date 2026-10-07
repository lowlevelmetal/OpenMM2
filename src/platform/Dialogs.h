#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::platform {

class Window;

enum class MessageKind { Info, Warning, Error };

// Blocking OS message box. Works before any window exists (parent may be null).
void showMessage(MessageKind kind, std::string_view title, std::string_view message, Window* parent = nullptr);

// Blocking message box with custom buttons. Returns the index of the chosen
// button, or -1 if the box was closed / could not be shown.
int askMessage(MessageKind kind, std::string_view title, std::string_view message,
               std::span<const std::string> buttons, Window* parent = nullptr);

struct FileFilter {
    std::string name;    // "Disc images"
    std::string pattern; // "iso;cue;bin;img" (extensions without dots, ';'-separated, "*" = all)
};

// Called with the chosen path, or std::nullopt if cancelled / failed.
using PathCallback = std::function<void(std::optional<std::filesystem::path>)>;

// Native (portal/GTK/Win32) file and folder pickers. Non-blocking: the
// callback runs later on the main thread, from platform::pollEvents().
void openFileDialog(Window* parent, std::span<const FileFilter> filters, const std::filesystem::path& startIn,
                    PathCallback callback);
void openFolderDialog(Window* parent, const std::filesystem::path& startIn, PathCallback callback);

// Internal: runs completed dialog callbacks. Called by pollEvents().
void dispatchDialogResults();

} // namespace mm2::platform
