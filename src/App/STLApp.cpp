#include "STLApp.h"
#include "MeshCore/Polycube.h"

#include <commdlg.h>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

const int gNumFrameResources = 3;

namespace {
std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::string HrToHex(HRESULT hr) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << static_cast<unsigned int>(hr);
    return oss.str();
}

std::string EnsureTrailingSeparator(std::string directory) {
    if (!directory.empty() && directory.back() != '\\' && directory.back() != '/') {
        directory.push_back(std::filesystem::path::preferred_separator);
    }
    return directory;
}

std::string BuildModelPath(const std::string& directory, const std::string& fileStem) {
    return EnsureTrailingSeparator(directory) + fileStem + ".stl";
}
}

const char* STLApp::WorkerTaskName(WorkerTaskKind kind) {
    switch (kind) {
    case WorkerTaskKind::RepairNormal: return "RepairNormal";
    case WorkerTaskKind::HolesFilling: return "HolesFilling";
    case WorkerTaskKind::Denoise: return "Denoise";
    case WorkerTaskKind::Smoothing: return "Smoothing";
    case WorkerTaskKind::ApplyAll: return "ApplyAll";
    case WorkerTaskKind::Reset: return "Reset";
    case WorkerTaskKind::Polycube: return "Polycube";
    default: return "None";
    }
}

METHOD STLApp::ToMeshMethod(GUI::HoleMethodOption option) {
    return option == GUI::HoleMethodOption::Angle ? ANGLE : AREA;
}

RMETHOD STLApp::ToMeshRefinement(GUI::RefinementOption option) {
    return option == GUI::RefinementOption::Refinement ? REFINEMENT : NO_REFINEMENT;
}

FMETHOD STLApp::ToMeshFairing(GUI::FairingOption option) {
    switch (option) {
    case GUI::FairingOption::Uniform: return UNIFORM;
    case GUI::FairingOption::Scale: return SCALE;
    case GUI::FairingOption::Tan: return TAN;
    case GUI::FairingOption::Cot: return COT;
    default: return NO_FAIRING;
    }
}

const char* STLApp::ToPolycubeStageLabel(PolycubeStage stage) {
    switch (stage) {
    case PolycubeStage::Preprocess: return "Preprocess";
    case PolycubeStage::Tetrahedralize: return "Tetrahedralize";
    case PolycubeStage::Optimize: return "Optimize";
    case PolycubeStage::Cleanup: return "Cleanup";
    case PolycubeStage::Completed: return "Completed";
    default: return "None";
    }
}

const char* STLApp::ToPolycubePreviewSourceLabel(PolycubePreviewSource previewSource) {
    switch (previewSource) {
    case PolycubePreviewSource::TetrahedralBoundary: return "Tet Boundary";
    case PolycubePreviewSource::AcceptedOptimizationStep: return "Accepted Optimization Step";
    case PolycubePreviewSource::CleanupResult: return "Cleanup Result";
    default: return "None";
    }
}

STLApp::STLApp(HINSTANCE hInstace) : D3DApp(hInstace) {
}

STLApp::~STLApp() {
    StopWorkerThread();
    imguiLayer.Shutdown();

    if (dx12Device != nullptr)
        FlushCommandQueue();
}


bool STLApp::Init() {
    if (!D3DApp::Init()) {
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
    GUI::ImGuiLayerInitInfo imguiInitInfo;
    imguiInitInfo.WindowHandle = hMainWnd;
    imguiInitInfo.Device = dx12Device.Get();
    imguiInitInfo.CommandQueue = commandQueue.Get();
    imguiInitInfo.NumFramesInFlight = gNumFrameResources;
    imguiInitInfo.RTVFormat = backBufferFormat;
    imguiInitInfo.DSVFormat = depthStencilFormat;
    imguiLayer.Initialize(imguiInitInfo);

    //初始化命令
    ThrowIfFailed(commandList->Close());
    ID3D12CommandList* cmdLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

    //等待初始化完成
    FlushCommandQueue();

    StartWorkerThread();

    return true;
}

LRESULT STLApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (imguiLayer.HandleWndProc(hwnd, msg, wParam, lParam)) {
        return 1;
    }

    return D3DApp::MsgProc(hwnd, msg, wParam, lParam);
}

