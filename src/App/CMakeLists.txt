add_executable(App WIN32
    STLApp.h
    STLApp.cpp
    MeshRenderer.h
    MeshRenderer.cpp
    main.cpp
)

target_include_directories(App
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(App
    PRIVATE
        MeshCore
        DX12Engine
        GUI
)

set_target_properties(App PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:App>"
)

add_custom_command(TARGET App POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:App>/assets
)
