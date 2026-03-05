cmake_minimum_required(VERSION 3.21)

project(STLRepair LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(STLREPAIR_BUILD_TESTS "Build STLRepair unit tests." OFF)

add_subdirectory(third_party)
add_subdirectory(src/MeshCore)
add_subdirectory(src/DX12Engine)
add_subdirectory(src/GUI)
add_subdirectory(src/App)

if(STLREPAIR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
