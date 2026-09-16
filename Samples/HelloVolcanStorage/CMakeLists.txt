add_executable(HelloVolcanStorage
    main.cpp
)

target_link_libraries(HelloVolcanStorage
    PRIVATE
        VolcanStorage
        Vulkan::Vulkan
        GDeflate
)

target_include_directories(HelloVolcanStorage
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../GDeflate/GDeflate
)

if(WIN32)
    target_compile_definitions(HelloVolcanStorage PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(HelloVolcanStorage PRIVATE user32 gdi32)
endif()
