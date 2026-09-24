#pragma once
#include <tatra/types.hpp>
#include <stdexcept>
#include <string_view>

namespace tatra::detail {
inline Error notImplemented(std::string_view task, std::string_view operation) {
    return {ErrorCode::NotImplemented,
        std::string{operation} + " is not implemented; see docs/MVP_TASKS.md (" + std::string{task} + ")."};
}
[[noreturn]] inline void throwNotImplemented(std::string_view task, std::string_view operation) {
    throw std::logic_error{notImplemented(task, operation).message};
}
} // namespace tatra::detail
