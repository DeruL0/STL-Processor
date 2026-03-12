add_executable(test_mesh_repair
    test_mesh_repair.cpp
)

target_include_directories(test_mesh_repair
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/MeshCore/src
)

target_link_libraries(test_mesh_repair
    PRIVATE
        MeshCore
)
