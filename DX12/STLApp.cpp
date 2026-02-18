#include "DX12.h"
#include "MathHelper.h"
#include "UploadBuffer.h"
#include "GeometryGenerator.h"
#include "HalfEdgeStructure.h"
#include "FrameResource.h"
#include "MathHelper.h"
#include "Camera.h"


#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

//单次绘制调用过程中,需要向渲染流水线提交的数据集称为渲染项.
struct RenderItem {
    RenderItem() = default;
    RenderItem(const RenderItem& rhs) = delete;

    //描述物体局部空间相对世界空间的世界矩阵
    //定义了物体位于世界空间的位置朝向和大小
    XMFLOAT4X4 World = MathHelper::Identity4x4();

    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

    //dirty flag(已更新标记)来表示物体的相关数据已发生改变,这意味着我们需要更新常量缓冲区
    //每个FrameResource中都有常量缓冲区,所以我们必须对每个FrameResource都进行更新
    //即当修改物体数据时，应按 NumFramesDirty = gNumFrameResources进行设置，使每个帧资源都能更新
    int NumFramesDirty = gNumFrameResources;

    //该索引指向的GPU常量缓冲区对应于当前渲染项中的物体常量缓冲区
    UINT ObjCBIndex = -1;

    //指向所使用的的材质
    Material* Mat = nullptr;

    //指向参与绘制的几何体(绘制一个可能要多个渲染项)
    MeshGeometry* Geo = nullptr;

    //图元拓扑
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    //DrawIndexedInstanced参数
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

enum class RenderLayer : int{
    Opaque = 0,
    Wire,
    Count
};

class STLApp : public DX12APP {
private:
    HE_MeshData meshData;
    std::vector<std::unique_ptr<FrameResource>> frameResources;
    FrameResource* currFrameResource = nullptr;
    int currFrameResourceIndex = 0;

    UINT cbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> rootSignature = nullptr;

    ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = nullptr;

    //使用unordered_map,并根据名称在常数时间内寻找和引用所需的对象
    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> geometries;
    std::unordered_map<std::string, std::unique_ptr<Material>> materials;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> shaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> PSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

    //所有渲染项
    std::vector<std::unique_ptr<RenderItem>> allRitems;

    //根据PSO划分渲染项
    std::vector<RenderItem*> rItemLayer[(int)RenderLayer::Count];

    //渲染常量缓冲区
    PassConstants mainPassCB;

    Camera camera;

    POINT lastMousePos;

public:
    STLApp(HINSTANCE hInstance);
    STLApp(const STLApp& rhs) = delete;
    STLApp& operator=(const STLApp& rhs) = delete;
    ~STLApp();

    virtual bool Init()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;
    //void UpdateCamera(const GameTimer& gt);

    void OnKeyboardInput(const GameTimer& gt);
    void AnimateMaterials(const GameTimer& gt);
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMaterialBuffer(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);

