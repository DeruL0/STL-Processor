#pragma once
#pragma once

#include "DX12.h"
#include "MathHelper.h"
#include "UploadBuffer.h"

struct ObjectConstants{
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
    UINT     MaterialIndex;
    UINT     ObjPad0;
    UINT     ObjPad1;
    UINT     ObjPad2;
};

struct PassConstants{
    DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    float cbPerObjectPad1 = 0.0f;
    DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
    DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
    float NearZ = 0.0f;
    float FarZ = 0.0f;
    float TotalTime = 0.0f;
    float DeltaTime = 0.0f;
    DirectX::XMFLOAT4 AmbientLight = { 0.0f, 0.0f, 0.0f, 1.0f };
    Light Lights[MaxLights];
};

struct MaterialData{
    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
    float Roughness = 64.0f;

    // Used in texture mapping.
    DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();

    UINT DiffuseMapIndex = 0;
    UINT MaterialPad0;
    UINT MaterialPad1;
    UINT MaterialPad2;
};

struct Triangle {
    UINT Index;
    DirectX::XMFLOAT3 Normal;

    std::int32_t VertexIndex0;
    std::int32_t VertexIndex1;
    std::int32_t VertexIndex2;
};

struct Vertex{
    //std::int32_t Index;
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexC;

    bool operator==(const Vertex& other) const {
        return(Pos.x == other.Pos.x && Pos.y == other.Pos.y && Pos.z == other.Pos.z);
    }
};

template<>
struct std::hash<Vertex> {
    size_t operator()(const Vertex& v) const {
        return ((hash<float>()(v.Pos.x)
            ^ (hash<float>()(v.Pos.y) << 1)) >> 1)
            ^ (hash<float>()(v.Pos.z) << 1);
    }
};

//以CPU每帧都需要更新的资源作为基本元素,创建一个环形数组,我们称这些资源为帧资源,
//而这种循环数组通常是由3个帧资源元素组成 -- CPU往往会比GPU提前处理两帧,以确保GPU可持续工作 
struct FrameResource{
public:
    //需要增加材质的构造
    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    //每一帧都需要自己的命令分配器 -- 因为在GPU处理完命令之前,不能重置命令分配器
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

    //每一帧都需要自己的常量缓冲区(上传堆) -- GPU处理完之前,不能对其更新
    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
    std::unique_ptr<UploadBuffer<MaterialData>> MaterialBuffer = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;

    //通过fence值将命令标记到此fence点,这使我们可以检测GPU是否还在使用这些资源
    UINT64 Fence = 0;
};