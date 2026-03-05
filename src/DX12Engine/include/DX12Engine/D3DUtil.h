#pragma once

#include <windows.h>
#include <wrl.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXColors.h>
#include <DirectXCollision.h>
#include <string>
#include <memory>
#include <algorithm>
#include <vector>
#include <array>
#include <unordered_map>
#include <map>
#include <queue>
#include <cstdint>
#include <functional>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cassert>
#include "d3dx12.h"
#include "DX12Engine/DDSTextureLoader.h"
#include "DX12Engine/MathHelper.h"

extern const int gNumFrameResources;

inline void d3dSetDebugName(IDXGIObject* obj, const char* name){
    if (obj){
        obj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(name), name);
    }
}
inline void d3dSetDebugName(ID3D12Device* obj, const char* name){
    if (obj){
        obj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(name), name);
    }
}
inline void d3dSetDebugName(ID3D12DeviceChild* obj, const char* name){
    if (obj){
        obj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(name), name);
    }
}

//把string类型的_FILE_转换成宽字符，因为文件名或文件路径可能包含扩展字符集的字符导致不能正常显示
inline std::wstring AnsiToWString(const std::string& str){
	WCHAR buffer[512];
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
	return std::wstring(buffer);
}

//如果创建失败，即FAILED(hr__)为真，就会抛出异常DxException，执行其代码，对错误信息进行处理并输出
class DxException{
public:
    DxException() = default;
    DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber);

    std::wstring ToString()const;

    HRESULT ErrorCode = S_OK;
    std::wstring FunctionName;
    std::wstring Filename;
    int LineNumber = -1;
};

//接受HRESULT类型的参数输入，然后判断HRESULT是否创建成功
#ifndef ThrowIfFailed
#define ThrowIfFailed(x)													\
{																			\
	HRESULT hr__ = (x);														\
	std::wstring wfn = AnsiToWString(__FILE__);								\
	if (FAILED(hr__)) { throw DxException(hr__, L#x, wfn, __LINE__); }		\
}//'\'符号用于多行定义
#endif

#ifndef ReleaseCom
#define ReleaseCom(x) { if(x){ x->Release(); x = 0; } }
#endif

class dx12Util{
public:
    static bool IsKeyDown(int vkeyCode);
    static std::string ToString(HRESULT hr);
    static UINT CalcConstantBufferByteSize(UINT byteSize){
        return (byteSize + 255) & ~255;
    }
    static Microsoft::WRL::ComPtr<ID3DBlob> LoadBinary(const std::wstring& filename);

	//构建用于使用默认缓冲区的工具函数
	//1.创建实际的默认缓冲区资源
	//2.将数据从CPU端复制到上传堆,再从上传堆复制到GPU端的缓冲区中
    static Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const void* initData,
        UINT64 byteSize,
        Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer
	);

	//用于运行时编译着色器的辅助函数
    static Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(
        const std::wstring& filename,
        const D3D_SHADER_MACRO* defines,
        const std::string& entrypoint,
        const std::string& target
	);
};

struct RenderVertex {
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexC;

    bool operator==(const RenderVertex& other) const {
        return Pos.x == other.Pos.x && Pos.y == other.Pos.y && Pos.z == other.Pos.z;
    }
};


//SubmeshGeometry定义MeshGeometry中存储的单个几何体
//适用于将多个几何体数据存于一个顶点缓冲区和一个索引缓冲区
//提供单个几何体进行绘制所需要的数据和偏移量
struct SubmeshGeometry{
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	INT BaseVertexLocation = 0;

	DirectX::BoundingBox Bounds;
};

//创建一个同时存有顶点缓冲区和索引缓冲区的结构体来方便地定义多个几何体
struct MeshGeometry{
	std::string Name;

	//系统内存副本,由于顶点/索引可以是泛型格式(具体格式依用户而定),所以用Blob类型来表示待用户在使用时再将其转换为适当的类型
	Microsoft::WRL::ComPtr<ID3DBlob> VertexBufferCPU = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> IndexBufferCPU = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;

	//与缓冲区相关数据,顶点缓冲区和索引缓冲区desc中的成员
	UINT VertexByteStride = 0;
	UINT VertexBufferByteSize = 0;
	DXGI_FORMAT IndexFormat = DXGI_FORMAT_R16_UINT;
	UINT IndexBufferByteSize = 0;

	//一个MeshGeometry能够存储一组顶点/索引缓冲区中的多个几何体
	//若利用下列容器来定义子网络几何体,我们就能单独地绘制出其中的子网络(单个几何体)
	std::unordered_map<std::string, SubmeshGeometry> DrawArgs;

	D3D12_VERTEX_BUFFER_VIEW VertexBufferView()const{
		D3D12_VERTEX_BUFFER_VIEW vbv;
		vbv.BufferLocation = VertexBufferGPU->GetGPUVirtualAddress();
		vbv.StrideInBytes = VertexByteStride;
		vbv.SizeInBytes = VertexBufferByteSize;

		return vbv;
	}

	D3D12_INDEX_BUFFER_VIEW IndexBufferView()const{
		D3D12_INDEX_BUFFER_VIEW ibv;
		ibv.BufferLocation = IndexBufferGPU->GetGPUVirtualAddress();
		ibv.Format = IndexFormat;
		ibv.SizeInBytes = IndexBufferByteSize;

		return ibv;
	}

	//待数据上传至GPU后,我们就能释放上传缓冲区内存了
	void DisposeUploaders(){
		VertexBufferUploader = nullptr;
		IndexBufferUploader = nullptr;
	}
};

//因为常量缓冲区封装规则，要按照如下顺序来定义，否则会出现用了4×4个4D向量
struct Light{
	DirectX::XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };	//光源颜色
	float FalloffStart = 1.0f;                          //仅供点光源/聚光灯源使用
	DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };//仅供方向光源/聚光灯源使用
	float FalloffEnd = 10.0f;                           //仅供点光源/聚光灯源使用
	DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  //仅供点光源/聚光灯源使用
	float SpotPower = 64.0f;                            //仅供聚光灯源使用
};

#define MaxLights 16

//为了让GPU能够访问到材质，放置到常量缓冲区中，其结构需要定义在FrameResource中
struct MaterialConstants{
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = 0.25f;

	// Used in texture mapping.
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};

//材质结构，一般用类
struct Material{
	//用于查找材质的唯一对应名称
	std::string Name;

	//本材质在常量缓冲区的索引
	int MatCBIndex = -1;

	//漫反射纹理在SRV堆的索引
	int DiffuseSrvHeapIndex = -1;

	//法线纹理在SRV堆的索引
	int NormalSrvHeapIndex = -1;

	//控制是否在像素着色器中采样漫反射纹理
	bool UseTexture = true;

	//已更新标志，表示本材质有变动，我们需要更新常量缓冲区
	//由于每个FrameResource都有一个材质常量缓冲区，必须对每个FrameResource都进行更新
	//因此修改材质时应该更新脏标志，使每个帧资源得到更新
	int NumFramesDirty = gNumFrameResources;

	//用于着色的材质常量缓冲区数据
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = 0.25f;
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};

struct Texture
{
	// Unique material name for lookup.
	std::string Name;

	std::wstring Filename;

	Microsoft::WRL::ComPtr<ID3D12Resource> Resource = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> UploadHeap = nullptr;
};

