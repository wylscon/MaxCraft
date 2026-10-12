#pragma once

// The slice of Direct3D 8 MaxCraft uses. The Windows SDK no longer ships d3d8.h, so the types,
// constants and vtable slots are declared here (values from the DirectX 8 SDK headers).
// COM methods are called by vtable index: VCall<Index, Ret>(object, args...).
//
// Every slot below is the DirectX 8 SDK's (d3d8.h, SDK version 220). Direct3D 8 and 9 lay surfaces
// and textures out differently: IDirect3DSurface8 derives straight from IUnknown (no
// IDirect3DResource8 in between) and D3DSURFACE_DESC has a Size field and no MultiSampleQuality.
// Checking these numbers against a Direct3D 9 header gives wrong answers - that has happened here.

#include <Windows.h>

#include <cstdint>

namespace d3d8
{
    template <int Index, class Ret = HRESULT, class... Args>
    Ret VCall(void* object, Args... args)
    {
        using Fn = Ret(__stdcall*)(void*, Args...);
        return reinterpret_cast<Fn*>(*static_cast<void***>(object))[Index](object, args...);
    }

    inline void* VSlot(void* object, int index) { return (*static_cast<void***>(object))[index]; }
    inline void Release(void* object)
    {
        if (object)
            VCall<2, ULONG>(object);
    }

    constexpr UINT kSdkVersion = 220;

    enum Format : UINT
    {
        kFmtUnknown = 0,
        kFmtA8R8G8B8 = 21,
        kFmtX8R8G8B8 = 22,
    };

    struct PresentParameters
    {
        UINT  BackBufferWidth;
        UINT  BackBufferHeight;
        UINT  BackBufferFormat;
        UINT  BackBufferCount;
        UINT  MultiSampleType;
        UINT  SwapEffect;
        HWND  hDeviceWindow;
        BOOL  Windowed;
        BOOL  EnableAutoDepthStencil;
        UINT  AutoDepthStencilFormat;
        DWORD Flags;
        UINT  FullScreen_RefreshRateInHz;
        UINT  FullScreen_PresentationInterval;
    };

    struct DisplayMode
    {
        UINT Width, Height, RefreshRate, Format;
    };

    // D3DSURFACE_DESC as Direct3D 8 declares it (Direct3D 9's differs: it has MultiSampleQuality and
    // no Size). Confirmed in the field: the back buffer reads 1920x1080 at words 6/7.
    struct SurfaceDesc
    {
        UINT  Format;
        UINT  Type;
        DWORD Usage;
        UINT  Pool;
        UINT  Size;
        UINT  MultiSampleType;
        UINT  Width;
        UINT  Height;
    };

    struct Viewport
    {
        DWORD X, Y, Width, Height;
        float MinZ, MaxZ;
    };

    struct LockedRect
    {
        INT   Pitch;
        void* pBits;
    };

    struct DeviceCreationParameters
    {
        UINT  AdapterOrdinal;
        UINT  DeviceType;
        HWND  hFocusWindow;
        DWORD BehaviorFlags;
    };

    struct Matrix
    {
        float m[4][4];
    };

    // IDirect3D8
    namespace d3d
    {
        constexpr int GetAdapterDisplayMode = 8;
        constexpr int CreateDevice = 15;
    }

    // IDirect3DDevice8
    namespace dev
    {
        constexpr int Reset = 14;
        constexpr int Present = 15;
        constexpr int GetBackBuffer = 16;
        constexpr int CreateTexture = 20;
        constexpr int CreateVertexBuffer = 23;
        constexpr int GetCreationParameters = 9;
        constexpr int GetRenderTarget = 32;
        constexpr int GetDepthStencilSurface = 33;
        constexpr int BeginScene = 34;
        constexpr int EndScene = 35;
        constexpr int Clear = 36;
        constexpr int SetTransform = 37;
        constexpr int GetTransform = 38;
        constexpr int SetViewport = 40;
        constexpr int GetViewport = 41;
        constexpr int SetRenderState = 50;
        constexpr int ApplyStateBlock = 54;
        constexpr int CaptureStateBlock = 55;
        constexpr int DeleteStateBlock = 56;
        constexpr int CreateStateBlock = 57;
        constexpr int SetTexture = 61;
        constexpr int SetTextureStageState = 63;
        constexpr int DrawPrimitive = 70;
        constexpr int DrawPrimitiveUP = 72;
        constexpr int SetVertexShader = 76;
        constexpr int SetVertexShaderConstant = 79;
        constexpr int SetStreamSource = 83;
        constexpr int SetPixelShader = 88;
    }

