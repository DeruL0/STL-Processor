#include "DX12Engine/D3DApp.h"
#include <WindowsX.h>

using Microsoft::WRL::ComPtr;
using namespace std;
using namespace DirectX;


LRESULT CALLBACK
MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam){
    // Forward hwnd on because we can get messages (e.g., WM_CREATE)
        // before CreateWindow returns, and thus before mhMainWnd is valid.
    return D3DApp::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
}

D3DApp* D3DApp::app = nullptr;
D3DApp* D3DApp::GetApp(){
    return app;
}



D3DApp::D3DApp(HINSTANCE hInstance)
    : hAppInst(hInstance){
    // Only one D3DApp can be constructed.
    assert(app == nullptr);
    app = this;
}

D3DApp::~D3DApp(){
    if (dx12Device != nullptr)
        FlushCommandQueue();
}



bool D3DApp::Init() {
    if (!InitMainWindow())
        return false;

    if (!InitDX12()) {
        return false;
    }

    OnResize();

    return true;
}

int D3DApp::Run() {
    MSG msg = { 0 };

    timer.Reset();

    while (msg.message != WM_QUIT){
        //有消息窗口就区里
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)){
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        //否则执行动画or游戏逻辑
        else{
            timer.Tick();

            if (!isAppPaused){
                CalculateFrameStats();
                Update(timer);
                Draw(timer);
            }
            else{
                Sleep(100);
            }
        }
    }

    return (int)msg.wParam;
}



HINSTANCE D3DApp::AppInst()const{
    return hAppInst;
}

HWND D3DApp::MainWnd()const{
    return hMainWnd;
}

float D3DApp::AspectRatio()const{
    return static_cast<float>(clientWidth) / clientHeight;
}



bool D3DApp::Get4xMsaaState()const{
    return m4xMsaaState;
}

void D3DApp::Set4xMsaaState(bool value){
    if (m4xMsaaState != value){
        m4xMsaaState = value;

        // Recreate the swapchain and buffers with new multisample settings.
        CreateSwapChain();
        OnResize();
    }
}



