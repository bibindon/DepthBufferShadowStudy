#pragma comment( lib, "d3d9.lib" )
#if defined(DEBUG) || defined(_DEBUG)
#pragma comment( lib, "d3dx9d.lib" )
#else
#pragma comment( lib, "d3dx9.lib" )
#endif

#include <d3d9.h>
#include <d3dx9.h>
#include <string>
#include <tchar.h>
#include <cassert>
#include <crtdbg.h>
#include <vector>

#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = NULL; } }

const int SCREEN_W = 1600;
const int SCREEN_H = 900;
const float SPACING = 10.0f;

LPDIRECT3D9 g_pD3D = NULL;
LPDIRECT3DDEVICE9 g_pd3dDevice = NULL;
LPD3DXMESH g_pMesh = NULL;

std::vector<D3DMATERIAL9> g_pMaterials;
std::vector<LPDIRECT3DTEXTURE9> g_pTextures;
DWORD g_dwNumMaterials = 0;
LPD3DXEFFECT g_fxBase = NULL;
LPD3DXEFFECT g_fxDepthBufferShadow = NULL;

LPDIRECT3DTEXTURE9 g_texRenderTargetBase = NULL;
LPDIRECT3DTEXTURE9 g_texRenderTargetLightZ = NULL;
LPDIRECT3DTEXTURE9 g_texRenderTargetShadow = NULL;

LPDIRECT3DSURFACE9 g_surfaceLightZStensil = NULL;

LPDIRECT3DVERTEXDECLARATION9 g_pQuadDecl = NULL;

// デバッグ確認用
LPD3DXSPRITE g_pSprite = NULL;

float g_fTime = 0.0f;

bool g_bClose = false;

struct QuadVertex
{
    // クリップ空間（-1..1, w=1）
    float x, y, z, w;

    // テクスチャ座標
    float u, v;
};

static void InitD3D(HWND hWnd);
static void Cleanup();

static void RenderPass1();
static void RenderPass2();
static void RenderPass3();
static void DrawFullscreenQuad();

LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern int WINAPI _tWinMain(_In_ HINSTANCE hInstance,
                            _In_opt_ HINSTANCE hPrevInstance,
                            _In_ LPTSTR lpCmdLine,
                            _In_ int nCmdShow);

int WINAPI _tWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPTSTR lpCmdLine,
                     _In_ int nCmdShow)
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    WNDCLASSEX wc { };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = MsgProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = _T("Window1");
    wc.hIconSm = NULL;

    ATOM atom = RegisterClassEx(&wc);
    assert(atom != 0);

    RECT rect;
    SetRect(&rect, 0, 0, SCREEN_W, SCREEN_H);
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    rect.right = rect.right - rect.left;
    rect.bottom = rect.bottom - rect.top;
    rect.top = 0;
    rect.left = 0;

    HWND hWnd = CreateWindow(_T("Window1"),
                             _T("Depth Buffer Shadow"),
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT,
                             CW_USEDEFAULT,
                             rect.right,
                             rect.bottom,
                             NULL,
                             NULL,
                             wc.hInstance,
                             NULL);

    InitD3D(hWnd);
    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);

    MSG msg;

    while (true)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            DispatchMessage(&msg);
        }
        else
        {
            // Sleep(16);

            g_fTime += 0.005f;

            RenderPass1();
            RenderPass2();
            RenderPass3();
        }

        if (g_bClose)
        {
            break;
        }
    }

    Cleanup();

    UnregisterClass(_T("Window1"), wc.hInstance);
    return 0;
}