    void LoadTextures();

    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildSkullGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();
};


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd) {
    //为调试版本开启内存检测，方便检测内存泄露
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try {
        STLApp theApp(hInstance);
        if (!theApp.Init())
            return 0;

        return theApp.Run();
    }
    catch (DxException& e) {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}


STLApp::STLApp(HINSTANCE hInstace) : DX12APP(hInstace) {
}

STLApp::~STLApp() {
    if (dx12Device != nullptr)
        FlushCommandQueue();
}


bool STLApp::Init() {
    if (!DX12APP::Init()) {
        return false;
    }

    //重置命令列表为执行初始化命令准备
    ThrowIfFailed(commandList->Reset(directCmdListAlloc.Get(), nullptr));

    //获取该堆类型中描述符的增量大小
    cbvSrvDescriptorSize = dx12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    camera.LookAt(
        XMFLOAT3(5.0f, 4.0f, -15.0f),
        XMFLOAT3(0.0f, 1.0f, 0.0f),
        XMFLOAT3(0.0f, 1.0f, 0.0f));

    LoadTextures();
    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    //BuildShapeGeometry();
    BuildSkullGeometry();
    BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    //初始化命令
    ThrowIfFailed(commandList->Close());
    ID3D12CommandList* cmdLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

    //等待初始化完成
    FlushCommandQueue();

    return true;
}

void STLApp::OnResize() {
    DX12APP::OnResize();

    //调整窗口尺寸则需更新横纵比并重新计算投影矩阵
    camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void STLApp::Update(const GameTimer& gt) {
    OnKeyboardInput(gt);

    //循环获取帧资源循环数组内容
    currFrameResourceIndex = (currFrameResourceIndex + 1) % gNumFrameResources;
    currFrameResource = frameResources[currFrameResourceIndex].get();

    //GPU是否完成处理当前帧资源所有命令
    //没有另CPU等待，直到GPU完成命令执行并达到fence点
    if (currFrameResource->Fence != 0 && fence->GetCompletedValue() < currFrameResource->Fence) {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(fence->SetEventOnCompletion(currFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    //更新当前帧资源内容
    AnimateMaterials(gt);
    UpdateObjectCBs(gt);
    UpdateMaterialBuffer(gt);
    UpdateMainPassCB(gt);
}

void STLApp::Draw(const GameTimer& gt) {
    auto cmdListAlloc = currFrameResource->CmdListAlloc;

    //当与GPU关联的CommandList执行完时，才将其重置以复用使用记录命令的相关内存
    ThrowIfFailed(cmdListAlloc->Reset());

    //将CommandList载入CommandQueue后，重置CommandList以此复用CommandList和内存
    ThrowIfFailed(commandList->Reset(cmdListAlloc.Get(), PSOs["opaque"].Get()));

    //设置Viewport和ScissorRect，需要随CommandList重置而重置
    commandList->RSSetViewports(1, &screenViewport);
    commandList->RSSetScissorRects(1, &scissorRect);

    //将资源从呈现状态转换为渲染目标状态
    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    //清除后台缓冲区&深度缓冲区
    commandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    commandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    //指定将要渲染的缓冲区
    commandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
    commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    commandList->SetGraphicsRootSignature(rootSignature.Get());

    //绑定根签名到命令队列
    auto passCB = currFrameResource->PassCB->Resource();
    commandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    auto matBuffer = currFrameResource->MaterialBuffer->Resource();
    commandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

    commandList->SetGraphicsRootDescriptorTable(3, srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    DrawRenderItems(commandList.Get(), rItemLayer[(int)RenderLayer::Opaque]);

    commandList->SetPipelineState(PSOs["wire"].Get());
    DrawRenderItems(commandList.Get(), rItemLayer[(int)RenderLayer::Wire]);

    //将资源从渲染目标状态转换为呈现状态
    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    //完成命令记录
    ThrowIfFailed(commandList->Close());

    //将待执行CommandList加入CommandQueue
    ID3D12CommandList* cmdsLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    //交换前后台缓冲区
    ThrowIfFailed(swapChain->Present(0, 0));
    currBackBuffer = (currBackBuffer + 1) % swapChainBufferCount;

    //CPU端增加fence值，将命令标记到此fence点
    currFrameResource->Fence = ++currentFence;

    //GPU端向命令队列添加一条指令来设置一个新fence点
    //因为GPU当前在绘制，所以GPU处理完Signal()以前不会设置新fence点
    commandQueue->Signal(fence.Get(), currentFence);
}


void STLApp::OnMouseDown(WPARAM btnState, int x, int y) {
    lastMousePos.x = x;
    lastMousePos.y = y;

    SetCapture(hMainWnd);

}

void STLApp::OnMouseUp(WPARAM btnState, int x, int y) {
    ReleaseCapture();
}

void STLApp::OnMouseMove(WPARAM btnState, int x, int y) {
    if ((btnState & MK_LBUTTON) != 0) {
        //根据鼠标移动速度计算旋转角度，每个像素依据该角度1/4旋转
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - lastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - lastMousePos.y));

        camera.Pitch(dy);
        camera.RotateY(dx);
    }

    else if ((btnState & MK_RBUTTON) != 0) {
        //每个像素根据鼠标移动距离0.005缩放
        float dx = 0.005f * static_cast<float>(x - lastMousePos.x);
        float dy = 0.005f * static_cast<float>(y - lastMousePos.y);

        //根据输入更新可视半径范围
        camera.Walk(10.0f * dx);
    }

    else if ((btnState & MK_MBUTTON) != 0) {
        //每个像素根据鼠标移动距离0.005缩放
        float dx = 0.005f * static_cast<float>(x - lastMousePos.x);
        float dy = 0.005f * static_cast<float>(y - lastMousePos.y);

        camera.Strafe(10.0f * dy);
    }

    camera.UpdateViewMatrix();

    lastMousePos.x = x;
    lastMousePos.y = y;
}


void STLApp::OnKeyboardInput(const GameTimer& gt) {
    const float dt = gt.DeltaTime();

    if (GetAsyncKeyState('W') & 0x8000)
        camera.Walk(10.0f * dt);

    if (GetAsyncKeyState('S') & 0x8000)
        camera.Walk(-10.0f * dt);

    if (GetAsyncKeyState('A') & 0x8000)
        camera.Strafe(-10.0f * dt);

    if (GetAsyncKeyState('D') & 0x8000)
        camera.Strafe(10.0f * dt);

    camera.UpdateViewMatrix();

}

void STLApp::AnimateMaterials(const GameTimer& gt) {
}

void STLApp::UpdateObjectCBs(const GameTimer& gt) {
    auto currObjectCB = currFrameResource->ObjectCB.get();
    for (auto& e : allRitems) {
        // 只要常量改变就得更新常量缓冲区数据，且要对每个帧资源进行更新
        if (e->NumFramesDirty > 0) {
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform); //下肢属性

            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
            objConstants.MaterialIndex = e->Mat->MatCBIndex;

            currObjectCB->CopyData(e->ObjCBIndex, objConstants);

            //对下一个帧资源也要更新
            e->NumFramesDirty--;
        }
    }
}

void STLApp::UpdateMainPassCB(const GameTimer& gt) {
    XMMATRIX viewMatrix = camera.GetView();
    XMMATRIX projMatrix = camera.GetProj();

    XMMATRIX viewProj = XMMatrixMultiply(viewMatrix, projMatrix);
    XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(viewMatrix), viewMatrix);
    XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(projMatrix), projMatrix);
    XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

    XMStoreFloat4x4(&mainPassCB.View, XMMatrixTranspose(viewMatrix));
    XMStoreFloat4x4(&mainPassCB.InvView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mainPassCB.Proj, XMMatrixTranspose(projMatrix));
    XMStoreFloat4x4(&mainPassCB.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mainPassCB.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));

    mainPassCB.EyePosW = camera.GetPosition3f();
    mainPassCB.RenderTargetSize = XMFLOAT2((float)clientWidth, (float)clientHeight);
    mainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / clientWidth, 1.0f / clientHeight);
    mainPassCB.NearZ = 1.0f;
    mainPassCB.FarZ = 1000.0f;
    mainPassCB.TotalTime = gt.TotalTime();
    mainPassCB.DeltaTime = gt.DeltaTime();

    //灯光属性
    mainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
    mainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
    mainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
    mainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
    mainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
    mainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
    mainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

    auto currPassCB = currFrameResource->PassCB.get();
    currPassCB->CopyData(0, mainPassCB);
}

