add_library(MeshCore STATIC
    src/HalfEdgeMesh.cpp
    src/MeshIO.cpp
    src/MeshRepair.cpp
    src/Polycube.cpp
)

target_include_directories(MeshCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(MeshCore
    PUBLIC
        Eigen
    PRIVATE
        tetgen
)