LRESULT D3DApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam){
    switch (msg) {
        // WM_ACTIVATE is sent when the window is activated or deactivated.  
        // We pause the game when the window is deactivated and unpause it 
        // when it becomes active.  
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            isAppPaused = true;
            timer.Stop();
        }
        else {
            isAppPaused = false;
            timer.Start();
        }
        return 0;

        // WM_SIZE is sent when the user resizes the window.  
    case WM_SIZE:
        // Save the new client area dimensions.
        clientWidth = LOWORD(lParam);
        clientHeight = HIWORD(lParam);
        if (dx12Device) {
            if (wParam == SIZE_MINIMIZED) {
                isAppPaused = true;
                isMinimized = true;
                isMaximized = false;
            }
            else if (wParam == SIZE_MAXIMIZED) {
                isAppPaused = false;
                isMinimized = false;
                isMaximized = true;
                OnResize();
            }
            else if (wParam == SIZE_RESTORED) {
                // Restoring from minimized state?
                if (isMinimized) {
                    isAppPaused = false;
                    isMinimized = false;
                    OnResize();
                }

                // Restoring from maximized state?
                else if (isMaximized) {
                    isAppPaused = false;
                    isMaximized = false;
                    OnResize();
                }
                else if (isResizing) {
                    // If user is dragging the resize bars, we do not resize 
                    // the buffers here because as the user continuously 
                    // drags the resize bars, a stream of WM_SIZE messages are
                    // sent to the window, and it would be pointless (and slow)
                    // to resize for each WM_SIZE message received from dragging
                    // the resize bars.  So instead, we reset after the user is 
                    // done resizing the window and releases the resize bars, which 
                    // sends a WM_EXITSIZEMOVE message.
                }
                // API call such as SetWindowPos or mSwapChain->SetFullscreenState
                else {
                    OnResize();
                }
            }
        }
        return 0;

        // WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
    case WM_ENTERSIZEMOVE:
        isAppPaused = true;
        isResizing = true;
        timer.Stop();
        return 0;

        // WM_EXITSIZEMOVE is sent when the user releases the resize bars.
        // Here we reset everything based on the new window dimensions.
    case WM_EXITSIZEMOVE:
        isAppPaused = false;
        isResizing = false;
        timer.Start();
        OnResize();
        return 0;

        // WM_DESTROY is sent when the window is being destroyed.
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

        // The WM_MENUCHAR message is sent when a menu is active and the user presses 
        // a key that does not correspond to any mnemonic or accelerator key. 
    case WM_MENUCHAR:
        // Don't beep when we alt-enter.
        return MAKELRESULT(0, MNC_CLOSE);

        // Catch this message so to prevent the window from becoming too small.
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
        ((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
        return 0;

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
        OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
        OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_KEYUP:
        if (wParam == VK_ESCAPE) {
            PostQuitMessage(0);
        }
        else if ((int)wParam == VK_F2)
            Set4xMsaaState(!m4xMsaaState);

        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}



bool D3DApp::InitMainWindow(){
    WNDCLASS wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hAppInst;
    wc.hIcon = LoadIcon(0, IDI_APPLICATION);
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc.lpszMenuName = 0;
    wc.lpszClassName = L"MainWnd";

    if (!RegisterClass(&wc)){
        MessageBox(0, L"RegisterClass Failed.", 0, 0);
        return false;
    }

    // Compute window rectangle dimensions based on requested client area dimensions.
    RECT R = { 0, 0, clientWidth, clientHeight };
    AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
    int width = R.right - R.left;
    int height = R.bottom - R.top;

    hMainWnd = CreateWindow(L"MainWnd", mainWndCaption.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, 0, 0, hAppInst, 0);

    if (!hMainWnd){
        MessageBox(0, L"CreateWindow Failed.", 0, 0);
        return false;
    }

    ShowWindow(hMainWnd, SW_SHOW);
    UpdateWindow(hMainWnd);

    return true;
}

bool D3DApp::InitDX12() {
    //创建debug层
    #if defined(DEBUG) || defined(_DEBUG) 
    {
	    ComPtr<ID3D12Debug> debugController;
	    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
	    debugController->EnableDebugLayer();
    }
    #endif

    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&dx12GiFactory)));

    //创建硬件设备并检测
    HRESULT hardwareRes = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dx12Device));

    //如果创建失败，回退到WARP设备(Windows高级光栅化平台)
    if(FAILED(hardwareRes)) {
        ComPtr<IDXGIAdapter> pWarpAdapter;
        ThrowIfFailed(dx12GiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));
        ThrowIfFailed(D3D12CreateDevice(pWarpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dx12Device)));
    }

    //创建fence(实现CPU和GPU同步，强制CPU等待，直到GPU达到某个指定围栏点为止)
    ThrowIfFailed(dx12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

    //缓存描述符大小，以便于直接引用
    rtvDescriptorSize = dx12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    dsvDescriptorSize = dx12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    cbvSrvUavDescriptorSize = dx12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    //检测4×MSAA是否可用
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
    msQualityLevels.Format = backBufferFormat;
    msQualityLevels.SampleCount = 4;
    msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    msQualityLevels.NumQualityLevels = 0;
    ThrowIfFailed(dx12Device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msQualityLevels, sizeof(msQualityLevels)));
    
    m4xMsaaQuality = msQualityLevels.NumQualityLevels;
    assert(m4xMsaaQuality > 0 && "Unexpected MSAA quality level.");

    #ifdef _DEBUG
        LogAdapters();
    #endif

    CreateCommandObjects();
    CreateSwapChain();
    CreateRtvAndDsvDescriptorHeaps();

    return true;
}



void D3DApp::CreateCommandObjects() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    //创建命令队列
    ThrowIfFailed(dx12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));
    //创建命令分配器
    ThrowIfFailed(dx12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(directCmdListAlloc.GetAddressOf())));
    //创建命令列表，并将流水线状态置空
    ThrowIfFailed(dx12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, directCmdListAlloc.Get(), nullptr, IID_PPV_ARGS(commandList.GetAddressOf())));

    //调用前要重置，先关闭
    commandList->Close();
}