void InitD3D(HWND hWnd)
{
    HRESULT hResult = E_FAIL;

    g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    assert(g_pD3D != NULL);

    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    d3dpp.BackBufferCount = 1;
    d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
    d3dpp.MultiSampleQuality = 0;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    d3dpp.hDeviceWindow = hWnd;
    d3dpp.Flags = 0;
    d3dpp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

    hResult = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT,
                                   D3DDEVTYPE_HAL,
                                   hWnd,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                   &d3dpp,
                                   &g_pd3dDevice);

    if (FAILED(hResult))
    {
        hResult = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT,
                                       D3DDEVTYPE_HAL,
                                       hWnd,
                                       D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                       &d3dpp,
                                       &g_pd3dDevice);
        assert(hResult == S_OK);
    }

    LPD3DXBUFFER pD3DXMtrlBuffer = NULL;

    hResult = D3DXLoadMeshFromX(_T("monkey.blend.x"),
                                D3DXMESH_SYSTEMMEM,
                                g_pd3dDevice,
                                NULL,
                                &pD3DXMtrlBuffer,
                                NULL,
                                &g_dwNumMaterials,
                                &g_pMesh);
    assert(hResult == S_OK);

    D3DXMATERIAL* d3dxMaterials = (D3DXMATERIAL*)pD3DXMtrlBuffer->GetBufferPointer();
    g_pMaterials.resize(g_dwNumMaterials);
    g_pTextures.resize(g_dwNumMaterials);

    for (DWORD i = 0; i < g_dwNumMaterials; i++)
    {
        g_pMaterials[i] = d3dxMaterials[i].MatD3D;
        g_pMaterials[i].Ambient = g_pMaterials[i].Diffuse;
        g_pTextures[i] = NULL;

        std::string pTexPath(d3dxMaterials[i].pTextureFilename);

        if (!pTexPath.empty())
        {
            hResult = D3DXCreateTextureFromFileA(g_pd3dDevice, pTexPath.c_str(), &g_pTextures[i]);
            assert(hResult == S_OK);
        }
    }

    hResult = pD3DXMtrlBuffer->Release();
    assert(hResult == S_OK);

    hResult = D3DXCreateEffectFromFile(g_pd3dDevice,
                                       _T("simple.fx"),
                                       NULL,
                                       NULL,
                                       D3DXSHADER_DEBUG,
                                       NULL,
                                       &g_fxBase,
                                       NULL);
    assert(hResult == S_OK);

    hResult = D3DXCreateEffectFromFile(g_pd3dDevice,
                                       _T("simple2.fx"),
                                       NULL,
                                       NULL,
                                       D3DXSHADER_DEBUG,
                                       NULL,
                                       &g_fxDepthBufferShadow,
                                       NULL);
    assert(hResult == S_OK);

    hResult = D3DXCreateTexture(g_pd3dDevice,
                                SCREEN_W, SCREEN_H,
                                1,
                                D3DUSAGE_RENDERTARGET,
                                D3DFMT_A8R8G8B8,
                                D3DPOOL_DEFAULT,
                                &g_texRenderTargetBase);
    assert(hResult == S_OK);

    if (false)
    {
        // 解像度16384x16384のテクスチャを作れないGPUは現代にはほぼ存在しない
        hResult = D3DXCreateTexture(g_pd3dDevice,
                                    SCREEN_W * 4,
                                    SCREEN_H * 4,
                                    1,
                                    D3DUSAGE_RENDERTARGET,
                                    D3DFMT_R16F,
                                    D3DPOOL_DEFAULT,
                                    &g_texRenderTargetLightZ);
    }
    else
    {
        hResult = D3DXCreateTexture(g_pd3dDevice,
                                    SCREEN_W * 2,
                                    SCREEN_H * 2,
                                    1,
                                    D3DUSAGE_RENDERTARGET,
                                    //D3DFMT_A16B16G16R16,
                                    //D3DFMT_R16F,
                                    D3DFMT_R32F,
                                    D3DPOOL_DEFAULT,
                                    &g_texRenderTargetLightZ);
    }

    assert(hResult == S_OK);

    D3DSURFACE_DESC bdesc{};
    g_texRenderTargetLightZ->GetLevelDesc(0, &bdesc);

    // 影用の深度ステンシル（サイズをRT2に合わせる）
    HRESULT hr = g_pd3dDevice->CreateDepthStencilSurface(bdesc.Width,
                                                         bdesc.Height,
                                                         D3DFMT_D16,
                                                         //D3DFMT_D24S8,
                                                         D3DMULTISAMPLE_NONE,
                                                         0,
                                                         TRUE,
                                                         &g_surfaceLightZStensil,
                                                         NULL);
    assert(hr == S_OK);

    hResult = D3DXCreateTexture(g_pd3dDevice,
                                SCREEN_W,
                                SCREEN_H,
                                1,
                                D3DUSAGE_RENDERTARGET,
                                D3DFMT_A8R8G8B8,
                                D3DPOOL_DEFAULT,
                                &g_texRenderTargetShadow);
    assert(hResult == S_OK);

    D3DVERTEXELEMENT9 elems[] =
    {
        { 0,  0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END()
    };

    hResult = g_pd3dDevice->CreateVertexDeclaration(elems, &g_pQuadDecl);
    assert(hResult == S_OK);

    hResult = D3DXCreateSprite(g_pd3dDevice, &g_pSprite);
    assert(hResult == S_OK);
}

