#include "GUI/ImGuiLayer.h"

#include "DX12Engine/D3DUtil.h"

#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

#include <stdexcept>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
);

namespace GUI {
void ImGuiLayer::Initialize(const ImGuiLayerInitInfo& initInfo) {
    if (initialized) {
        return;
    }

    if (initInfo.WindowHandle == nullptr || initInfo.Device == nullptr || initInfo.CommandQueue == nullptr || initInfo.NumFramesInFlight <= 0) {
        throw std::runtime_error("Invalid ImGui initialization parameters.");
    }

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 1;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NodeMask = 0;
    ThrowIfFailed(initInfo.Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&fontSrvHeap)));

    fontSrvCpuHandle = fontSrvHeap->GetCPUDescriptorHandleForHeapStart();
    fontSrvGpuHandle = fontSrvHeap->GetGPUDescriptorHandleForHeapStart();
    fontDescriptorAllocated = false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(initInfo.WindowHandle)) {
        ImGui::DestroyContext();
        throw std::runtime_error("ImGui Win32 backend initialization failed.");
    }

    ImGui_ImplDX12_InitInfo dx12InitInfo;
    dx12InitInfo.Device = initInfo.Device;
    dx12InitInfo.CommandQueue = initInfo.CommandQueue;
    dx12InitInfo.NumFramesInFlight = initInfo.NumFramesInFlight;
    dx12InitInfo.RTVFormat = initInfo.RTVFormat;
    dx12InitInfo.DSVFormat = initInfo.DSVFormat;
    dx12InitInfo.SrvDescriptorHeap = fontSrvHeap.Get();
    dx12InitInfo.UserData = this;
    dx12InitInfo.SrvDescriptorAllocFn = &ImGuiLayer::AllocateSrvDescriptor;
    dx12InitInfo.SrvDescriptorFreeFn = &ImGuiLayer::FreeSrvDescriptor;

    if (!ImGui_ImplDX12_Init(&dx12InitInfo)) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("ImGui DX12 backend initialization failed.");
    }

    initialized = true;
}

void ImGuiLayer::BeginFrame() {
    if (!initialized) {
        return;
    }

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::Render(ID3D12GraphicsCommandList* commandList) {
    if (!initialized || commandList == nullptr) {
        return;
    }

    ImGui::Render();
    ID3D12DescriptorHeap* descriptorHeaps[] = { fontSrvHeap.Get() };
    commandList->SetDescriptorHeaps(1, descriptorHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}

void ImGuiLayer::Shutdown() {
    if (!initialized) {
        return;
    }

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    fontSrvHeap.Reset();
    fontDescriptorAllocated = false;
    initialized = false;
}

bool ImGuiLayer::HandleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) const {
    if (!initialized) {
        return false;
    }

    return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam) != 0;
}

bool ImGuiLayer::WantsCaptureMouse() const {
    return initialized ? ImGui::GetIO().WantCaptureMouse : false;
}

bool ImGuiLayer::WantsCaptureKeyboard() const {
    return initialized ? ImGui::GetIO().WantCaptureKeyboard : false;
}

bool ImGuiLayer::IsInitialized() const {
    return initialized;
}

void ImGuiLayer::AllocateSrvDescriptor(
    ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE* outCpuDescHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE* outGpuDescHandle
) {
    ImGuiLayer* layer = static_cast<ImGuiLayer*>(info->UserData);
    if (layer == nullptr || outCpuDescHandle == nullptr || outGpuDescHandle == nullptr) {
        return;
    }

    *outCpuDescHandle = layer->fontSrvCpuHandle;
    *outGpuDescHandle = layer->fontSrvGpuHandle;
    layer->fontDescriptorAllocated = true;
}

void ImGuiLayer::FreeSrvDescriptor(
    ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE,
    D3D12_GPU_DESCRIPTOR_HANDLE
) {
    ImGuiLayer* layer = static_cast<ImGuiLayer*>(info->UserData);
    if (layer != nullptr) {
        layer->fontDescriptorAllocated = false;
    }
}
} // namespace GUI