void STLApp::OnResize() {
    D3DApp::OnResize();

    //调整窗口尺寸则需更新横纵比并重新计算投影矩阵
    camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void STLApp::Update(const GameTimer& gt) {
    PumpWorkerResults();
    HandlePendingModelDialog();
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

    ApplyPendingGpuBuffers();

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

    imguiLayer.BeginFrame();
    GUI::PanelActions panelActions;
    panels.Draw(meshInfoState, repairPanelState, polycubePanelState, polycubeInfoState, panelActions);
    ProcessPanelActions(panelActions);
    imguiLayer.Render(commandList.Get());

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
    if (imguiLayer.WantsCaptureMouse()) {
        return;
    }

    lastMousePos.x = x;
    lastMousePos.y = y;

    SetCapture(hMainWnd);

}

void STLApp::OnMouseUp(WPARAM btnState, int x, int y) {
    if (imguiLayer.WantsCaptureMouse()) {
        return;
    }

    ReleaseCapture();
}

void STLApp::OnMouseMove(WPARAM btnState, int x, int y) {
    if (imguiLayer.WantsCaptureMouse()) {
        return;
    }

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
    if (imguiLayer.WantsCaptureKeyboard()) {
        return;
    }

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
            matData.UseDiffuseTexture = mat->UseTexture ? 1u : 0u;

            currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

            // Next FrameResource need to be updated too.
            mat->NumFramesDirty--;
        }
    }
}

void STLApp::LoadTextures(){
    auto defaultDiffuseTex = std::make_unique<Texture>();
    defaultDiffuseTex->Name = "defaultDiffuseTex";
    defaultDiffuseTex->Filename = L"assets/Textures/white1x1.dds";

    if (std::filesystem::exists(defaultDiffuseTex->Filename)) {
        ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
            dx12Device.Get(),
            commandList.Get(),
            defaultDiffuseTex->Filename.c_str(),
            defaultDiffuseTex->Resource,
            defaultDiffuseTex->UploadHeap)
        );
    }
    else {
        OutputDebugStringA("defaultDiffuseTex not found, using material color only.\n");
    }

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
    srvDesc.Format = defaultDiffuseTex ? defaultDiffuseTex->GetDesc().Format : DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = defaultDiffuseTex ? defaultDiffuseTex->GetDesc().MipLevels : 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    dx12Device->CreateShaderResourceView(defaultDiffuseTex.Get(), &srvDesc, hDescriptor);
}

void STLApp::BuildShadersAndInputLayout() {
    const D3D_SHADER_MACRO alphaTestDefines[] = {
        "ALPHA_TEST", "1", NULL, NULL
    };

    shaders["standardVS"] = dx12Util::CompileShader(L"assets/Shaders/Light.hlsl", nullptr, "VS", "vs_5_1");
    shaders["opaquePS"] = dx12Util::CompileShader(L"assets/Shaders/Light.hlsl", nullptr, "PS", "ps_5_1");

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

    std::vector<RenderVertex> vertices(totalVertexCount);

    //按顺序合并顶点缓冲区
    UINT k = 0;
    for (size_t i = 0; i < box.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = box.Vertices[i].Position;
        vertices[k].Normal = box.Vertices[i].Normal;
        vertices[k].TexC = box.Vertices[i].TexC;
    }

    for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = grid.Vertices[i].Position;
        vertices[k].Normal = grid.Vertices[i].Normal;
        vertices[k].TexC = grid.Vertices[i].TexC;
    }

    for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = sphere.Vertices[i].Position;
        vertices[k].Normal = sphere.Vertices[i].Normal;
        vertices[k].TexC = sphere.Vertices[i].TexC;
    }

    for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = cylinder.Vertices[i].Position;
        vertices[k].Normal = cylinder.Vertices[i].Normal;
        vertices[k].TexC = cylinder.Vertices[i].TexC;
    }

    //按顺序合并索引缓冲区
    std::vector<std::uint16_t> indices;
    indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
    indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
    indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
    indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

    //将数据分别填入MeshGeometry的CPU和GPU端
    const UINT vbByteSize = (UINT)vertices.size() * sizeof(RenderVertex);
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
    geo->VertexByteStride = sizeof(RenderVertex);
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
    displayMeshData.ReadSTL(modelDirectory, sourceModelName);
    displayMeshData.GetVertexesNormal();
    UpdateMeshInfo(displayMeshData);

    currentModelPath = BuildModelPath(modelDirectory, sourceModelName);
    meshInfoState.ModelPath = currentModelPath;
    meshInfoState.Status = "Loaded source mesh.";
    meshInfoState.ActiveTask = "None";
    polycubeInfoState = {};

    const MeshRendererBuffers meshBuffers = MeshRenderer::BuildFromHalfEdge(displayMeshData);
    geometries["skullGeo"] = MeshRenderer::CreateMeshGeometry(
        "skullGeo",
        "skull",
        meshBuffers,
        dx12Device.Get(),
        commandList.Get()
    );
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
    wireMat->UseTexture = false;

    auto skullMat = std::make_unique<Material>();
    skullMat->Name = "skullMat";
    skullMat->MatCBIndex = 1;
    skullMat->DiffuseSrvHeapIndex = 0;
    skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05);
    skullMat->Roughness = 0.0f;
    skullMat->UseTexture = false;

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

    UpdateRenderItemsForSkull();
}