void Cleanup()
{
    for (auto& texture : g_pTextures)
    {
        SAFE_RELEASE(texture);
    }

    SAFE_RELEASE(g_pMesh);
    SAFE_RELEASE(g_fxBase);
    SAFE_RELEASE(g_fxDepthBufferShadow);

    SAFE_RELEASE(g_texRenderTargetBase);
    SAFE_RELEASE(g_texRenderTargetLightZ);
    SAFE_RELEASE(g_texRenderTargetShadow);
    SAFE_RELEASE(g_pQuadDecl);
    SAFE_RELEASE(g_pSprite);

    SAFE_RELEASE(g_pd3dDevice);
    SAFE_RELEASE(g_pD3D);
}

void RenderPass1()
{
    HRESULT hr = E_FAIL;

    LPDIRECT3DSURFACE9 oldRT0 = NULL;

    hr = g_pd3dDevice->GetRenderTarget(0, &oldRT0);
    assert(hr == S_OK);

    LPDIRECT3DSURFACE9 surfaceBase = NULL;

    // Pass1で深度情報を描画するが今回は使用しない
    LPDIRECT3DSURFACE9 surfaceDummy = NULL;

    hr = g_texRenderTargetBase->GetSurfaceLevel(0, &surfaceBase);
    assert(hr == S_OK);

    hr = g_texRenderTargetLightZ->GetSurfaceLevel(0, &surfaceDummy);
    assert(hr == S_OK);

    hr = g_pd3dDevice->SetRenderTarget(0, surfaceBase);
    assert(hr == S_OK);

    hr = g_pd3dDevice->SetRenderTarget(1, NULL);
    assert(hr == S_OK);

    // カメラ行列
    D3DXMATRIX mView;
    D3DXMATRIX mProj;
    D3DXMatrixPerspectiveFovLH(&mProj,
                               D3DXToRadian(45.0f),
                               (float)SCREEN_W / SCREEN_H,
                               1.0f,
                               100.0f);

    D3DXVECTOR3 vEye(10.0f * sinf(g_fTime), 5.0f, -10.0f * cosf(g_fTime));
    D3DXVECTOR3 vAt(0, 0, 0);
    D3DXVECTOR3 vUp(0, 1, 0);

    D3DXMatrixLookAtLH(&mView, &vEye, &vAt, &vUp);

    hr = g_pd3dDevice->Clear(0, NULL,
                             D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                             D3DCOLOR_XRGB(100, 100, 100),
                             1.0f,
                             0);
    assert(hr == S_OK);

    hr = g_pd3dDevice->BeginScene();
    assert(hr == S_OK);

    hr = g_fxBase->SetTechnique("TechniqueMRT");
    assert(hr == S_OK);
    UINT np = 0;

    hr = g_fxBase->Begin(&np, 0);
    assert(hr == S_OK);

    hr = g_fxBase->BeginPass(0);
    assert(hr == S_OK);

    hr = g_fxBase->SetBool("g_bUseTexture", TRUE);
    assert(hr == S_OK);

    for (int idx = 0; idx < 25; ++idx)
    {
        int gx = idx % 5 - 2;
        int gz = idx / 5 - 2;

        D3DXMATRIX mWorld;
        D3DXMATRIX mWorldViewProj;
        D3DXMatrixTranslation(&mWorld, gx * SPACING, 0.0f, gz * SPACING);
        mWorldViewProj = mWorld * mView * mProj;

        hr = g_fxBase->SetMatrix("g_matWorldViewProj", &mWorldViewProj);
        assert(hr == S_OK);

        for (DWORD i = 0; i < g_dwNumMaterials; ++i)
        {
            hr = g_fxBase->SetTexture("g_textureBase", g_pTextures[i]);
            assert(hr == S_OK);

            hr = g_fxBase->CommitChanges();
            assert(hr == S_OK);

            hr = g_pMesh->DrawSubset(i);
            assert(hr == S_OK);
        }
    }

    hr = g_fxBase->EndPass();
    assert(hr == S_OK);

    hr = g_fxBase->End();
    assert(hr == S_OK);

    hr = g_pd3dDevice->EndScene();
    assert(hr == S_OK);

    hr = g_pd3dDevice->SetRenderTarget(0, oldRT0);
    assert(hr == S_OK);

    hr = g_pd3dDevice->SetRenderTarget(1, NULL);
    assert(hr == S_OK);

    SAFE_RELEASE(surfaceBase);
    SAFE_RELEASE(surfaceDummy);
    SAFE_RELEASE(oldRT0);
}