void STLApp::UpdateMaterialBuffer(const GameTimer& gt) {
    auto currMaterialBuffer = currFrameResource->MaterialBuffer.get();
    for (auto& e : materials){
        // Only update the cbuffer data if the constants have changed.  If the cbuffer
        // data changes, it needs to be updated for each FrameResource.
        Material* mat = e.second.get();
        if (mat->NumFramesDirty > 0){
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

            MaterialData matData;
            matData.DiffuseAlbedo = mat->DiffuseAlbedo;
            matData.FresnelR0 = mat->FresnelR0;
            matData.Roughness = mat->Roughness;
            XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
            matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;

            currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

            // Next FrameResource need to be updated too.
            mat->NumFramesDirty--;
        }
    }
}

void STLApp::LoadTextures(){
    auto defaultDiffuseTex = std::make_unique<Texture>();
    defaultDiffuseTex->Name = "defaultDiffuseTex";
    defaultDiffuseTex->Filename = L"D:/Projects/DX12/DX12/Textures/white1x1.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
        dx12Device.Get(),
        commandList.Get(), 
        defaultDiffuseTex->Filename.c_str(),
        defaultDiffuseTex->Resource, 
        defaultDiffuseTex->UploadHeap)
    );

    textures[defaultDiffuseTex->Name] = std::move(defaultDiffuseTex);
}


