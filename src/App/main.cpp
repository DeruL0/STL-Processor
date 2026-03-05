#include "STLApp.h"

#include <exception>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd) {
#if defined(DEBUG) || defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try {
        STLApp theApp(hInstance);
        if (!theApp.Init()) {
            return 0;
        }

        return theApp.Run();
    }
    catch (DxException& e) {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
    catch (std::exception& e) {
        std::wstring message = AnsiToWString(e.what());
        MessageBox(nullptr, message.c_str(), L"Unhandled Exception", MB_OK);
        return 0;
    }
}