void RenderPass2()
{
    HRESULT hr = E_FAIL;

    //---------------------------------------------------------------------
    // (1) 光源から見た深度をテクスチャに描画
    //---------------------------------------------------------------------
    LPDIRECT3DSURFACE9 oldRT0 = NULL;
    LPDIRECT3DSURFACE9 oldZ = NULL;

    D3DXMATRIX mLightView;
    D3DXMATRIX mLightProj;

    float fLightNear = 10.0f;
    float fLightFar = 200.0f;

    {
        hr = g_pd3dDevice->GetRenderTarget(0, &oldRT0);
        assert(hr == S_OK);

        hr = g_pd3dDevice->GetDepthStencilSurface(&oldZ);
        assert(hr == S_OK);

        LPDIRECT3DSURFACE9 surfaceLightZ = NULL;

        hr = g_texRenderTargetLightZ->GetSurfaceLevel(0, &surfaceLightZ);
        assert(hr == S_OK);

        hr = g_pd3dDevice->SetRenderTarget(0, surfaceLightZ);
        assert(hr == S_OK);

        hr = g_pd3dDevice->SetDepthStencilSurface(g_surfaceLightZStensil);
        assert(hr == S_OK);

        // Viewport をテクスチャのサイズに変更
        // これをしないと一部のエリアにしか描画されない
        D3DSURFACE_DESC descLightZ { };
        hr = g_texRenderTargetLightZ->GetLevelDesc(0, &descLightZ);
        assert(hr == S_OK);

        D3DVIEWPORT9 oldViewPort { };
        hr = g_pd3dDevice->GetViewport(&oldViewPort);
        assert(hr == S_OK);

        D3DVIEWPORT9 viewPortLightZ{};
        viewPortLightZ.X = 0;
        viewPortLightZ.Y = 0;
        viewPortLightZ.Width  = descLightZ.Width;
        viewPortLightZ.Height = descLightZ.Height;
        viewPortLightZ.MinZ = 0.0f;
        viewPortLightZ.MaxZ = 1.0f;

        hr = g_pd3dDevice->SetViewport(&viewPortLightZ);
        assert(hr == S_OK);

        hr = g_pd3dDevice->Clear(0, NULL,
                                 D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                                 D3DCOLOR_XRGB(255, 255, 255),
                                 1.0f,
                                 0);
        assert(hr == S_OK);

        D3DXVECTOR3 vLightEye(40, 50, -40);
        D3DXVECTOR3 vLightAt(0, 0, 0);
        D3DXVECTOR3 vLightUp(0, 1, 0);
        D3DXMatrixLookAtLH(&mLightView, &vLightEye, &vLightAt, &vLightUp);

        float viewWidth = 70.0f;
        float viewHeight = 70.0f;
        D3DXMatrixOrthoLH(&mLightProj, viewWidth, viewHeight, fLightNear, fLightFar);

        hr = g_pd3dDevice->BeginScene();
        assert(hr == S_OK);

        hr = g_fxDepthBufferShadow->SetTechnique("TechniqueDepthFromLight");
        assert(hr == S_OK);

        hr = g_fxDepthBufferShadow->SetMatrix("g_matLightView", &mLightView);
        assert(hr == S_OK);

        hr = g_fxDepthBufferShadow->SetFloat ("g_lightNear", fLightNear);
        assert(hr == S_OK);

        hr = g_fxDepthBufferShadow->SetFloat ("g_lightFar", fLightFar);
        assert(hr == S_OK);

        UINT nPassNum = 0;
        hr = g_fxDepthBufferShadow->Begin(&nPassNum, 0);
        assert(hr == S_OK);

        hr = g_fxDepthBufferShadow->BeginPass(0);
        assert(hr == S_OK);

        for (int idx = 0; idx < 25; ++idx)
        {
            int gx = idx % 5 - 2;
            int gz = idx / 5 - 2;

            D3DXMATRIX mWorld;
            D3DXMATRIX mWorldViewProjLight;

            D3DXMatrixTranslation(&mWorld,
                                  gx * SPACING,
                                  0.0f,
                                  gz * SPACING);

            mWorldViewProjLight = mWorld * mLightView * mLightProj;
            
            hr = g_fxDepthBufferShadow->SetMatrix("g_matWorld", &mWorld);
            assert(hr == S_OK);

            hr = g_fxDepthBufferShadow->SetMatrix("g_matWorldViewProj", &mWorldViewProjLight);
            assert(hr == S_OK);

            hr = g_fxDepthBufferShadow->CommitChanges();
            assert(hr == S_OK);

            for (DWORD i = 0; i < g_dwNumMaterials; ++i)
            {
                hr = g_pMesh->DrawSubset(i);
                assert(hr == S_OK);
            }
        }

        hr = g_fxDepthBufferShadow->EndPass();
        assert(hr == S_OK);

        hr = g_fxDepthBufferShadow->End();
        assert(hr == S_OK);

        hr = g_pd3dDevice->EndScene();
        assert(hr == S_OK);

        SAFE_RELEASE(surfaceLightZ);
    }

    //------------------------------------------------------------------
    // (2) カメラ視点で描画を行い、各ピクセルについて影か判定し、影を描画
    //------------------------------------------------------------------
    {
        LPDIRECT3DSURFACE9 surfaceShadow= NULL;
        hr = g_texRenderTargetShadow->GetSurfaceLevel(0, &surfaceShadow);
        assert(hr == S_OK);

        hr = g_pd3dDevice->SetRenderTarget(0, surfaceShadow);
        assert(hr == S_OK);

        hr = g_pd3dDevice->SetDepthStencilSurface(oldZ);
        assert(hr == S_OK);

        D3DVIEWPORT9 viewportShadow{};

        viewportShadow.X = 0;
        viewportShadow.Y = 0;

        viewportShadow.Width  = SCREEN_W;
        viewportShadow.Height = SCREEN_H;

        viewportShadow.MinZ = 0.0f;
        viewportShadow.MaxZ = 1.0f;

        g_pd3dDevice->SetViewport(&viewportShadow);

        hr = g_pd3dDevice->Clear(0, NULL,
                                 D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                                 D3DCOLOR_ARGB(0, 0, 0, 0),
                                 1.0f,
                                 0);
        assert(hr == S_OK);

        // カメラ行列
        D3DXMATRIX mView;
        D3DXMATRIX mProj;

        D3DXMatrixPerspectiveFovLH(&mProj,
                                   D3DXToRadian(45.0f),
                                   (float)SCREEN_W / SCREEN_H,
                                   1.0f,
                                   100.0f);

        D3DXVECTOR3 vEye(10.0f * sinf(g_fTime),
                         5.0f,
                         -10.0f * cosf(g_fTime));

        D3DXVECTOR3 vAt(0, 0, 0);
        D3DXVECTOR3 vUp(0, 1, 0);

        D3DXMatrixLookAtLH(&mView, &vEye, &vAt, &vUp);

        D3DXMATRIX mLightViewProj = mLightView * mLightProj;

        hr = g_pd3dDevice->BeginScene();
        assert(hr == S_OK);

        hr = g_fxDepthBufferShadow->SetMatrix("g_matLightViewProj", &mLightViewProj);
        assert(hr == S_OK);

        hr = g_fxDepthBufferShadow->SetMatrix("g_matLightView", &mLightView);
        assert(hr == S_OK);

        hr = g_fxDepthBufferShadow->SetFloat("g_lightNear", fLightNear);
        assert(hr == S_OK);

        hr = g_fxDepthBufferShadow->SetFloat("g_lightFar", fLightFar);
        assert(hr == S_OK);

        hr = g_fxDepthBufferShadow->SetTexture("g_texLightZ", g_texRenderTargetLightZ);
        assert(hr == S_OK);

        D3DSURFACE_DESC descLightZ{};

        hr = g_texRenderTargetLightZ->GetLevelDesc(0, &descLightZ);
        assert(hr == S_OK);

        hr = g_fxDepthBufferShadow->SetFloat("g_shadowTexelW", 1.0f / (float)descLightZ.Width);
        assert(hr == S_OK);

        hr = g_fxDepthBufferShadow->SetFloat("g_shadowTexelH", 1.0f / (float)descLightZ.Height);
        assert(hr == S_OK);

        hr = g_fxDepthBufferShadow->SetFloat("g_shadowBias",   0.001f);
        assert(hr == S_OK);

        float fTime = 0.f;

        if (false)
        {
            fTime = g_fTime;
            fTime *= 0.5;
            fTime = fmodf(fTime, 1.0f);
        }
        else
        {
            fTime = 0.5f;
        }

        hr = g_fxDepthBufferShadow->SetFloat("g_shadowIntensity", fTime);
        assert(hr == S_OK);

        int nBlurSize = 0;

        if (false)
        {
            nBlurSize = (int)(g_fTime * 10);
            nBlurSize = nBlurSize % 13;

            if (nBlurSize % 2 == 0)
            {
                nBlurSize++;
            }
        }
        else
        {
            nBlurSize = 3;
        }

        hr = g_fxDepthBufferShadow->SetInt("g_nBlurSize", nBlurSize);
        assert(hr == S_OK);

        hr = g_fxDepthBufferShadow->SetTechnique("TechniqueWriteShadow");
        assert(hr == S_OK);

        UINT nPassNum = 0;

        hr = g_fxDepthBufferShadow->Begin(&nPassNum, 0);
        assert(hr == S_OK);

        hr = g_fxDepthBufferShadow->BeginPass(0);
        assert(hr == S_OK);

        for (int idx = 0; idx < 25; ++idx)
        {
            int gx = idx % 5 - 2;
            int gz = idx / 5 - 2;

            D3DXMATRIX mWorld;
            D3DXMATRIX mWorldViewProj;
            D3DXMatrixTranslation(&mWorld,
                                  gx * SPACING,
                                  0.0f,
                                  gz * SPACING);

            mWorldViewProj = mWorld * mView * mProj;

            hr = g_fxDepthBufferShadow->SetMatrix("g_matWorld", &mWorld);
            assert(hr == S_OK);

            hr = g_fxDepthBufferShadow->SetMatrix("g_matWorldViewProj", &mWorldViewProj);
            assert(hr == S_OK);

            hr = g_fxDepthBufferShadow->CommitChanges();
            assert(hr == S_OK);

            for (DWORD i = 0; i < g_dwNumMaterials; ++i)
            {
                hr = g_pMesh->DrawSubset(i);
                assert(hr == S_OK);
            }
        }

        hr = g_fxDepthBufferShadow->EndPass();
        assert(hr == S_OK);

        hr = g_fxDepthBufferShadow->End();
        assert(hr == S_OK);

        hr = g_pd3dDevice->EndScene();
        assert(hr == S_OK);

        SAFE_RELEASE(surfaceShadow);
     }

    hr = g_pd3dDevice->SetRenderTarget(0, oldRT0);
    assert(hr == S_OK);

    SAFE_RELEASE(oldRT0);
}

