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
endif()
