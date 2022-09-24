module;

#include "framework.h"
#include "macro.h"
#include "Include/d3d8.h"
#include "Include/d3dx8core.h"
#include <string>
#include <vector>

export module dx8.hook;

import common.datatype;
import common.var;
import common.helper;
import common.minhook;

#define ModulateColor(i) D3DCOLOR_RGBA(i, i, i, 255)
#define SetTextureColorStage(dev, i, op, arg1, arg2)      \
    dev->SetTextureStageState(i, D3DTSS_COLOROP, op);     \
    dev->SetTextureStageState(i, D3DTSS_COLORARG1, arg1); \
    dev->SetTextureStageState(i, D3DTSS_COLORARG2, arg2)

constexpr auto ResetIdx = 14;
constexpr auto PresentIdx = 15;
constexpr auto ErrorMessageTitle = "D3D8 Hook Setup Error";

using namespace std;

HRESULT WINAPI D3DReset(IDirect3DDevice8* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);
decltype(&D3DReset) OriReset;
HRESULT WINAPI D3DPresent(IDirect3DDevice8* pDevice, RECT* pSourceRect, RECT* pDestRect, HWND hDestWindowOverride, RGNDATA* pDirtyRegion);
decltype(&D3DPresent) OriPresent;

inline const char* GetD3dErrStr(const int errorCode) {
    if (errorCode == D3DERR_INVALIDCALL)
        return "D3DERR_INVALIDCALL";
    if (errorCode == D3DERR_NOTAVAILABLE)
        return "D3DERR_NOTAVAILABLE";
    if (errorCode == D3DERR_OUTOFVIDEOMEMORY)
        return "D3DERR_OUTOFVIDEOMEMORY";
    return "Unknown error.";
}

using CallbackType = void (*)(void);
vector<CallbackType>& initializeCallbacks() {
    static vector<CallbackType> backing;
    return backing;
}
export DLLEXPORT void RegisterD3D8InitializeCallback(CallbackType callback) {
    initializeCallbacks().push_back(callback);
}

export DLLEXPORT bool PopulateD3D8MethodRVAs() {
    bool result = false;
    DWORD* vtable{};
    HRESULT rs{};
    IDirect3D8* pD3D{};
    IDirect3DDevice8* pDevice{};
    D3DPRESENT_PARAMETERS d3dpp{};
    DWORD baseAddress{};

    auto tmpWnd = CreateWindowA("BUTTON", "Temp Window", WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 300, 300, NULL, NULL, NULL, NULL);
    if (tmpWnd == NULL) {
        ReportLastError(ErrorMessageTitle);
        goto CleanAndReturn;
    }

    pD3D = Direct3DCreate8(D3D_SDK_VERSION);
    if (!pD3D) {
        MessageBoxA(NULL, "Failed to create an IDirect3D8 instance.", ErrorMessageTitle, MB_OK | MB_ICONERROR);
        goto CleanAndReturn;
    }

    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = tmpWnd;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    d3dpp.BackBufferCount = 1;
    d3dpp.BackBufferWidth = 4;
    d3dpp.BackBufferHeight = 4;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;

    rs = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDevice);
    if (FAILED(rs)) {
        MessageBoxA(NULL, (string("Failed to create an IDirect3DDevice8 instance:") + GetD3dErrStr(rs)).c_str(), ErrorMessageTitle, MB_OK | MB_ICONERROR);
        goto CleanAndReturn;
    }

    vtable = *(DWORD**)pDevice;
    baseAddress = (DWORD)GetModuleHandleA("d3d8.dll");

    gs_d3d8_Reset_RVA = vtable[ResetIdx] - baseAddress;
    gs_d3d8_Present_RVA = vtable[PresentIdx] - baseAddress;

    result = true;
CleanAndReturn:
    pDevice && pDevice->Release();
    pD3D && pD3D->Release();
    tmpWnd && DestroyWindow(tmpWnd);
    return result;
}

export DLLEXPORT vector<MHookConfig> D3D8HookConfig() {
    auto baseAddress = (DWORD)GetModuleHandleA("d3d8.dll");
    return {
        {PVOID(baseAddress + gs_d3d8_Reset_RVA), &D3DReset, (PVOID*)&OriReset},
        {PVOID(baseAddress + gs_d3d8_Present_RVA), &D3DPresent, (PVOID*)&OriPresent},
    };
}

// job flags
bool initialized;
bool isReset;
bool measurementPrepared;
bool cursorStatePrepared;

// cursor and screen state
LPDIRECT3DTEXTURE8  cursorTexture;
LPD3DXSPRITE        cursorSprite;
D3DXVECTOR2         cursorPivot;
D3DXVECTOR2         cursorScale;
float               d3dScale = 1.f;

void CleanUp() {
    if (cursorSprite)
        cursorSprite->Release();
    if (cursorTexture)
        cursorTexture->Release();
    cursorSprite = NULL;
    cursorTexture = NULL;
}

struct OnInit {
    OnInit() {
        RegisterMHookUninitializeCallback(Callback);
    }
    static void Callback(bool isProcessTerminating) {
        if (isProcessTerminating)
            return;
        CleanUp();
    }
} _;

