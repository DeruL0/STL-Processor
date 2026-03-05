#include "MeshRenderer.h"

#include <sstream>
#include <stdexcept>

namespace {
void UploadMeshBuffers(
    MeshGeometry& geometry,
    const std::string& submeshName,
    const MeshRendererBuffers& buffers,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList
) {
    if (device == nullptr || commandList == nullptr) {
        throw std::runtime_error("Invalid D3D12 device or command list.");
    }
    if (buffers.Vertices.empty() || buffers.Indices.empty()) {
        throw std::runtime_error("Mesh buffers are empty.");
    }

    const UINT vbByteSize = static_cast<UINT>(buffers.Vertices.size() * sizeof(RenderVertex));
    const UINT ibByteSize = static_cast<UINT>(buffers.Indices.size() * sizeof(std::int32_t));

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geometry.VertexBufferCPU));
    CopyMemory(geometry.VertexBufferCPU->GetBufferPointer(), buffers.Vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geometry.IndexBufferCPU));
    CopyMemory(geometry.IndexBufferCPU->GetBufferPointer(), buffers.Indices.data(), ibByteSize);

    geometry.VertexBufferGPU = dx12Util::CreateDefaultBuffer(
        device,
        commandList,
        buffers.Vertices.data(),
        vbByteSize,
        geometry.VertexBufferUploader
    );

    geometry.IndexBufferGPU = dx12Util::CreateDefaultBuffer(
        device,
        commandList,
        buffers.Indices.data(),
        ibByteSize,
        geometry.IndexBufferUploader
    );

    geometry.VertexByteStride = sizeof(RenderVertex);
    geometry.VertexBufferByteSize = vbByteSize;
    geometry.IndexFormat = DXGI_FORMAT_R32_UINT;
    geometry.IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = static_cast<UINT>(buffers.Indices.size());
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;
    geometry.DrawArgs[submeshName] = submesh;
}
} // namespace

MeshRendererBuffers MeshRenderer::BuildFromHalfEdge(const HE_MeshData& meshData) {
    MeshRendererBuffers buffers;
    buffers.Indices.reserve(meshData.HE_Triangles.size() * 3);
    buffers.Vertices.reserve(meshData.HE_Vertexes.size());

    const std::size_t vertexCount = meshData.HE_Vertexes.size();
    for (std::size_t i = 0; i < meshData.HE_Triangles.size(); ++i) {
        const HE_Triangle* triangle = meshData.HE_Triangles[i];
        if (triangle == nullptr) {
            std::ostringstream oss;
            oss << "Null HE_Triangle at index " << i;
            throw std::runtime_error(oss.str());
        }
        if (triangle->VertexIndex0 < 0 || triangle->VertexIndex1 < 0 || triangle->VertexIndex2 < 0) {
            std::ostringstream oss;
            oss << "Negative vertex index in triangle " << i;
            throw std::runtime_error(oss.str());
        }
        if (static_cast<std::size_t>(triangle->VertexIndex0) >= vertexCount ||
            static_cast<std::size_t>(triangle->VertexIndex1) >= vertexCount ||
            static_cast<std::size_t>(triangle->VertexIndex2) >= vertexCount) {
            std::ostringstream oss;
            oss << "Triangle " << i << " has out-of-range vertex index.";
            throw std::runtime_error(oss.str());
        }

        buffers.Indices.push_back(triangle->VertexIndex0);
        buffers.Indices.push_back(triangle->VertexIndex1);
        buffers.Indices.push_back(triangle->VertexIndex2);
    }

    for (std::size_t i = 0; i < meshData.HE_Vertexes.size(); ++i) {
        const HE_Vertex* vertex = meshData.HE_Vertexes[i];
        if (vertex == nullptr) {
            std::ostringstream oss;
            oss << "Null HE_Vertex at index " << i;
            throw std::runtime_error(oss.str());
        }

        RenderVertex renderVertex;
        renderVertex.Pos = { vertex->Pos.x, vertex->Pos.y, vertex->Pos.z };
        renderVertex.Normal = { vertex->Normal.x, vertex->Normal.y, vertex->Normal.z };
        renderVertex.TexC = { 0.0f, 0.0f };
        buffers.Vertices.push_back(renderVertex);
    }

    return buffers;
}

std::unique_ptr<MeshGeometry> MeshRenderer::CreateMeshGeometry(
    const std::string& geometryName,
    const std::string& submeshName,
    const MeshRendererBuffers& buffers,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList
) {
    auto geometry = std::make_unique<MeshGeometry>();
    geometry->Name = geometryName;
    UploadMeshBuffers(*geometry, submeshName, buffers, device, commandList);
    return geometry;
}

void MeshRenderer::UpdateMeshGeometry(
    MeshGeometry& geometry,
    const std::string& submeshName,
    const MeshRendererBuffers& buffers,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList
) {
    UploadMeshBuffers(geometry, submeshName, buffers, device, commandList);
}
