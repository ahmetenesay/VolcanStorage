cmake_minimum_required(VERSION 3.20)

project(VolcanStorage
    VERSION 1.0.0
    DESCRIPTION "High-performance cross-platform asynchronous GPU asset streaming and decompression library for Vulkan"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# Vulkan SDK Detection (VULKAN_SDK is primary, VK_SDK_PATH is fallback)
# -----------------------------------------------------------------------------
if(DEFINED ENV{VULKAN_SDK} AND NOT "$ENV{VULKAN_SDK}" STREQUAL "")
    set(VULKAN_SDK_PATH "$ENV{VULKAN_SDK}")
    message(STATUS "VolcanStorage: Using VULKAN_SDK from environment: ${VULKAN_SDK_PATH}")
elseif(DEFINED ENV{VK_SDK_PATH} AND NOT "$ENV{VK_SDK_PATH}" STREQUAL "")
    set(VULKAN_SDK_PATH "$ENV{VK_SDK_PATH}")
    message(STATUS "VolcanStorage: Using VK_SDK_PATH fallback: ${VULKAN_SDK_PATH}")
else()
    message(WARNING "VolcanStorage: Neither VULKAN_SDK nor VK_SDK_PATH environment variable is set. Falling back to default system search.")
endif()

if(DEFINED VULKAN_SDK_PATH)
    list(PREPEND CMAKE_PREFIX_PATH "${VULKAN_SDK_PATH}")
    if(WIN32)
        set(Vulkan_INCLUDE_DIR "${VULKAN_SDK_PATH}/Include" CACHE PATH "" FORCE)
        if(CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
            set(Vulkan_LIBRARY "${VULKAN_SDK_PATH}/Lib-ARM64/vulkan-1.lib" CACHE FILEPATH "" FORCE)
            message(STATUS "VolcanStorage: Selected ARM64 Vulkan library: ${Vulkan_LIBRARY}")
        else()
            set(Vulkan_LIBRARY "${VULKAN_SDK_PATH}/Lib/vulkan-1.lib" CACHE FILEPATH "" FORCE)
            message(STATUS "VolcanStorage: Selected x64 Vulkan library: ${Vulkan_LIBRARY}")
        endif()
    endif()
endif()

find_package(Vulkan REQUIRED)

message(STATUS "VolcanStorage: Found Vulkan Include: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "VolcanStorage: Found Vulkan Libraries: ${Vulkan_LIBRARIES}")

# -----------------------------------------------------------------------------
# Shader Compiler Detection (glslc & dxc from Vulkan SDK)
# -----------------------------------------------------------------------------
find_program(GLSLC_EXECUTABLE NAMES glslc HINTS "${VULKAN_SDK_PATH}/Bin" "${VULKAN_SDK_PATH}/bin")
find_program(DXC_EXECUTABLE NAMES dxc HINTS "${VULKAN_SDK_PATH}/Bin" "${VULKAN_SDK_PATH}/bin")

if(GLSLC_EXECUTABLE)
    message(STATUS "VolcanStorage: Found glslc: ${GLSLC_EXECUTABLE}")
endif()
if(DXC_EXECUTABLE)
    message(STATUS "VolcanStorage: Found dxc: ${DXC_EXECUTABLE}")
endif()

# -----------------------------------------------------------------------------
# Shader Targets
# -----------------------------------------------------------------------------
if(GLSLC_EXECUTABLE)
    set(SHADER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/shaders/GDeflate.comp")
    set(SHADER_SPV "${CMAKE_CURRENT_BINARY_DIR}/shaders/GDeflate.spv")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    
    add_custom_command(
        OUTPUT "${SHADER_SPV}"
        COMMAND "${GLSLC_EXECUTABLE}" -I"${CMAKE_CURRENT_SOURCE_DIR}/shaders" "${SHADER_SRC}" -o "${SHADER_SPV}"
        DEPENDS "${SHADER_SRC}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/tilestream.glsl"
        COMMENT "Compiling GDeflate GLSL compute shader to SPIR-V..."
    )
    add_custom_target(VolcanStorageShaders ALL DEPENDS "${SHADER_SPV}")
endif()

# -----------------------------------------------------------------------------
# Build Options
# -----------------------------------------------------------------------------
option(VOLCANSTORAGE_BUILD_SAMPLES "Build VolcanStorage sample applications" ON)
option(VOLCANSTORAGE_BUILD_GDEFLATE "Build original GDeflate codec submodule" ON)

# -----------------------------------------------------------------------------
# Subdirectories
# -----------------------------------------------------------------------------
if(VOLCANSTORAGE_BUILD_GDEFLATE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/GDeflate/GDeflate/CMakeLists.txt")
    add_subdirectory(GDeflate/GDeflate)
endif()

# Core Library
add_subdirectory(src)

# Samples
if(VOLCANSTORAGE_BUILD_SAMPLES)
    add_subdirectory(samples/HelloVolcanStorage)
endif()
