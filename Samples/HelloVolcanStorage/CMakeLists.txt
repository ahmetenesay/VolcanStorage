add_executable(HelloVolcanStorage
    main.cpp
)

target_link_libraries(HelloVolcanStorage
    PRIVATE
        VolcanStorage
        Vulkan::Vulkan
)

if(WIN32)
    target_compile_definitions(HelloVolcanStorage PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(HelloVolcanStorage PRIVATE user32 gdi32)
endif()

# Ensure sample archive is copied next to executable
add_custom_command(TARGET HelloVolcanStorage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/nano_banana_archive.volcan
        $<TARGET_FILE_DIR:HelloVolcanStorage>/nano_banana_archive.volcan
)
