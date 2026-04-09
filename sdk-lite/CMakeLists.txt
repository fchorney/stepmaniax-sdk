cmake_minimum_required(VERSION 3.14)
project(smx-sdk-lite VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    find_package(hidapi REQUIRED)
    set(HIDAPI_LIBRARIES hidapi::hidapi)
    set(HIDAPI_INCLUDE_DIRS "")
endif()

# Library
add_library(smx-lite
    SMXConfigPacket.cpp
    SMXDeviceConnection.cpp
    SMX.cpp
)

target_include_directories(smx-lite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${HIDAPI_INCLUDE_DIRS})
target_link_libraries(smx-lite PUBLIC ${HIDAPI_LIBRARIES})
target_compile_definitions(smx-lite PRIVATE SMX_EXPORTS)

# If using Homebrew on macOS, you may need to specify the library and include directories
target_include_directories(smx-lite PUBLIC /opt/homebrew/include)
target_link_directories(smx-lite PUBLIC /opt/homebrew/lib)

find_package(Threads REQUIRED)
target_link_libraries(smx-lite PUBLIC Threads::Threads)

# Sample
add_executable(smx-sample sample.cpp)
target_link_libraries(smx-sample PRIVATE smx-lite)