void STLApp::BuildRootSignature() {
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0);

    //Shader一般需要资源作为输入,根签名定义了其需要的具体资源
    //如果把Shader看做一个函数,输入资源看做参数,根签名可看做函数签名

    //根参数可以是表格、根描述符或根常量
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

    //创建根cbv
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstantBufferView(1);
    slotRootParameter[2].InitAsShaderResourceView(0, 1);
    slotRootParameter[3].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

    auto staticSamplers = GetStaticSamplers();
    
    //根签名由一组根参数构成
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
        4,
        slotRootParameter,
        (UINT)staticSamplers.size(),
        staticSamplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    );

    //创建一个含一个槽位的根签名(该槽位指向一个仅有单个常量缓冲区组成的描述符区域)
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;

    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(),
        errorBlob.GetAddressOf()
    );

    if (errorBlob != nullptr) {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }

    ThrowIfFailed(hr);

    ThrowIfFailed(dx12Device->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(rootSignature.GetAddressOf()))
    );
}

void STLApp::BuildDescriptorHeaps() {
    //Create the SRV heap.
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 1;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(dx12Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvDescriptorHeap)));

    //Fill out the heap with actual descriptors.
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    auto defaultDiffuseTex = textures["defaultDiffuseTex"]->Resource;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = defaultDiffuseTex->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = defaultDiffuseTex->GetDesc().MipLevels;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    dx12Device->CreateShaderResourceView(defaultDiffuseTex.Get(), &srvDesc, hDescriptor);
}

void STLApp::BuildShadersAndInputLayout() {
    const D3D_SHADER_MACRO alphaTestDefines[] = {
        "ALPHA_TEST", "1", NULL, NULL
    };

    shaders["standardVS"] = dx12Util::CompileShader(L"Light.hlsl", nullptr, "VS", "vs_5_1");
    shaders["opaquePS"] = dx12Util::CompileShader(L"Light.hlsl", nullptr, "PS", "ps_5_1");

    inputLayout = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void STLApp::BuildShapeGeometry() {
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
    GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
    GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

    //将所有的几何体放入一个大的顶点/索引缓冲区，并分为不同的submesh

    //按缓冲区排列顺序计算出各个几何体的submeshGeometry数据索引
    UINT boxVertexOffset = 0;
    UINT gridVertexOffset = (UINT)box.Vertices.size();
    UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
    UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

    UINT boxIndexOffset = 0;
    UINT gridIndexOffset = (UINT)box.Indices32.size();
    UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
    UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

    //构造子几何体的submeshGeometry
    SubmeshGeometry boxSubmesh;
    boxSubmesh.IndexCount = (UINT)box.Indices32.size();
    boxSubmesh.StartIndexLocation = boxIndexOffset;
    boxSubmesh.BaseVertexLocation = boxVertexOffset;

    SubmeshGeometry gridSubmesh;
    gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
    gridSubmesh.StartIndexLocation = gridIndexOffset;
    gridSubmesh.BaseVertexLocation = gridVertexOffset;

    SubmeshGeometry sphereSubmesh;
    sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
    sphereSubmesh.StartIndexLocation = sphereIndexOffset;
    sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

    SubmeshGeometry cylinderSubmesh;
    cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
    cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
    cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

    //合并顶点缓冲区和索引缓冲区 -- 顺序保持一致 
    //合并操作可以通过for循环遍历,或者insert
    auto totalVertexCount =
        box.Vertices.size() +
        grid.Vertices.size() +
        sphere.Vertices.size() +
        cylinder.Vertices.size();

    std::vector<Vertex> vertices(totalVertexCount);

    //按顺序合并顶点缓冲区
    UINT k = 0;
    for (size_t i = 0; i < box.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = box.Vertices[i].Position;
        vertices[k].Normal = box.Vertices[i].Normal;
    }

    for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = grid.Vertices[i].Position;
        vertices[k].Normal = grid.Vertices[i].Normal;
    }

    for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = sphere.Vertices[i].Position;
        vertices[k].Normal = sphere.Vertices[i].Normal;
    }

    for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = cylinder.Vertices[i].Position;
        vertices[k].Normal = cylinder.Vertices[i].Normal;
    }

    //按顺序合并索引缓冲区
    std::vector<std::uint16_t> indices;
    indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
    indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
    indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
    indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

    //将数据分别填入MeshGeometry的CPU和GPU端
    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "shapeGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = dx12Util::CreateDefaultBuffer(
        dx12Device.Get(),
        commandList.Get(),
        vertices.data(),
        vbByteSize,
        geo->VertexBufferUploader
    );

    geo->IndexBufferGPU = dx12Util::CreateDefaultBuffer(
        dx12Device.Get(),
        commandList.Get(),
        indices.data(),
        ibByteSize,
        geo->IndexBufferUploader
    );

    //其他数据
    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    geo->DrawArgs["box"] = boxSubmesh;
    geo->DrawArgs["grid"] = gridSubmesh;
    geo->DrawArgs["sphere"] = sphereSubmesh;
    geo->DrawArgs["cylinder"] = cylinderSubmesh;

    geometries[geo->Name] = std::move(geo);
}

