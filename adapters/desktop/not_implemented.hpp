#pragma once
#include <tatra/types.hpp>
#include <string_view>

namespace tatra::desktop::detail {
inline Error notImplemented(std::string_view task, std::string_view operation) {
    return {ErrorCode::NotImplemented, std::string{operation} +
        " is not implemented; see docs/MVP_TASKS.md (" + std::string{task} + ")."};
}
} // namespace tatra::desktop::detail
