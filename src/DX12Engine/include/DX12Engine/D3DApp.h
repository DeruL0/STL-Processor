#pragma once

#include "DX12Engine/D3DUtil.h"
#include "DX12Engine/GameTimer.h"
#include <unordered_map>
#include <unordered_set>

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

class D3DApp {
protected:
    static D3DApp* app;

    HINSTANCE hAppInst     = nullptr; // application instance handle
    HWND      hMainWnd     = nullptr; // main window handle
    bool      isAppPaused  = false;   // is the application paused?
    bool      isMinimized  = false;   // is the application minimized?
    bool      isMaximized  = false;   // is the application maximized?
    bool      isResizing   = false;   // are the resize bars being dragged?
    bool      isFullscreen = false;   // fullscreen enabled

    // Set true to use 4X MSAA (?.1.8).  The default is false.
    bool      m4xMsaaState = false;    // 4X MSAA enabled
    UINT      m4xMsaaQuality = 0;      // quality level of 4X MSAA

    GameTimer timer;

    Microsoft::WRL::ComPtr<IDXGIFactory4> dx12GiFactory;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain;
    Microsoft::WRL::ComPtr<ID3D12Device> dx12Device;

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    UINT64 currentFence = 0;

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> directCmdListAlloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;

    static const int swapChainBufferCount = 2;
    int currBackBuffer = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> swapChainBuffer[swapChainBufferCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilBuffer;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> RtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DsvHeap;

    D3D12_VIEWPORT screenViewport;
    D3D12_RECT scissorRect;

    UINT rtvDescriptorSize = 0;       //RTV：渲染目标视图资源
    UINT dsvDescriptorSize = 0;       //DSV：深度/模板视图资源
    UINT cbvSrvUavDescriptorSize = 0; //CBV/SRV/UAV：常量缓冲区视图/着色器资源视图/无序访问视图

    // Derived class should set these in derived constructor to customize starting values.
    std::wstring mainWndCaption = L"STL APP";
    D3D_DRIVER_TYPE dx12DriverType = D3D_DRIVER_TYPE_HARDWARE;
    DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    int clientWidth = 800;
    int clientHeight = 600;

protected:
    D3DApp(HINSTANCE hInstance);
    D3DApp(const D3DApp& rhs) = delete;
    D3DApp& operator=(const D3DApp& rhs) = delete;
    virtual ~D3DApp();

public:
    virtual bool Init();
    int Run();

    static D3DApp* GetApp();

    HINSTANCE AppInst()const;
    HWND      MainWnd()const;
    float     AspectRatio()const;

    bool Get4xMsaaState()const;
    void Set4xMsaaState(bool value);

    virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

protected:
    bool InitMainWindow();
    bool InitDX12();

    void CreateCommandObjects();
    void CreateSwapChain();
    virtual void CreateRtvAndDsvDescriptorHeaps();

    virtual void OnResize();
    virtual void Update(const GameTimer& gt) = 0;
    virtual void Draw(const GameTimer& gt) = 0;

    void FlushCommandQueue();

    void CalculateFrameStats();
    
    ID3D12Resource* CurrentBackBuffer()const;
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView()const;
    D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView()const;

    void LogAdapters();                                                  //枚举所有适配器(如显卡)
    void LogAdapterOutput(IDXGIAdapter* adapter);                        //枚举所有显示输出(如显示器)
    void LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format); //获得显示输出对此格式支持的全部显示模式

    virtual void OnMouseDown(WPARAM btnState, int x, int y) { }
    virtual void OnMouseUp(WPARAM btnState, int x, int y) { }
    virtual void OnMouseMove(WPARAM btnState, int x, int y) { }
};
