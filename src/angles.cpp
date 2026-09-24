#include <tatra/angles.hpp>
#include <numbers>

namespace tatra {
double degreesToRadians(double degrees) noexcept {
    return degrees * (std::numbers::pi_v<double> / 180.0);
}
double radiansToDegrees(double radians) noexcept {
    return radians * (180.0 / std::numbers::pi_v<double>);
}
} // namespace tatra
