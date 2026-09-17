cmake_minimum_required(VERSION 3.20)

project(VolcanStorage
    VERSION 1.0.0
    DESCRIPTION "High-performance cross-platform asynchronous GPU asset streaming and decompression library for Vulkan"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)


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
        if((CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64") AND EXISTS "${VULKAN_SDK_PATH}/Lib-ARM64/vulkan-1.lib")
            set(Vulkan_LIBRARY "${VULKAN_SDK_PATH}/Lib-ARM64/vulkan-1.lib" CACHE FILEPATH "" FORCE)
            message(STATUS "VolcanStorage: Selected ARM64 Vulkan library: ${Vulkan_LIBRARY}")
        else()
            set(Vulkan_LIBRARY "${VULKAN_SDK_PATH}/Lib/vulkan-1.lib" CACHE FILEPATH "" FORCE)
            message(STATUS "VolcanStorage: Selected default Vulkan library: ${Vulkan_LIBRARY}")
        endif()
    endif()
endif()

find_package(Vulkan QUIET)

if(NOT Vulkan_FOUND AND (APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
    find_path(MOLTENVK_INCLUDE_DIR NAMES vulkan/vulkan.h HINTS 
        "/opt/homebrew/opt/molten-vk/include"
        "/usr/local/opt/molten-vk/include"
        "/opt/homebrew/include"
        "/usr/local/include"
    )
    find_library(MOLTENVK_LIB NAMES MoltenVK vulkan HINTS 
        "/opt/homebrew/opt/molten-vk/lib"
        "/usr/local/opt/molten-vk/lib"
        "/opt/homebrew/lib"
        "/usr/local/lib"
    )
    if(MOLTENVK_INCLUDE_DIR AND MOLTENVK_LIB)
        set(Vulkan_FOUND TRUE)
        set(Vulkan_INCLUDE_DIRS ${MOLTENVK_INCLUDE_DIR})
        set(Vulkan_LIBRARIES ${MOLTENVK_LIB})
        if(NOT TARGET Vulkan::Vulkan)
            add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
            set_target_properties(Vulkan::Vulkan PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${Vulkan_INCLUDE_DIRS}"
                IMPORTED_LOCATION "${Vulkan_LIBRARIES}"
            )
        endif()
        message(STATUS "VolcanStorage: Found MoltenVK manually at ${MOLTENVK_LIB}")
    endif()
endif()

if(NOT Vulkan_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(Vulkan_PKG QUIET vulkan)
        if(Vulkan_PKG_FOUND)
            set(Vulkan_FOUND TRUE)
            set(Vulkan_INCLUDE_DIRS ${Vulkan_PKG_INCLUDE_DIRS})
            set(Vulkan_LIBRARIES ${Vulkan_PKG_LIBRARIES})
            if(NOT TARGET Vulkan::Vulkan)
                add_library(Vulkan::Vulkan INTERFACE IMPORTED)
                set_target_properties(Vulkan::Vulkan PROPERTIES
                    INTERFACE_INCLUDE_DIRECTORIES "${Vulkan_INCLUDE_DIRS}"
                    INTERFACE_LINK_LIBRARIES "${Vulkan_LIBRARIES}"
                )
            endif()
            message(STATUS "VolcanStorage: Found Vulkan via pkg-config: ${Vulkan_LIBRARIES}")
        endif()
    endif()
endif()

if(NOT Vulkan_FOUND)
    find_package(Vulkan REQUIRED)
endif()

message(STATUS "VolcanStorage: Found Vulkan Include: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "VolcanStorage: Found Vulkan Libraries: ${Vulkan_LIBRARIES}")

# -----------------------------------------------------------------------------
# Shader Compiler Detection (glslc from Vulkan SDK)
# -----------------------------------------------------------------------------
find_program(GLSLC_EXECUTABLE NAMES glslc HINTS "${VULKAN_SDK_PATH}/Bin" "${VULKAN_SDK_PATH}/bin")

if(GLSLC_EXECUTABLE)
    message(STATUS "VolcanStorage: Found glslc: ${GLSLC_EXECUTABLE}")
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

# Archiving Tool
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/volcan_pack.cpp")
    add_executable(volcan_pack tools/volcan_pack.cpp)
    target_link_libraries(volcan_pack PRIVATE VolcanStorage GDeflate)
    target_include_directories(volcan_pack PRIVATE include GDeflate/GDeflate)
endif()

# -----------------------------------------------------------------------------
# Installation & Automated Packaging (bin, include, docs [pdf & html], txt/md)
# -----------------------------------------------------------------------------
include(GNUInstallDirs)

# 1. Binaries (Libraries, Executables, Shaders)
install(TARGETS VolcanStorage
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION bin
)

if(TARGET VolcanStorage_Shared)
    install(TARGETS VolcanStorage_Shared
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin
        ARCHIVE DESTINATION bin
    )
endif()

if(TARGET HelloVolcanStorage)
    install(TARGETS HelloVolcanStorage RUNTIME DESTINATION bin)
endif()

if(TARGET GDeflate)
    install(TARGETS GDeflate RUNTIME DESTINATION bin LIBRARY DESTINATION bin ARCHIVE DESTINATION bin)
endif()

if(TARGET deflate_static)
    install(TARGETS deflate_static ARCHIVE DESTINATION bin)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/GDeflate.spv")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/shaders/GDeflate.spv" DESTINATION bin/shaders)
endif()

# 2. Public C++20 Header API
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/volcanstorage" DESTINATION include)

# 3. Documentation (Developer Guide PDF and HTML)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Docs/" 
    DESTINATION docs 
    FILES_MATCHING 
        PATTERN "*.pdf" 
        PATTERN "*.html"
)

# 4. Licensing and Project Manifests (txt & md)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/NOTICES.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/SECURITY.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/SUPPORT.md"
    DESTINATION .
)

# -----------------------------------------------------------------------------
# CPack Packaging Configuration (Generates VolcanStorage-<Platform>-<Arch>.zip)
# -----------------------------------------------------------------------------
if(WIN32)
    set(VOLCAN_OS "Windows")
    if(CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(VOLCAN_ARCH "arm64")
    else()
        set(VOLCAN_ARCH "x64")
    endif()
elseif(APPLE)
    set(VOLCAN_OS "macOS")
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
        set(VOLCAN_ARCH "x64")
    elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        set(VOLCAN_ARCH "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
        set(VOLCAN_ARCH "arm64")
    else()
        set(VOLCAN_ARCH "x64")
    endif()
else()
    set(VOLCAN_OS "Linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(VOLCAN_ARCH "arm64")
    else()
        set(VOLCAN_ARCH "x64")
    endif()
endif()

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "VolcanStorage-${VOLCAN_OS}-${VOLCAN_ARCH}")
set(CPACK_PACKAGE_FILE_NAME "VolcanStorage-${VOLCAN_OS}-${VOLCAN_ARCH}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Ahmet Enes (Chiretallyn)")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist")

include(CPack)

# Convenience target: cmake --build <dir> --target package_zip
add_custom_target(package_zip
    COMMAND ${CMAKE_CPACK_COMMAND} -G ZIP -C $<CONFIG>
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "[VolcanStorage] Automated packaging of release zip archive via CPack..."
)
