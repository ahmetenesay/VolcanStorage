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
)

target_link_libraries(VolcanStorage
    PUBLIC
        Vulkan::Vulkan
)

if(TARGET GDeflate)
    target_link_libraries(VolcanStorage PUBLIC GDeflate)
    target_include_directories(VolcanStorage PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../GDeflate/GDeflate")
    target_compile_definitions(VolcanStorage PRIVATE VOLCANSTORAGE_HAS_GDEFLATE=1)
endif()

if(WIN32)
    target_compile_definitions(VolcanStorage PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
