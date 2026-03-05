#pragma once

#include <d3d12.h>
#include <dxgi.h>
#include <windows.h>
#include <wrl.h>

struct ImGui_ImplDX12_InitInfo;

namespace GUI {
struct ImGuiLayerInitInfo {
    HWND WindowHandle = nullptr;
    ID3D12Device* Device = nullptr;
    ID3D12CommandQueue* CommandQueue = nullptr;
    int NumFramesInFlight = 0;
    DXGI_FORMAT RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
};

class ImGuiLayer {
public:
    void Initialize(const ImGuiLayerInitInfo& initInfo);
    void BeginFrame();
    void Render(ID3D12GraphicsCommandList* commandList);
    void Shutdown();

    bool HandleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) const;
    bool WantsCaptureMouse() const;
    bool WantsCaptureKeyboard() const;
    bool IsInitialized() const;

private:
    static void AllocateSrvDescriptor(
        ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE* outCpuDescHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE* outGpuDescHandle
    );
    static void FreeSrvDescriptor(
        ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE cpuDescHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandle
    );

private:
    bool initialized = false;
    bool fontDescriptorAllocated = false;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> fontSrvHeap = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE fontSrvCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE fontSrvGpuHandle{};
};
} // namespace GUI
