#pragma once

#include <filesystem>
#include <string_view>

namespace trayring::log {

// Logging is off unless enabled; enabling creates the parent directory if needed.
void enable(const std::filesystem::path& file);
[[nodiscard]] bool enabled() noexcept;
void write(std::wstring_view message);

}  // namespace trayring::log
