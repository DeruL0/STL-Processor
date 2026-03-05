#pragma once

#include "DX12Engine/D3DUtil.h"
#include "MeshCore/HalfEdgeMesh.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct MeshRendererBuffers {
    std::vector<RenderVertex> Vertices;
    std::vector<std::int32_t> Indices;
};

class MeshRenderer {
public:
    static MeshRendererBuffers BuildFromHalfEdge(const HE_MeshData& meshData);
    static std::unique_ptr<MeshGeometry> CreateMeshGeometry(
        const std::string& geometryName,
        const std::string& submeshName,
        const MeshRendererBuffers& buffers,
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList
    );
    static void UpdateMeshGeometry(
        MeshGeometry& geometry,
        const std::string& submeshName,
        const MeshRendererBuffers& buffers,
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList
    );
};