void STLApp::StartWorkerThread() {
    workerStop = false;
    workerThread = std::thread(&STLApp::WorkerLoop, this);
}

void STLApp::StopWorkerThread() {
    {
        std::lock_guard<std::mutex> lock(workerMutex);
        workerStop = true;
        pendingWorkerTask.reset();
    }
    workerCv.notify_all();

    if (workerThread.joinable()) {
        workerThread.join();
    }
}

void STLApp::WorkerLoop() {
    HE_MeshData workerMesh;
    bool meshLoaded = false;
    Polycube polycube;
    std::string workerModelDirectory;
    std::string workerSourceModelName;

    while (true) {
        WorkerTask task;
        {
            std::unique_lock<std::mutex> lock(workerMutex);
            workerCv.wait(lock, [this] { return workerStop || pendingWorkerTask.has_value(); });
            if (workerStop) {
                break;
            }

            task = *pendingWorkerTask;
            pendingWorkerTask.reset();
            workerBusy = true;
            workerRunningTask = task.Kind;
            workerRunningSequence = task.Sequence;
            workerProgressState = WorkerProgress{
                task.Kind,
                task.Sequence,
                task.Kind == WorkerTaskKind::Polycube ? PolycubeStage::Preprocess : PolycubeStage::None,
                PolycubePreviewSource::None,
                "Running.",
                {}
            };
        }

        WorkerResult result;
        result.Kind = task.Kind;
        result.Sequence = task.Sequence;

        if (workerModelDirectory != task.ModelDirectory || workerSourceModelName != task.SourceModelName) {
            workerModelDirectory = task.ModelDirectory;
            workerSourceModelName = task.SourceModelName;
            meshLoaded = false;
        }

        auto ensureLoaded = [&]() {
            if (!meshLoaded) {
                workerMesh.ReadSTL(workerModelDirectory, workerSourceModelName);
                meshLoaded = true;
            }
        };

        try {
            if (task.Kind == WorkerTaskKind::Reset) {
                workerMesh.ReadSTL(workerModelDirectory, workerSourceModelName);
                meshLoaded = true;
                workerMesh.GetVertexesNormal();
                result.Message = "Reset completed.";
            } else if (task.Kind == WorkerTaskKind::RepairNormal) {
                ensureLoaded();
                workerMesh.RepairNormal();
                workerMesh.GetVertexesNormal();
                result.Message = "RepairNormal completed.";
            } else if (task.Kind == WorkerTaskKind::HolesFilling) {
                ensureLoaded();
                workerMesh.HolesFilling(
                    ToMeshMethod(task.RepairState.HoleMethod),
                    ToMeshRefinement(task.RepairState.HoleRefinement),
                    ToMeshFairing(task.RepairState.HoleFairing),
                    task.RepairState.HoleFairingIterations
                );
                workerMesh.RepairNormal();
                workerMesh.GetVertexesNormal();
                result.Message = "HolesFilling completed.";
            } else if (task.Kind == WorkerTaskKind::Denoise) {
                ensureLoaded();
                workerMesh.Denoise(task.RepairState.DenoiseIterations, task.RepairState.DenoiseT);
                workerMesh.GetVertexesNormal();
                result.Message = "Denoise completed.";
            } else if (task.Kind == WorkerTaskKind::Smoothing) {
                ensureLoaded();
                workerMesh.Smoothing(
                    task.RepairState.SmoothingIterations,
                    ToMeshFairing(task.RepairState.SmoothingFairing)
                );
                workerMesh.GetVertexesNormal();
                result.Message = "Smoothing completed.";
            } else if (task.Kind == WorkerTaskKind::ApplyAll) {
                ensureLoaded();
                workerMesh.HolesFilling(
                    ToMeshMethod(task.RepairState.HoleMethod),
                    ToMeshRefinement(task.RepairState.HoleRefinement),
                    ToMeshFairing(task.RepairState.HoleFairing),
                    task.RepairState.HoleFairingIterations
                );
                workerMesh.RepairNormal();
                workerMesh.Denoise(task.RepairState.DenoiseIterations, task.RepairState.DenoiseT);
                workerMesh.Smoothing(
                    task.RepairState.SmoothingIterations,
                    ToMeshFairing(task.RepairState.SmoothingFairing)
                );
                workerMesh.GetVertexesNormal();
                result.Message = "Apply All completed.";
            } else if (task.Kind == WorkerTaskKind::Polycube) {
                ensureLoaded();

                PolycubeOptions options;
                options.TetSpacingScale = task.PolycubeState.TetSpacingScale;
                options.InitialAlpha = task.PolycubeState.InitialAlpha;
                options.ComplexityWeight = task.PolycubeState.ComplexityWeight;
                options.AlphaMultiplier = task.PolycubeState.AlphaMultiplier;
                options.InitialEpsilon = task.PolycubeState.InitialEpsilon;
                options.EpsilonDecay = task.PolycubeState.EpsilonDecay;
                options.MinEpsilon = task.PolycubeState.MinEpsilon;
                options.TargetNormalizedError = task.PolycubeState.TargetNormalizedError;
                options.MaxOuterStages = task.PolycubeState.MaxOuterStages;
                options.MaxInnerIterations = task.PolycubeState.MaxInnerIterations;
                options.ProgressCallback = [this, sequence = task.Sequence](const PolycubeProgress& progress) {
                    std::lock_guard<std::mutex> lock(workerMutex);
                    if (!workerBusy || workerRunningTask != WorkerTaskKind::Polycube || workerRunningSequence != sequence) {
                        return;
                    }
                    workerProgressState = WorkerProgress{
                        WorkerTaskKind::Polycube,
                        sequence,
                        progress.StageReached,
                        progress.PreviewSource,
                        progress.Status,
                        progress.Stats
                    };
                };

                PolycubeResult polycubeResult = polycube.Generate(workerMesh, options);
                result.StageReached = polycubeResult.StageReached;
                result.PreviewSource = polycubeResult.PreviewSource;
                result.Summary = polycubeResult.Summary;
                result.Stats = polycubeResult.Stats;
                if (!polycubeResult.Ok) {
                    result.Succeeded = false;
                    result.Message = polycubeResult.Error.empty() ? "Polycube failed." : polycubeResult.Error;
                    result.HasUpdatedMesh = polycubeResult.PreviewApplied;
                } else {
                    result.Succeeded = true;
                    result.Message = polycubeResult.Summary.empty() ? "Polycube completed." : polycubeResult.Summary;
                    result.Succeeded = true;
                    result.HasUpdatedMesh = true;
                }

                if (polycubeResult.Ok || polycubeResult.PreviewApplied) {
                    workerMesh = std::move(polycubeResult.BoundaryPreviewMesh);
                }
            }

            if (result.Kind != WorkerTaskKind::Polycube || result.Succeeded || result.HasUpdatedMesh) {
                result.Buffers = MeshRenderer::BuildFromHalfEdge(workerMesh);
                result.VertexCount = static_cast<std::uint32_t>(workerMesh.HE_Vertexes.size());
                result.TriangleCount = static_cast<std::uint32_t>(workerMesh.HE_Triangles.size());
                result.HasUpdatedMesh = true;
                if (result.Kind != WorkerTaskKind::Polycube || result.Succeeded) {
                    result.Succeeded = true;
                }
            }
        } catch (const std::exception& e) {
            result.Succeeded = false;
            result.Message = e.what();
        }

        {
            std::lock_guard<std::mutex> lock(workerMutex);
            workerBusy = false;
            workerRunningTask = WorkerTaskKind::None;
            workerRunningSequence = 0;
            workerProgressState.reset();
            completedWorkerResult = std::move(result);
        }
    }
}

