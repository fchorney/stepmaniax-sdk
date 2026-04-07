cmake_minimum_required(VERSION 3.14)
project(smx-sdk-lite VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find hidapi. Try pkg-config first (Linux), then cmake find (macOS/Windows via vcpkg/brew).
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    # Linux: prefer hidraw backend over libusb for better HID support
    pkg_check_modules(HIDAPI hidapi-hidraw)
    if(NOT HIDAPI_FOUND)
        pkg_check_modules(HIDAPI hidapi-libusb)
    endif()
    if(NOT HIDAPI_FOUND)
        pkg_check_modules(HIDAPI hidapi)
    endif()
endif()

if(NOT HIDAPI_FOUND)
    # Fallback: try find_package (works with vcpkg, homebrew cmake modules, etc.)
    find_package(hidapi REQUIRED)
    set(HIDAPI_LIBRARIES hidapi::hidapi)
    set(HIDAPI_INCLUDE_DIRS "")
endif()

# Library
add_library(smx-lite
    Helpers.cpp
    SMXConfigPacket.cpp
    SMXDeviceSearch.cpp
    SMXDeviceConnection.cpp
    SMXDevice.cpp
    SMXManager.cpp
    SMX.cpp
)

target_include_directories(smx-lite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${HIDAPI_INCLUDE_DIRS})
target_link_libraries(smx-lite PUBLIC ${HIDAPI_LIBRARIES})
target_compile_definitions(smx-lite PRIVATE SMX_EXPORTS)

find_package(Threads REQUIRED)
target_link_libraries(smx-lite PUBLIC Threads::Threads)

# Sample
add_executable(smx-sample sample.cpp)
target_link_libraries(smx-sample PRIVATE smx-lite)
