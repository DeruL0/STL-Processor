add_library(Eigen INTERFACE)
target_include_directories(Eigen INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/eigen
)

add_library(d3dx12 INTERFACE)
target_include_directories(d3dx12 INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/d3dx12
)

add_subdirectory(tetgen)

set(IMGUI_VERSION_FILE ${CMAKE_CURRENT_SOURCE_DIR}/imgui/VERSION.txt)
if(EXISTS ${IMGUI_VERSION_FILE})
    file(READ ${IMGUI_VERSION_FILE} IMGUI_VERSION_RAW)
    string(STRIP "${IMGUI_VERSION_RAW}" IMGUI_VERSION)
    message(STATUS "Dear ImGui version: ${IMGUI_VERSION}")
endif()