void STLApp::BuildSkullGeometry() {
    std::string read_file_path = "D:/Projects/DX12/DX12/Models/";
    std::string read_file_name = "bunnyhole";

    meshData.ReadSTL(read_file_path, read_file_name);

    meshData.HolesFilling(AREA, REFINEMENT, NO_FAIRING, 1);

    meshData.RepairNormal();

    //meshData.Denoise(20, 0.3);

    //meshData.Smoothing(1, TAN);

    meshData.GetVertexesNormal();

    //meshData.GetSteps(0, 1);

    std::string export_file_path = "D:/Projects/DX12/DX12/Models/";
    std::string export_file_name = "bunnyRepaired";

    meshData.ExportSTL(export_file_path, export_file_name);

    //===================================================================================

    std::vector<std::int32_t> indices;

    for (UINT i = 0; i < meshData.HE_Triangles.size(); ++i) {
        indices.push_back(meshData.HE_Triangles[i]->VertexIndex0);
        indices.push_back(meshData.HE_Triangles[i]->VertexIndex1);
        indices.push_back(meshData.HE_Triangles[i]->VertexIndex2);
    }

    std::vector<Vertex> vertices;

    for (UINT i = 0; i < meshData.HE_Vertexes.size(); ++i) {
        Vertex tempVertex;
        tempVertex.Pos = meshData.HE_Vertexes[i]->Pos;
        tempVertex.Normal = meshData.HE_Vertexes[i]->Normal;
        tempVertex.TexC = { 0.0f, 0.0f };
        vertices.push_back(tempVertex);
    };

    //将所有网格归并进一个索引组
    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "skullGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = dx12Util::CreateDefaultBuffer(dx12Device.Get(),
        commandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = dx12Util::CreateDefaultBuffer(dx12Device.Get(),
        commandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R32_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["skull"] = submesh;

    geometries[geo->Name] = std::move(geo);
}

void STLApp::BuildPSOs() {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

    //PSO for opaque objects.
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaquePsoDesc.InputLayout = { inputLayout.data(), (UINT)inputLayout.size() };
    opaquePsoDesc.pRootSignature = rootSignature.Get();

    opaquePsoDesc.VS = {
        reinterpret_cast<BYTE*>(shaders["standardVS"]->GetBufferPointer()),
        shaders["standardVS"]->GetBufferSize()
    };
    opaquePsoDesc.PS = {
        reinterpret_cast<BYTE*>(shaders["opaquePS"]->GetBufferPointer()),
        shaders["opaquePS"]->GetBufferSize()
    };

    opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaquePsoDesc.SampleMask = UINT_MAX;
    opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    opaquePsoDesc.NumRenderTargets = 1;
    opaquePsoDesc.RTVFormats[0] = backBufferFormat;
    opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    opaquePsoDesc.DSVFormat = depthStencilFormat;

    ThrowIfFailed(dx12Device->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&PSOs["opaque"])));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC wirePsoDesc = opaquePsoDesc;
    wirePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    wirePsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    // Standard transparency blending.
    D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
    transparencyBlendDesc.BlendEnable = true;
    transparencyBlendDesc.LogicOpEnable = false;
    transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
    transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
    transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
    transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    wirePsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;

    ThrowIfFailed(dx12Device->CreateGraphicsPipelineState(&wirePsoDesc, IID_PPV_ARGS(&PSOs["wire"])));
}

