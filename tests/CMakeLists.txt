add_executable(test_mesh_repair
    test_mesh_repair.cpp
)

target_link_libraries(test_mesh_repair
    PRIVATE
        MeshCore
)