void STLApp::QueueWorkerTask(WorkerTaskKind kind) {
    WorkerTask task;
    task.Kind = kind;
    task.RepairState = repairPanelState;
    task.PolycubeState = polycubePanelState;
    task.ModelDirectory = modelDirectory;
    task.SourceModelName = sourceModelName;

    bool hasRunningTask = false;
    {
        std::lock_guard<std::mutex> lock(workerMutex);
        hasRunningTask = workerBusy || pendingWorkerTask.has_value();
        task.Sequence = ++taskSequence;
        pendingWorkerTask = std::move(task);
    }

    workerCv.notify_one();

    meshInfoState.Busy = true;
    meshInfoState.ActiveTask = WorkerTaskName(kind);
    meshInfoState.Status = hasRunningTask ? "Queued (latest request kept)." : "Queued.";
    if (kind == WorkerTaskKind::Polycube) {
        polycubeInfoState = {};
        polycubeInfoState.Status = "Queued";
        polycubeInfoState.Stage = "Queued";
    }
}

void STLApp::PumpWorkerResults() {
    std::optional<WorkerResult> result;
    std::optional<WorkerProgress> progress;
    bool hasPendingTask = false;
    bool isBusy = false;
    WorkerTaskKind pendingKind = WorkerTaskKind::None;
    WorkerTaskKind runningKind = WorkerTaskKind::None;

    {
        std::lock_guard<std::mutex> lock(workerMutex);
        hasPendingTask = pendingWorkerTask.has_value();
        if (hasPendingTask) {
            pendingKind = pendingWorkerTask->Kind;
        }
        runningKind = workerRunningTask;
        isBusy = workerBusy || hasPendingTask;
        if (workerProgressState.has_value()) {
            progress = workerProgressState;
        }
        if (completedWorkerResult.has_value()) {
            result = std::move(completedWorkerResult);
            completedWorkerResult.reset();
        }
    }

    meshInfoState.Busy = isBusy;
    if (runningKind != WorkerTaskKind::None) {
        meshInfoState.ActiveTask = WorkerTaskName(runningKind);
    } else if (isBusy && hasPendingTask) {
        meshInfoState.ActiveTask = WorkerTaskName(pendingKind);
    } else if (!isBusy) {
        meshInfoState.ActiveTask = "None";
    }

    if (!result.has_value() && progress.has_value()) {
        meshInfoState.Status = progress->Message;
        if (progress->Kind == WorkerTaskKind::Polycube) {
            polycubeInfoState.Status = progress->Message;
            polycubeInfoState.Stage = ToPolycubeStageLabel(progress->StageReached);
            polycubeInfoState.PreviewSource = ToPolycubePreviewSourceLabel(progress->PreviewSource);
            polycubeInfoState.TetCount = progress->Stats.TetCount;
            polycubeInfoState.BoundaryFaceCount = progress->Stats.BoundaryFaceCount;
            polycubeInfoState.OuterStages = progress->Stats.OuterStages;
            polycubeInfoState.InnerIterations = progress->Stats.InnerIterations;
            polycubeInfoState.InitialPatchCount = progress->Stats.InitialPatchCount;
            polycubeInfoState.FinalPatchCount = progress->Stats.FinalPatchCount;
            polycubeInfoState.NormalizedError = progress->Stats.NormalizedError;
            polycubeInfoState.AreaDrift = progress->Stats.AreaDrift;
            polycubeInfoState.MinTetVolume = progress->Stats.MinTetVolume;
        }
    }

    if (!result.has_value()) {
        return;
    }

    if (result->HasUpdatedMesh) {
        pendingGpuBuffers = std::move(result->Buffers);
        meshInfoState.VertexCount = static_cast<int>(result->VertexCount);
        meshInfoState.TriangleCount = static_cast<int>(result->TriangleCount);
    }

    if (result->Succeeded) {
        meshInfoState.Status = result->Message;

        if (result->Kind == WorkerTaskKind::Reset) {
            polycubeInfoState = {};
        }

        if (result->Kind == WorkerTaskKind::Polycube) {
            polycubeInfoState.Status = "Success";
            polycubeInfoState.Summary = result->Summary;
            polycubeInfoState.Stage = ToPolycubeStageLabel(result->StageReached);
            polycubeInfoState.PreviewSource = ToPolycubePreviewSourceLabel(result->PreviewSource);
            polycubeInfoState.TetCount = result->Stats.TetCount;
            polycubeInfoState.BoundaryFaceCount = result->Stats.BoundaryFaceCount;
            polycubeInfoState.OuterStages = result->Stats.OuterStages;
            polycubeInfoState.InnerIterations = result->Stats.InnerIterations;
            polycubeInfoState.InitialPatchCount = result->Stats.InitialPatchCount;
            polycubeInfoState.FinalPatchCount = result->Stats.FinalPatchCount;
            polycubeInfoState.NormalizedError = result->Stats.NormalizedError;
            polycubeInfoState.AreaDrift = result->Stats.AreaDrift;
            polycubeInfoState.MinTetVolume = result->Stats.MinTetVolume;
        }
    } else {
        meshInfoState.Status = "Failed: " + result->Message;
        if (result->Kind == WorkerTaskKind::Polycube) {
            polycubeInfoState.Summary = result->Summary;
            polycubeInfoState.Stage = ToPolycubeStageLabel(result->StageReached);
            polycubeInfoState.PreviewSource = ToPolycubePreviewSourceLabel(result->PreviewSource);
            polycubeInfoState.TetCount = result->Stats.TetCount;
            polycubeInfoState.BoundaryFaceCount = result->Stats.BoundaryFaceCount;
            polycubeInfoState.OuterStages = result->Stats.OuterStages;
            polycubeInfoState.InnerIterations = result->Stats.InnerIterations;
            polycubeInfoState.InitialPatchCount = result->Stats.InitialPatchCount;
            polycubeInfoState.FinalPatchCount = result->Stats.FinalPatchCount;
            polycubeInfoState.NormalizedError = result->Stats.NormalizedError;
            polycubeInfoState.AreaDrift = result->Stats.AreaDrift;
            polycubeInfoState.MinTetVolume = result->Stats.MinTetVolume;
            polycubeInfoState.Status = "Failed: " + result->Message;
        }
    }
}

