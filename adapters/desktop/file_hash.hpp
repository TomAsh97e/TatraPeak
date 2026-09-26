#pragma once

#include <filesystem>
#include <string>
#include <tatra/types.hpp>

namespace tatra::desktop::detail {
[[nodiscard]] Result<std::string> sha256File(const std::filesystem::path& path);
}