    // IDirect3DSurface8: IUnknown 0-2, GetDevice 3, SetPrivateData 4, GetPrivateData 5,
    // FreePrivateData 6, GetContainer 7, GetDesc 8, LockRect 9, UnlockRect 10. Slot 10 used to be
    // probed as a "GetDescAlt": that called UnlockRect with an extra argument, which leaves the
    // stdcall stack 4 bytes off.
    namespace surf
    {
        constexpr int GetDesc = 8;
    }

    // IDirect3DTexture8: IUnknown 0-2, IDirect3DResource8 3-10 (GetDevice, Set/Get/FreePrivateData,
    // Set/GetPriority, PreLoad, GetType), IDirect3DBaseTexture8 11-13 (SetLOD, GetLOD,
    // GetLevelCount), then GetLevelDesc 14, GetSurfaceLevel 15, LockRect 16, UnlockRect 17.
    //
    // Not 5/6 (GetPrivateData/FreePrivateData) and not the retired 13/14 "alternative" pair: calling
    // 14 as LockRect makes GetLevelDesc write a 32-byte D3DSURFACE_DESC over an 8-byte LockedRect on
    // the caller's stack, which is the likely cause of the crash the log named CHECK_TEXTURE.
    namespace tex
    {
        constexpr int LockRect = 16;
        constexpr int UnlockRect = 17;
    }

    // IDirect3DVertexBuffer8
    namespace vb
    {
        constexpr int Lock = 11;
        constexpr int Unlock = 12;
    }

    enum : UINT
    {
        kDevTypeHal = 1,
        kSwapDiscard = 1,
        kCreateSoftwareVp = 0x20,
        kPoolDefault = 0,
        kPoolManaged = 1,
        kBackBufferMono = 0,
        kStateBlockAll = 1,

        kTsView = 2,
        kTsProjection = 3,
        kTsWorld = 256,

        kPtLineList = 2,
        kPtTriangleList = 4,
        kPtTriangleStrip = 5,

        kFvfXyz = 0x002,
        kFvfXyzRhw = 0x004,
        kFvfDiffuse = 0x040,
        kFvfTex1 = 0x100,

        kUsageWriteOnly = 0x8,
    };

    enum RenderState : UINT
    {
        kRsZEnable = 7,
        kRsFillMode = 8,
        kRsShadeMode = 9,
        kRsZWriteEnable = 14,
        kRsAlphaTestEnable = 15,
        kRsSrcBlend = 19,
        kRsDestBlend = 20,
        kRsCullMode = 22,
        kRsZFunc = 23,
        kRsAlphaRef = 24,
        kRsAlphaFunc = 25,
        kRsAlphaBlendEnable = 27,
        kRsFogEnable = 28,
        kRsSpecularEnable = 29,
        kRsStencilEnable = 52,
        kRsClipping = 136,
        kRsLighting = 137,
        kRsColorVertex = 141,
        kRsColorWriteEnable = 168,
        kRsBlendOp = 171,
    };

    enum TextureStageState : UINT
    {
        kTssColorOp = 1,
        kTssColorArg1 = 2,
        kTssColorArg2 = 3,
        kTssAlphaOp = 4,
        kTssAlphaArg1 = 5,
        kTssAlphaArg2 = 6,
        kTssTexCoordIndex = 11,
        kTssAddressU = 13,
        kTssAddressV = 14,
        kTssMagFilter = 16,
        kTssMinFilter = 17,
        kTssMipFilter = 18,
        kTssTextureTransformFlags = 24,
    };

    enum : UINT
    {
        kBlendZero = 1,
        kBlendOne = 2,
        kBlendSrcAlpha = 5,
        kBlendInvSrcAlpha = 6,
        kBlendOpAdd = 1,
        kCmpLessEqual = 4,
        kCmpGreaterEqual = 7,
        kCullNone = 1,
        kFillSolid = 3,
        kShadeGouraud = 2,
        kTopDisable = 1,
        kTopSelectArg1 = 2,
        kTopModulate = 4,
        kTaDiffuse = 0,
        kTaTexture = 2,
        kTexfNone = 0,
        kTexfPoint = 1,
        kTaddressWrap = 1,
        kTaddressClamp = 3,
    };
}
