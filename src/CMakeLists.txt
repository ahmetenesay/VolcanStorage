# -----------------------------------------------------------------------------
# 1. Static Library Target (VolcanStorage.lib / libVolcanStorage.a)
# -----------------------------------------------------------------------------
add_library(VolcanStorage STATIC
    volcanstorage.cpp
    ../include/volcanstorage/volcanstorage.h
)

target_include_directories(VolcanStorage
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
        ${Vulkan_INCLUDE_DIRS}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(VolcanStorage
    PUBLIC
        Vulkan::Vulkan
)

if(TARGET GDeflate)
    target_link_libraries(VolcanStorage PUBLIC GDeflate)
    target_include_directories(VolcanStorage PRIVATE 
        "${CMAKE_CURRENT_SOURCE_DIR}/../GDeflate/GDeflate"
        "${CMAKE_CURRENT_SOURCE_DIR}/../GDeflate/3rdparty/libdeflate"
    )
    target_compile_definitions(VolcanStorage PRIVATE VOLCANSTORAGE_HAS_GDEFLATE=1)
endif()

if(WIN32)
    target_compile_definitions(VolcanStorage PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# -----------------------------------------------------------------------------
# 2. Shared / Dynamic Library Target (VolcanStorage.dll / libVolcanStorage.so / .dylib)
# -----------------------------------------------------------------------------
add_library(VolcanStorage_Shared SHARED
    volcanstorage.cpp
    ../include/volcanstorage/volcanstorage.h
)

set_target_properties(VolcanStorage_Shared PROPERTIES
    OUTPUT_NAME "VolcanStorage"
    ARCHIVE_OUTPUT_NAME "VolcanStorage_dll"
)

target_compile_definitions(VolcanStorage_Shared PRIVATE VOLCANSTORAGE_BUILD_SHARED=1)

target_include_directories(VolcanStorage_Shared
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
        ${Vulkan_INCLUDE_DIRS}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(VolcanStorage_Shared
    PUBLIC
        Vulkan::Vulkan
)

if(TARGET GDeflate)
    target_link_libraries(VolcanStorage_Shared PUBLIC GDeflate)
    target_include_directories(VolcanStorage_Shared PRIVATE 
        "${CMAKE_CURRENT_SOURCE_DIR}/../GDeflate/GDeflate"
        "${CMAKE_CURRENT_SOURCE_DIR}/../GDeflate/3rdparty/libdeflate"
    )
    target_compile_definitions(VolcanStorage_Shared PRIVATE VOLCANSTORAGE_HAS_GDEFLATE=1)
endif()

if(WIN32)
    target_compile_definitions(VolcanStorage_Shared PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
