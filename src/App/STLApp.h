#pragma once

#include "DX12Engine/Camera.h"
#include "DX12Engine/D3DApp.h"
#include "DX12Engine/FrameResource.h"
#include "DX12Engine/GeometryGenerator.h"
#include "GUI/ImGuiLayer.h"
#include "GUI/Panels.h"
#include "MeshRenderer.h"
#include "MeshCore/HalfEdgeMesh.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct RenderItem {
    RenderItem() = default;
    RenderItem(const RenderItem& rhs) = delete;

    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

    int NumFramesDirty = gNumFrameResources;
    UINT ObjCBIndex = -1;

    Material* Mat = nullptr;
    MeshGeometry* Geo = nullptr;

    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

enum class RenderLayer : int {
    Opaque = 0,
    Wire,
    Count
};

class STLApp : public D3DApp {
private:
    enum class WorkerTaskKind {
        None = 0,
        RepairNormal,
        HolesFilling,
        Denoise,
        Smoothing,
        ApplyAll,
        Reset,
        Polycube
    };

    struct WorkerTask {
        WorkerTaskKind Kind = WorkerTaskKind::None;
        GUI::RepairPanelState RepairState;
        GUI::PolycubePanelState PolycubeState;
        std::uint64_t Sequence = 0;
    };

    struct WorkerResult {
        WorkerTaskKind Kind = WorkerTaskKind::None;
        std::uint64_t Sequence = 0;
        bool Succeeded = false;
        std::string Message;
        MeshRendererBuffers Buffers;
        std::uint32_t VertexCount = 0;
        std::uint32_t TriangleCount = 0;
    };

    HE_MeshData displayMeshData;
    std::string modelDirectory = "assets/Models/";
    std::string sourceModelName = "bunnyhole";

    GUI::ImGuiLayer imguiLayer;
    GUI::Panels panels;
    GUI::MeshInfoState meshInfoState;
    GUI::RepairPanelState repairPanelState;
    GUI::PolycubePanelState polycubePanelState;
    std::string polycubeStatus = "Idle";

    std::thread workerThread;
    std::mutex workerMutex;
    std::condition_variable workerCv;
    bool workerStop = false;
    bool workerBusy = false;
    std::uint64_t taskSequence = 0;
    std::optional<WorkerTask> pendingWorkerTask;
    std::optional<WorkerResult> completedWorkerResult;
    std::optional<MeshRendererBuffers> pendingGpuBuffers;

    std::vector<std::unique_ptr<FrameResource>> frameResources;
    FrameResource* currFrameResource = nullptr;
    int currFrameResourceIndex = 0;

    UINT cbvSrvDescriptorSize = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> geometries;
    std::unordered_map<std::string, std::unique_ptr<Material>> materials;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> shaders;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> PSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;
    std::vector<std::unique_ptr<RenderItem>> allRitems;
    std::vector<RenderItem*> rItemLayer[(int)RenderLayer::Count];

    PassConstants mainPassCB;
    Camera camera;
    POINT lastMousePos;

public:
    STLApp(HINSTANCE hInstance);
    STLApp(const STLApp& rhs) = delete;
    STLApp& operator=(const STLApp& rhs) = delete;
    ~STLApp();

    bool Init() override;
    LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    void OnResize() override;
    void Update(const GameTimer& gt) override;
    void Draw(const GameTimer& gt) override;

    void OnMouseDown(WPARAM btnState, int x, int y) override;
    void OnMouseUp(WPARAM btnState, int x, int y) override;
    void OnMouseMove(WPARAM btnState, int x, int y) override;

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

    void StartWorkerThread();
    void StopWorkerThread();
    void WorkerLoop();
    void QueueWorkerTask(WorkerTaskKind kind);
    void PumpWorkerResults();
    void ProcessPanelActions(const GUI::PanelActions& actions);
    void ApplyPendingGpuBuffers();
    void UpdateRenderItemsForSkull();
    void UpdateMeshInfo(const HE_MeshData& meshData);

    static const char* WorkerTaskName(WorkerTaskKind kind);
    static METHOD ToMeshMethod(GUI::HoleMethodOption option);
    static RMETHOD ToMeshRefinement(GUI::RefinementOption option);
    static FMETHOD ToMeshFairing(GUI::FairingOption option);

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();
};
