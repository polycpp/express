/**
 * @file express.cpp
 * @brief Compiled translation unit for polycpp Express.
 *
 * This file exists to give the library a compiled object.
 * Most of Express is header-only, but this TU ensures the library
 * target has at least one source file for CMake.
 *
 * @since 0.1.0
 */

#include <polycpp/express/express.hpp>

namespace polycpp {
namespace express {

// Library version
const char* version() noexcept {
    return "0.1.0";
}

} // namespace express
} // namespace polycpp