// 通常描画画像と影画像を合成して画面に描画
void RenderPass3()
{
    HRESULT hr = E_FAIL;

    hr = g_pd3dDevice->Clear(0, NULL,
                             D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                             D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    assert(hr == S_OK);

    hr = g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    assert(hr == S_OK);

    hr = g_pd3dDevice->BeginScene();
    assert(hr == S_OK);

    hr = g_fxDepthBufferShadow->SetTechnique("TechniqueComposite");
    assert(hr == S_OK);

    UINT nPassNum = 0;
    hr = g_fxDepthBufferShadow->Begin(&nPassNum, 0);
    assert(hr == S_OK);

    hr = g_fxDepthBufferShadow->BeginPass(0);
    assert(hr == S_OK);

    hr = g_fxDepthBufferShadow->SetTexture("g_texBase", g_texRenderTargetBase);
    assert(hr == S_OK);

    hr = g_fxDepthBufferShadow->SetTexture("g_texShadow", g_texRenderTargetShadow);
    assert(hr == S_OK);

    hr = g_fxDepthBufferShadow->CommitChanges();
    assert(hr == S_OK);

    DrawFullscreenQuad();

    hr = g_fxDepthBufferShadow->EndPass();
    assert(hr == S_OK);

    hr = g_fxDepthBufferShadow->End();
    assert(hr == S_OK);

    // デバッグ用のスプライト表示
    if (false)
    {
        hr = g_pSprite->Begin(D3DXSPRITE_ALPHABLEND);
        assert(hr == S_OK);

        D3DXMATRIX m;
        // 左上
        {
            D3DXVECTOR2 sc(0.25f, 0.25f);
            D3DXVECTOR2 rotCenter(0, 0);
            D3DXVECTOR2 trans(0.0f, 0.0f);

            D3DXMatrixTransformation2D(&m, NULL, 0.0f, &sc, &rotCenter, 0.0f, &trans);

            hr = g_pSprite->SetTransform(&m);
            assert(hr == S_OK);

            hr = g_pSprite->Draw(g_texRenderTargetLightZ, NULL, NULL, NULL, 0xFFFFFFFF);
            assert(hr == S_OK);
        }

        // 左下
        {
            D3DXVECTOR2 sc(0.5f, 0.5f);
            D3DXVECTOR2 rotCenter(0, 0);
            D3DXVECTOR2 trans(0.0f, SCREEN_H * 0.5f);

            D3DXMatrixTransformation2D(&m, NULL, 0.0f, &sc, &rotCenter, 0.0f, &trans);

            hr = g_pSprite->SetTransform(&m);
            assert(hr == S_OK);

            hr = g_pSprite->Draw(g_texRenderTargetShadow, NULL, NULL, NULL, 0xFFFFFFFF);
            assert(hr == S_OK);
        }

        hr = g_pSprite->End();
        assert(hr == S_OK);
    }

    hr = g_pd3dDevice->EndScene();
    assert(hr == S_OK);

    hr = g_pd3dDevice->Present(NULL,NULL,NULL,NULL);
    assert(hr == S_OK);

    hr = g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
    assert(hr == S_OK);
}

void DrawFullscreenQuad()
{
    QuadVertex v[4] { };

    float du = 0.5f / (float)SCREEN_W;
    float dv = 0.5f / (float)SCREEN_H;

    v[0].x = -1.0f;
    v[0].y = -1.0f;
    v[0].z = 0.0f;
    v[0].w = 1.0f;
    v[0].u = 0.0f + du;
    v[0].v = 1.0f - dv;

    v[1].x = -1.0f;
    v[1].y = 1.0f;
    v[1].z = 0.0f;
    v[1].w = 1.0f;
    v[1].u = 0.0f + du;
    v[1].v = 0.0f + dv;

    v[2].x = 1.0f;
    v[2].y = -1.0f;
    v[2].z = 0.0f;
    v[2].w = 1.0f;
    v[2].u = 1.0f - du;
    v[2].v = 1.0f - dv;

    v[3].x = 1.0f;
    v[3].y = 1.0f;
    v[3].z = 0.0f;
    v[3].w = 1.0f;
    v[3].u = 1.0f - du;
    v[3].v = 0.0f + dv;

    HRESULT hr = E_FAIL;

    hr = g_pd3dDevice->SetVertexDeclaration(g_pQuadDecl);
    assert(hr == S_OK);

    hr = g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(QuadVertex));
    assert(hr == S_OK);
}

LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
    {
        PostQuitMessage(0);
        g_bClose = true;
        return 0;
    }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
