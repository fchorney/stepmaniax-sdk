cmake_minimum_required(VERSION 3.14)
project(smx-sdk-lite VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Hide symbols by default (only SMX_API symbols are exported)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Find hidapi
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(HIDAPI hidapi-hidraw)
    if(NOT HIDAPI_FOUND)
        pkg_check_modules(HIDAPI hidapi-libusb)
    endif()
    if(NOT HIDAPI_FOUND)
        pkg_check_modules(HIDAPI hidapi)
    endif()
endif()

if(NOT HIDAPI_FOUND)
    # macOS Homebrew paths
    if(APPLE)
        list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
    endif()
    find_package(hidapi REQUIRED)
    set(HIDAPI_LIBRARIES hidapi::hidapi)
    set(HIDAPI_INCLUDE_DIRS "")
endif()

# Library (use -DBUILD_SHARED_LIBS=ON for shared, default is static)
add_library(smx-lite
    SMXConfigPacket.cpp
    SMXDeviceConnection.cpp
    SMX.cpp
)

target_include_directories(smx-lite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${HIDAPI_INCLUDE_DIRS})
target_link_libraries(smx-lite PUBLIC ${HIDAPI_LIBRARIES})
target_compile_definitions(smx-lite PRIVATE SMX_EXPORTS)

find_package(Threads REQUIRED)
target_link_libraries(smx-lite PUBLIC Threads::Threads)

# macOS: build universal binary (Intel + Apple Silicon) if not cross-compiling
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set_target_properties(smx-lite PROPERTIES OSX_ARCHITECTURES "x86_64;arm64")
endif()

# Sample
add_executable(smx-sample sample.cpp)
target_link_libraries(smx-sample PRIVATE smx-lite)