void Initialize(IDirect3DDevice8* device) {
    if (initialized)
        return;
    initialized = true;
    for (auto& callback : initializeCallbacks())
        callback();
    if (!isReset) {
        D3DDEVICE_CREATION_PARAMETERS params;
        device->GetCreationParameters(&params);
        g_hFocusWindow = params.hFocusWindow;
    }
    if (gs_textureFilePath[0] && D3DXCreateTextureFromFileW(device, gs_textureFilePath, &cursorTexture) == D3D_OK) {
        D3DXCreateSprite(device, &cursorSprite);
        D3DSURFACE_DESC cursorSize;
        cursorTexture->GetLevelDesc(0, &cursorSize);
        cursorPivot = {(cursorSize.Height - 1) / 2.f, (cursorSize.Width - 1) / 2.f};
    }
    SystemParametersInfoA(SPI_SETCURSORSHADOW, 0, (PVOID)TRUE, SPIF_SENDCHANGE);
}

HRESULT WINAPI D3DReset(IDirect3DDevice8* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    Initialize(pDevice);
    if (pPresentationParameters->hDeviceWindow != NULL)
        g_hFocusWindow = pPresentationParameters->hDeviceWindow;
    CleanUp();
    isReset = true;
    initialized = false;
    measurementPrepared = false;
    cursorStatePrepared = false;
    return OriReset(pDevice, pPresentationParameters);
}

/*
this routine:
- remove window's border if game is fullscreened (exclusive mode)
- determine g_pixelRate
- determine g_pixelOffset
*/
void PrepareMeasurement(IDirect3DDevice8* pDevice) {
    if (measurementPrepared)
        return;
    measurementPrepared = true;
    RECTSIZE clientSize;
    if (GetClientRect(g_hFocusWindow, &clientSize) == FALSE)
        return;
    IDirect3DSurface8* pSurface;
    auto rs = pDevice->GetRenderTarget(&pSurface);
    if (rs != D3D_OK)
        return;
    D3DSURFACE_DESC d3dSize;
    rs = pSurface->GetDesc(&d3dSize);
    pSurface->Release();
    if (rs != D3D_OK)
        return;
    // if client size is smaller than d3d size (fullscreen exclusive mode)
    if (d3dSize.Width > UINT(clientSize.width()) || d3dSize.Height > UINT(clientSize.height())) {
        // clear border to avoid "click-out-of-bound"
        RemoveWindowBorder(d3dSize.Width, d3dSize.Height);
    }
    if (GetClientRect(g_hFocusWindow, &clientSize) == FALSE)
        return;
    g_pixelRate = float(g_currentConfig.BaseHeight) / clientSize.height();
    g_pixelOffset.X = g_currentConfig.BasePixelOffset.X / g_pixelRate;
    g_pixelOffset.Y = g_currentConfig.BasePixelOffset.Y / g_pixelRate;
}

/*
Determine scaling
*/
void PrepareCursorState(IDirect3DDevice8* pDevice) {
    if (cursorStatePrepared)
        return;
    cursorStatePrepared = true;
    IDirect3DSurface8* pSurface;
    auto rs = pDevice->GetRenderTarget(&pSurface);
    if (rs != D3D_OK) {
        d3dScale = 0.f;
        return;
    }
    D3DSURFACE_DESC d3dSize;
    rs = pSurface->GetDesc(&d3dSize);
    pSurface->Release();
    if (rs != D3D_OK) {
        d3dScale = 0.f;
        return;
    }
    auto scale = float(d3dSize.Height) / gs_textureBaseHeight;
    cursorScale = D3DXVECTOR2(scale, scale);

    RECTSIZE clientSize;
    BOOL rs2 = GetClientRect(g_hFocusWindow, &clientSize);
    if (rs2 == 0) {
        d3dScale = 0.f;
        return;
    }
    d3dScale = float(clientSize.width()) / d3dSize.Width;
}

void RenderCursor(IDirect3DDevice8* pDevice) {
    if (!cursorTexture)
        return;

    pDevice->BeginScene();

    // scale mouse cursor's position from screen coordinate to D3D coordinate
    POINT pointerPosition = GetPointerPosition();
    D3DXVECTOR2 cursorPositionD3D(float(pointerPosition.x), float(pointerPosition.y));
    if (d3dScale != 0.f && d3dScale != 1.f)
        cursorPositionD3D /= d3dScale;

    // DirectX8 doesn't have a conventional method for sprite pivot
    cursorPositionD3D.x -= cursorPivot.x;
    cursorPositionD3D.y -= cursorPivot.y;

    cursorSprite->Begin();
    auto scale = cursorStatePrepared ? &cursorScale : NULL;
    scale = NULL;
    // draw the cursor and scale cursor sprite to match the current render resolution
    if (g_inputEnabled) {
        static UCHAR modulate = 0;
        static auto modulateStage = WhiteInc;
        CalculateNextModulate(_ref modulate, _ref modulateStage);
        if (modulateStage == WhiteInc || modulateStage == WhiteDec) {
            SetTextureColorStage(pDevice, 0, D3DTOP_ADD, D3DTA_TEXTURE, D3DTA_DIFFUSE);
            cursorSprite->Draw(cursorTexture, NULL, scale, NULL, 0, &cursorPositionD3D, ModulateColor(modulate));
        } else {
            cursorSprite->Draw(cursorTexture, NULL, scale, NULL, 0, &cursorPositionD3D, ModulateColor(modulate));
        }
    } else {
        cursorSprite->Draw(cursorTexture, NULL, scale, NULL, 0, &cursorPositionD3D, D3DCOLOR_RGBA(255, 200, 200, 128));
    }
    cursorSprite->End();

    pDevice->EndScene();
}


HRESULT WINAPI D3DPresent(IDirect3DDevice8* pDevice, RECT* pSourceRect, RECT* pDestRect, HWND hDestWindowOverride, RGNDATA* pDirtyRegion) {
    Initialize(pDevice);
    PrepareMeasurement(pDevice);
    PrepareCursorState(pDevice);
    RenderCursor(pDevice);
    return OriPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}