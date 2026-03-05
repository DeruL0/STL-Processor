add_library(DX12Engine STATIC
    src/D3DApp.cpp
    src/D3DUtil.cpp
    src/FrameResource.cpp
    src/Camera.cpp
    src/GameTimer.cpp
    src/GeometryGenerator.cpp
    src/MathHelper.cpp
    src/DDSTextureLoader.cpp
)

target_include_directories(DX12Engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(DX12Engine
    PUBLIC
        d3dx12
    PRIVATE
        d3d12
        dxgi
        d3dcompiler
        dxguid
        d3d11
)

if(MSVC)
    target_compile_definitions(DX12Engine PUBLIC UNICODE _UNICODE NOMINMAX)
endif()