void STLApp::ProcessPanelActions(const GUI::PanelActions& actions) {
    if (actions.RequestOpenModel) {
        if (meshInfoState.Busy) {
            meshInfoState.Status = "Wait for the current background task to finish before loading another model.";
            return;
        }
        openModelDialogRequested = true;
        return;
    }
    if (actions.RequestReset) {
        QueueWorkerTask(WorkerTaskKind::Reset);
        return;
    }
    if (actions.RequestApplyAll) {
        QueueWorkerTask(WorkerTaskKind::ApplyAll);
        return;
    }
    if (actions.RequestRepairNormal) {
        QueueWorkerTask(WorkerTaskKind::RepairNormal);
        return;
    }
    if (actions.RequestHolesFilling) {
        QueueWorkerTask(WorkerTaskKind::HolesFilling);
        return;
    }
    if (actions.RequestDenoise) {
        QueueWorkerTask(WorkerTaskKind::Denoise);
        return;
    }
    if (actions.RequestSmoothing) {
        QueueWorkerTask(WorkerTaskKind::Smoothing);
        return;
    }
    if (actions.RequestPolycube) {
        QueueWorkerTask(WorkerTaskKind::Polycube);
        return;
    }
}

void STLApp::HandlePendingModelDialog() {
    if (!openModelDialogRequested) {
        return;
    }

    openModelDialogRequested = false;

    std::array<char, 4096> fileBuffer = {};
    std::filesystem::path initialDirectory = std::filesystem::path(currentModelPath).parent_path();
    const std::string initialDirectoryString = initialDirectory.empty() ? std::string() : initialDirectory.string();

    OPENFILENAMEA dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hMainWnd;
    dialog.lpstrFilter = "STL Files (*.stl)\0*.stl\0All Files (*.*)\0*.*\0";
    dialog.lpstrFile = fileBuffer.data();
    dialog.nMaxFile = static_cast<DWORD>(fileBuffer.size());
    dialog.lpstrInitialDir = initialDirectoryString.empty() ? nullptr : initialDirectoryString.c_str();
    dialog.lpstrDefExt = "stl";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (!GetOpenFileNameA(&dialog)) {
        const DWORD error = CommDlgExtendedError();
        if (error != 0) {
            meshInfoState.Status = "Open model dialog failed: " + std::to_string(error);
        }
        return;
    }

    QueueModelLoadFromPath(std::filesystem::path(fileBuffer.data()));
}

