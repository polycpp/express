cmake_minimum_required(VERSION 3.20)
project(polycpp_express VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# ── Disable ALL upstream tests ────────────────────────────────────────
set(POLYCPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(POLYCPP_PATH_TO_REGEXP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(POLYCPP_QS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(POLYCPP_COOKIE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(POLYCPP_MIME_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(POLYCPP_NEGOTIATE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(POLYCPP_IPADDR_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# ── Core dependency: polycpp ──────────────────────────────────────────
FetchContent_Declare(
    polycpp
    GIT_REPOSITORY git@github.com:enricohuang/polycpp.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(polycpp)

# ── Companion libraries ──────────────────────────────────────────────
# NOTE: FetchContent names MUST match what each library uses internally
# so that transitive dependencies are properly deduplicated.
# negotiate uses "polycpp_mime" internally, so we must use the same name.

FetchContent_Declare(
    polycpp_path_to_regexp
    GIT_REPOSITORY git@github.com:polycpp/path-to-regexp.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    polycpp_qs
    GIT_REPOSITORY git@github.com:polycpp/qs.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    polycpp_cookie
    GIT_REPOSITORY git@github.com:polycpp/cookie.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    polycpp_mime
    GIT_REPOSITORY git@github.com:polycpp/mime.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    polycpp_negotiate
    GIT_REPOSITORY git@github.com:polycpp/negotiate.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    polycpp_ipaddr
    GIT_REPOSITORY git@github.com:polycpp/ipaddr.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

# Fetch mime BEFORE negotiate (negotiate depends on mime)
FetchContent_MakeAvailable(
    polycpp_path_to_regexp
    polycpp_qs
    polycpp_cookie
    polycpp_mime
    polycpp_negotiate
    polycpp_ipaddr
)

# ── Library target ────────────────────────────────────────────────────
add_library(polycpp_express
    src/express.cpp
)
target_link_libraries(polycpp_express PUBLIC
    polycpp
    polycpp_path_to_regexp
    polycpp_qs
    polycpp_cookie
    polycpp_mime
    polycpp_negotiate
    polycpp_ipaddr
)
target_include_directories(polycpp_express PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Alias for consistent naming
add_library(polycpp::express ALIAS polycpp_express)

# ── Tests ─────────────────────────────────────────────────────────────
option(POLYCPP_EXPRESS_BUILD_TESTS "Build tests" ON)
if(POLYCPP_EXPRESS_BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(test_express_basic tests/test_express_basic.cpp)
    target_link_libraries(test_express_basic PRIVATE polycpp_express GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(test_express_basic)

    add_executable(test_router tests/test_router.cpp)
    target_link_libraries(test_router PRIVATE polycpp_express GTest::gtest_main)
    gtest_discover_tests(test_router)

    add_executable(test_view_render tests/test_view_render.cpp)
    target_link_libraries(test_view_render PRIVATE polycpp_express GTest::gtest_main)
    gtest_discover_tests(test_view_render)
endif()