void D3DApp::CreateSwapChain() {
    //通过销毁旧交换链来重建新交换链，借此改变多重采样设置
    swapChain.Reset();

    DXGI_SWAP_CHAIN_DESC sd;

    //BufferDesc描述后台缓冲区属性，仅关注宽高。像素格式属性
    sd.BufferDesc.Width = clientWidth;
    sd.BufferDesc.Height = clientHeight;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Format = backBufferFormat;
    sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

    //SampleDesc描述多重采样的质量级别和对像素采样次数(此处数量为1，质量为0)
    sd.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    sd.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;

    //将数据渲染至后台缓冲区
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    //交换链缓冲区数量
    sd.BufferCount = swapChainBufferCount;

    //渲染窗口的句柄
    sd.OutputWindow = hMainWnd;
    //窗口or全屏模式
    sd.Windowed = true;

    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    //切换全屏则以最适合当前应用程序的尺寸的显示模式，不指定该模式则当前桌面显示模式
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    //通过命令队列进行刷新
    ThrowIfFailed(dx12GiFactory->CreateSwapChain(commandQueue.Get(), &sd, swapChain.GetAddressOf()));
}

void D3DApp::CreateRtvAndDsvDescriptorHeaps() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = swapChainBufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;
    ThrowIfFailed(dx12Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(RtvHeap.GetAddressOf())));


    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;
    ThrowIfFailed(dx12Device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(DsvHeap.GetAddressOf())));
}



void D3DApp::OnResize() {
    assert(dx12Device);
    assert(swapChain);
    assert(directCmdListAlloc);

    FlushCommandQueue();

    ThrowIfFailed(commandList->Reset(directCmdListAlloc.Get(), nullptr));

    //释放将会被重建的资源
    for (int i = 0; i < swapChainBufferCount; i++){
        swapChainBuffer[i].Reset();
    }
    depthStencilBuffer.Reset();
    ThrowIfFailed(swapChain->ResizeBuffers(swapChainBufferCount, clientWidth, clientHeight, backBufferFormat, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));
    currBackBuffer = 0;

    //创建渲染目标视图，为资源创建视图并将其绑定到流水线阶段========================================================================

    //为交换链每一个缓冲区建立rtv
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(RtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < swapChainBufferCount; i++){
        //获得交换链第i个缓冲区
        ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&swapChainBuffer[i])));
        //为缓冲区创建rtv
        dx12Device->CreateRenderTargetView(swapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
        //偏移到描述符堆下一个缓冲区
        rtvHeapHandle.Offset(1, rtvDescriptorSize);
    }

    //创建深度/模板缓冲区&视图=======================================================================================================
  
    D3D12_RESOURCE_DESC depthStencilDesc;

    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;  //资源维度
    depthStencilDesc.Alignment = 0;                                   
    depthStencilDesc.Width = clientWidth;                             //纹素宽度
    depthStencilDesc.Height = clientHeight;                           //纹素高度
    depthStencilDesc.DepthOrArraySize = 1;                            //纹素为单位表示纹理深度(纹理数组大小)
    depthStencilDesc.MipLevels = 1;                                   //mimap层级
    depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;             //指定纹素格式
   
    depthStencilDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;                       //多重采样每个像素采样次数
    depthStencilDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;  //多重采样质量级别
   
    depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;            //指定纹理布局
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;  //杂项

    D3D12_CLEAR_VALUE optClear;
    optClear.Format = depthStencilFormat;
    optClear.DepthStencil.Depth = 1.0f;
    optClear.DepthStencil.Stencil = 0;

    ThrowIfFailed(dx12Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),  //资源将要提交至的堆的属性
        D3D12_HEAP_FLAG_NONE,                               //额外项
        &depthStencilDesc,                                  //指向D3D12_RESOURCE_DESC实例的指针
        D3D12_RESOURCE_STATE_COMMON,                        //设置初始状态
        &optClear,                                          //指向D3D12_CLEAR_VALUE的指针，用于清除资源的优化值
        IID_PPV_ARGS(depthStencilBuffer.GetAddressOf()))
    );


    //为资源第0 mip层创建描述符
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Format = depthStencilFormat;
    dsvDesc.Texture2D.MipSlice = 0;
    dx12Device->CreateDepthStencilView(depthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

    //将资源从初始状态转换为深度缓冲区
    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(depthStencilBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));

    // Execute the resize commands.
    ThrowIfFailed(commandList->Close());
    ID3D12CommandList* cmdsLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until resize is complete.
    FlushCommandQueue();

    // Update the viewport transform to cover the client area.
    screenViewport.TopLeftX = 0;
    screenViewport.TopLeftY = 0;
    screenViewport.Width = static_cast<float>(clientWidth);
    screenViewport.Height = static_cast<float>(clientHeight);
    screenViewport.MinDepth = 0.0f;
    screenViewport.MaxDepth = 1.0f;

    scissorRect = { 0, 0, clientWidth, clientHeight };
}