void STLApp::QueueModelLoadFromPath(const std::filesystem::path& modelPath) {
    if (modelPath.empty()) {
        meshInfoState.Status = "No model selected.";
        return;
    }
    if (!std::filesystem::exists(modelPath)) {
        meshInfoState.Status = "Selected model does not exist.";
        return;
    }

    std::string extension = modelPath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (extension != ".stl") {
        meshInfoState.Status = "Only .stl files are supported.";
        return;
    }

    modelDirectory = EnsureTrailingSeparator(modelPath.parent_path().string());
    sourceModelName = modelPath.stem().string();
    currentModelPath = modelPath.lexically_normal().string();
    meshInfoState.ModelPath = currentModelPath;
    polycubeInfoState = {};

    QueueWorkerTask(WorkerTaskKind::Reset);
}

void STLApp::ApplyPendingGpuBuffers() {
    if (!pendingGpuBuffers.has_value()) {
        return;
    }

    FlushCommandQueue();

    auto geoIter = geometries.find("skullGeo");
    if (geoIter == geometries.end()) {
        pendingGpuBuffers.reset();
        return;
    }

    try {
        MeshRenderer::UpdateMeshGeometry(
            *geoIter->second,
            "skull",
            *pendingGpuBuffers,
            dx12Device.Get(),
            commandList.Get()
        );
    } catch (const DxException& e) {
        HRESULT removedReason = S_OK;
        if (dx12Device != nullptr) {
            removedReason = dx12Device->GetDeviceRemovedReason();
        }
        meshInfoState.Status = std::string("GPU update failed: ") + WideToUtf8(e.ToString());
        if (FAILED(removedReason)) {
            meshInfoState.Status += " RemovedReason=" + HrToHex(removedReason);
        }
        pendingGpuBuffers.reset();
        return;
    } catch (const std::exception& e) {
        meshInfoState.Status = std::string("GPU update failed: ") + e.what();
        pendingGpuBuffers.reset();
        return;
    }

    UpdateRenderItemsForSkull();
    for (auto& item : allRitems) {
        item->NumFramesDirty = gNumFrameResources;
    }

    pendingGpuBuffers.reset();
}