void STLApp::BuildFrameResources() {
    //实例化一个由3个帧资源所构成的向量,并留有特定的成员变量来记录当前的帧资源：
    for (int i = 0; i < gNumFrameResources; ++i) {
        frameResources.push_back(std::make_unique<FrameResource>(dx12Device.Get(), 1, (UINT)allRitems.size(), (UINT)materials.size()));
    }
}

void STLApp::BuildMaterials() {
    auto wireMat = std::make_unique<Material>();
    wireMat->Name = "wireMat";
    wireMat->MatCBIndex = 0;
    wireMat->DiffuseSrvHeapIndex = 0;
    wireMat->DiffuseAlbedo = XMFLOAT4(Colors::Gray);
    wireMat->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    wireMat->Roughness = 0.1f;

    auto skullMat = std::make_unique<Material>();
    skullMat->Name = "skullMat";
    skullMat->MatCBIndex = 1;
    skullMat->DiffuseSrvHeapIndex = 0;
    skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05);
    skullMat->Roughness = 0.0f;

    materials["wireMat"] = std::move(wireMat);
    materials["skullMat"] = std::move(skullMat);
}

void STLApp::BuildRenderItems() {
    auto skullRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&skullRitem->World, XMMatrixScaling(15.0f, 15.0f, 15.0f) * XMMatrixTranslation(0.0f, -1.5f, 0.0f));
    XMStoreFloat4x4(&skullRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
    skullRitem->ObjCBIndex = 0;
    skullRitem->Mat = materials["skullMat"].get();
    skullRitem->Geo = geometries["skullGeo"].get();
    skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
    skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
    skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
    rItemLayer[(int)RenderLayer::Opaque].push_back(skullRitem.get());

    auto wireRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&wireRitem->World, XMMatrixScaling(15.01f, 15.01f, 15.01f) * XMMatrixTranslation(0.0f, -1.5f, 0.0f));
    XMStoreFloat4x4(&wireRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
    wireRitem->ObjCBIndex = 1;
    wireRitem->Mat = materials["wireMat"].get();
    wireRitem->Geo = geometries["skullGeo"].get();
    wireRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    wireRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
    wireRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
    wireRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
    rItemLayer[(int)RenderLayer::Wire].push_back(wireRitem.get());

    allRitems.push_back(std::move(skullRitem));
    allRitems.push_back(std::move(wireRitem));
}


void STLApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems) {
    UINT objCBByteSize = dx12Util::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    auto objectCB = currFrameResource->ObjectCB->Resource();

    //针对每个渲染项
    for (size_t i = 0; i < ritems.size(); ++i) {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        //计算常量数据的虚拟地址
        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;

        //与根描述符绑定
        cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> STLApp::GetStaticSamplers(){
    // Applications usually only need a handful of samplers.  So just define them all up front
    // and keep them available as part of the root signature.  

    const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        1, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        2, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        3, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
        4, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
        0.0f,                             // mipLODBias
        8);                               // maxAnisotropy

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
        5, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
        0.0f,                              // mipLODBias
        8);                                // maxAnisotropy

    return {
        pointWrap, pointClamp,
        linearWrap, linearClamp,
        anisotropicWrap, anisotropicClamp };
}