void D3DApp::FlushCommandQueue(){
    // Advance the fence value to mark commands up to this fence point.
    currentFence++;

    // Add an instruction to the command queue to set a new fence point.  Because we 
    // are on the GPU timeline, the new fence point won't be set until the GPU finishes
    // processing all the commands prior to this Signal().
    ThrowIfFailed(commandQueue->Signal(fence.Get(), currentFence));

    // Wait until the GPU has completed commands up to this fence point.
    if (fence->GetCompletedValue() < currentFence){
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

        // Fire event when GPU hits current fence.  
        ThrowIfFailed(fence->SetEventOnCompletion(currentFence, eventHandle));

        // Wait until the GPU hits current fence event is fired.
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}



void D3DApp::CalculateFrameStats(){
    // Code computes the average frames per second, and also the 
    // average time it takes to render one frame.  These stats 
    // are appended to the window caption bar.

    static int frameCnt = 0;
    static float timeElapsed = 0.0f;

    frameCnt++;

    // Compute averages over one second period.
    if ((timer.TotalTime() - timeElapsed) >= 1.0f){
        float fps = (float)frameCnt; // fps = frameCnt / 1
        float mspf = 1000.0f / fps;

        wstring fpsStr = to_wstring(fps);
        wstring mspfStr = to_wstring(mspf);

        wstring windowText = mainWndCaption +
            L"    fps: " + fpsStr +
            L"   mspf: " + mspfStr;

        SetWindowText(hMainWnd, windowText.c_str());

        // Reset for next average.
        frameCnt = 0;
        timeElapsed += 1.0f;
    }
}



ID3D12Resource* D3DApp::CurrentBackBuffer()const{
    return swapChainBuffer[currBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::CurrentBackBufferView()const{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        RtvHeap->GetCPUDescriptorHandleForHeapStart(),
        currBackBuffer,
        rtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::DepthStencilView()const {
    return DsvHeap->GetCPUDescriptorHandleForHeapStart();
}


void D3DApp::LogAdapters() {
    UINT i = 0;
    IDXGIAdapter* adapter = nullptr;
    std::vector<IDXGIAdapter*> adapterList;
    while (dx12GiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND){
        DXGI_ADAPTER_DESC desc;
        adapter->GetDesc(&desc);

        std::wstring text = L"***Adapter: ";
        text += desc.Description;
        text += L"\n";

        OutputDebugString(text.c_str());

        adapterList.push_back(adapter);

        ++i;
    }

    for (size_t i = 0; i < adapterList.size(); ++i){
        LogAdapterOutput(adapterList[i]);
        ReleaseCom(adapterList[i]);
    }
}

void D3DApp::LogAdapterOutput(IDXGIAdapter* adapter){
    UINT i = 0;
    IDXGIOutput* output = nullptr;
    while (adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND)
    {
        DXGI_OUTPUT_DESC desc;
        output->GetDesc(&desc);

        std::wstring text = L"***Output: ";
        text += desc.DeviceName;
        text += L"\n";
        OutputDebugString(text.c_str());

        LogOutputDisplayModes(output, backBufferFormat);

        ReleaseCom(output);

        ++i;
    }
}

void D3DApp::LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format) {
    UINT count = 0;
    UINT flags = 0;

    // Call with nullptr to get list count.
    output->GetDisplayModeList(format, flags, &count, nullptr);

    std::vector<DXGI_MODE_DESC> modeList(count);
    output->GetDisplayModeList(format, flags, &count, &modeList[0]);

    for (auto& x : modeList){
        UINT n = x.RefreshRate.Numerator;
        UINT d = x.RefreshRate.Denominator;
        std::wstring text =
            L"Width = " + std::to_wstring(x.Width) + L" " +
            L"Height = " + std::to_wstring(x.Height) + L" " +
            L"Refresh = " + std::to_wstring(n) + L"/" + std::to_wstring(d) +
            L"\n";
        OutputDebugString(text.c_str());
    }
}