void STLApp::UpdateRenderItemsForSkull() {
    auto geoIter = geometries.find("skullGeo");
    if (geoIter == geometries.end()) {
        return;
    }

    MeshGeometry* skullGeo = geoIter->second.get();
    auto drawArgIter = skullGeo->DrawArgs.find("skull");
    if (drawArgIter == skullGeo->DrawArgs.end()) {
        return;
    }

    const SubmeshGeometry& skullSubmesh = drawArgIter->second;
    for (auto& item : allRitems) {
        item->Geo = skullGeo;
        item->IndexCount = skullSubmesh.IndexCount;
        item->StartIndexLocation = skullSubmesh.StartIndexLocation;
        item->BaseVertexLocation = skullSubmesh.BaseVertexLocation;
    }
}

void STLApp::UpdateMeshInfo(const HE_MeshData& meshData) {
    meshInfoState.VertexCount = static_cast<int>(meshData.HE_Vertexes.size());
    meshInfoState.TriangleCount = static_cast<int>(meshData.HE_Triangles.size());
}


void STLApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems) {
    UINT objCBByteSize = dx12Util::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    auto objectCB = currFrameResource->ObjectCB->Resource();

    //针对每个渲染项
    for (size_t i = 0; i < ritems.size(); ++i) {
        auto ri = ritems[i];
        if (ri == nullptr || ri->Geo == nullptr) {
            continue;
        }
        if (ri->IndexCount == 0) {
            continue;
        }

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


