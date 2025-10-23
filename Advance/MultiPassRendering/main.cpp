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

LPDIRECT3D9 g_pD3D = NULL;
LPDIRECT3DDEVICE9 g_pd3dDevice = NULL;
LPD3DXMESH g_pMesh = NULL;

std::vector<D3DMATERIAL9> g_pMaterials;
std::vector<LPDIRECT3DTEXTURE9> g_pTextures;
DWORD g_dwNumMaterials = 0;
LPD3DXEFFECT g_pEffect1 = NULL;
LPD3DXEFFECT g_pEffect2 = NULL;

bool g_bClose = false;

LPDIRECT3DTEXTURE9 g_pRenderTarget = NULL;
LPDIRECT3DTEXTURE9 g_pRenderTarget2 = NULL;
LPDIRECT3DTEXTURE9 g_pPostTexture = NULL;

LPDIRECT3DSURFACE9 g_pShadowZ = NULL;

LPDIRECT3DTEXTURE9  g_pShadowMap1 = NULL;  // 遠景用
LPDIRECT3DSURFACE9  g_pShadowZ1   = NULL;  // 遠景用 DS

LPDIRECT3DVERTEXDECLARATION9 g_pQuadDecl = NULL;

// デバッグ確認用
LPD3DXSPRITE g_pSprite = NULL;

float g_fTime = 0.0f;
float SPACING = 15.0f; // 5x5 グリッドの間隔

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
            Sleep(16);

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
                                       &g_pEffect1,
                                       NULL);
    assert(hResult == S_OK);

    hResult = D3DXCreateEffectFromFile(g_pd3dDevice,
                                       _T("simple2.fx"),
                                       NULL,
                                       NULL,
                                       D3DXSHADER_DEBUG,
                                       NULL,
                                       &g_pEffect2,
                                       NULL);
    assert(hResult == S_OK);

    hResult = D3DXCreateTexture(g_pd3dDevice,
                                SCREEN_W, SCREEN_H,
                                1,
                                D3DUSAGE_RENDERTARGET,
                                D3DFMT_A8R8G8B8,
                                D3DPOOL_DEFAULT,
                                &g_pRenderTarget);
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
                                    &g_pRenderTarget2);
    }
    else
    {
        hResult = D3DXCreateTexture(g_pd3dDevice,
                                    SCREEN_W * 2,
                                    SCREEN_H * 2,
                                    1,
                                    D3DUSAGE_RENDERTARGET,
                                    //D3DFMT_A16B16G16R16,
                                    D3DFMT_R32F,
                                    D3DPOOL_DEFAULT,
                                    &g_pRenderTarget2);
    }

    assert(hResult == S_OK);

    D3DSURFACE_DESC bdesc{};
    g_pRenderTarget2->GetLevelDesc(0, &bdesc);

    // 影用の深度ステンシル（サイズをRT2に合わせる）
    HRESULT hr = g_pd3dDevice->CreateDepthStencilSurface(bdesc.Width,
                                                         bdesc.Height,
                                                         D3DFMT_D16,
                                                         //D3DFMT_D24S8,
                                                         D3DMULTISAMPLE_NONE,
                                                         0,
                                                         TRUE,
                                                         &g_pShadowZ,
                                                         NULL);
    assert(hr == S_OK);

    // g_pRenderTarget2 作成の直後あたりに追加
    {
        HRESULT hr = D3DXCreateTexture(g_pd3dDevice,
                                       SCREEN_W, SCREEN_H,
                                       1,
                                       D3DUSAGE_RENDERTARGET,
                                       D3DFMT_R32F,        // 近景と同じ
                                       D3DPOOL_DEFAULT,
                                       &g_pShadowMap1);
        assert(hr == S_OK);

        D3DSURFACE_DESC d{};
        g_pShadowMap1->GetLevelDesc(0, &d);

        hr = g_pd3dDevice->CreateDepthStencilSurface(
                d.Width, d.Height,
                D3DFMT_D16,                 // 近景と同じ形式でOK
                D3DMULTISAMPLE_NONE, 0, TRUE,
                &g_pShadowZ1, NULL);
        assert(hr == S_OK);
    }

    hResult = D3DXCreateTexture(g_pd3dDevice,
                                SCREEN_W,
                                SCREEN_H,
                                1,
                                D3DUSAGE_RENDERTARGET,
                                D3DFMT_A8R8G8B8,
                                D3DPOOL_DEFAULT,
                                &g_pPostTexture);
    assert(hResult == S_OK);

    // フルスクリーンクアッドの頂宣言
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
    SAFE_RELEASE(g_pEffect1);
    SAFE_RELEASE(g_pEffect2);

    SAFE_RELEASE(g_pRenderTarget);
    SAFE_RELEASE(g_pRenderTarget2);
    SAFE_RELEASE(g_pPostTexture);
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

    LPDIRECT3DSURFACE9 rtA = NULL;
    LPDIRECT3DSURFACE9 rtDummy = NULL;

    hr = g_pRenderTarget->GetSurfaceLevel(0, &rtA);
    assert(hr == S_OK);

    hr = g_pRenderTarget2->GetSurfaceLevel(0, &rtDummy);
    assert(hr == S_OK);

    hr = g_pd3dDevice->SetRenderTarget(0, rtA);
    assert(hr == S_OK);

    hr = g_pd3dDevice->SetRenderTarget(1, NULL);
    assert(hr == S_OK);

    // カメラ行列（V/P）
    D3DXMATRIX V, P;
    D3DXMatrixPerspectiveFovLH(&P,
                               D3DXToRadian(45.0f),
                               (float)SCREEN_W / SCREEN_H,
                               1.0f,
                               100.0f);

    D3DXVECTOR3 eye(10.0f * sinf(g_fTime), 5.0f, -10.0f * cosf(g_fTime));
    D3DXVECTOR3 at(0, 0, 0);
    D3DXVECTOR3 up(0, 1, 0);

    D3DXMatrixLookAtLH(&V, &eye, &at, &up);

    // クリア
    hr = g_pd3dDevice->Clear(0, NULL,
                             D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                             D3DCOLOR_XRGB(100, 100, 100),
                             1.0f,
                             0);
    assert(hr == S_OK);

    hr = g_pd3dDevice->BeginScene();
    assert(hr == S_OK);

    // 通常描画（TechniqueMRT）
    hr = g_pEffect1->SetTechnique("TechniqueMRT");
    assert(hr == S_OK);
    UINT np = 0;

    hr = g_pEffect1->Begin(&np, 0);
    assert(hr == S_OK);

    hr = g_pEffect1->BeginPass(0);
    assert(hr == S_OK);

    hr = g_pEffect1->SetBool("g_bUseTexture", TRUE);
    assert(hr == S_OK);

    for (int idx = 0; idx < 25; ++idx)
    {
        int gx = idx % 5 - 2;
        int gz = idx / 5 - 2;

        // W と WVP を個体ごとにセット
        D3DXMATRIX W, WVP;
        D3DXMatrixTranslation(&W, gx * SPACING, 0.0f, gz * SPACING);
        WVP = W * V * P;

        hr = g_pEffect1->SetMatrix("g_matWorldViewProj", &WVP);
        assert(hr == S_OK);

        for (DWORD i = 0; i < g_dwNumMaterials; ++i)
        {
            hr = g_pEffect1->SetTexture("g_textureBase", g_pTextures[i]);
            assert(hr == S_OK);

            hr = g_pEffect1->CommitChanges();
            assert(hr == S_OK);

            hr = g_pMesh->DrawSubset(i);
            assert(hr == S_OK);
        }
    }

    hr = g_pEffect1->EndPass();
    assert(hr == S_OK);

    hr = g_pEffect1->End();
    assert(hr == S_OK);

    hr = g_pd3dDevice->EndScene();
    assert(hr == S_OK);

    // 後片付け
    hr = g_pd3dDevice->SetRenderTarget(0, oldRT0);
    assert(hr == S_OK);

    hr = g_pd3dDevice->SetRenderTarget(1, NULL);
    assert(hr == S_OK);

    SAFE_RELEASE(rtA);
    SAFE_RELEASE(rtDummy);
    SAFE_RELEASE(oldRT0);
}

// 2カスケードのシャドウ（近景：g_pRenderTarget2、遠景：g_pShadowMap1）を描き、
// C（g_pPostTexture）で影マスクを作るパス
void RenderPass2()
{
    // ===== ライト View（方向光） =====
    D3DXMATRIX Lview;
    {
        D3DXVECTOR3 leye(40.0f, 50.0f, -40.0f), lat(0,0,0), lup(0,1,0);
        D3DXMatrixLookAtLH(&Lview, &leye, &lat, &lup);
    }

    // ===== 近景カスケードの設定（①で使用、③でも同じ値を再使用） =====
    float lNear0 = 1.0f,  lFar0 = 140.0f;
    float ow0    = 30.0f, oh0   = 30.0f;
    D3DXMATRIX Lproj0; D3DXMatrixOrthoLH(&Lproj0, ow0, oh0, lNear0, lFar0);

    // ===== 遠景カスケードの設定（②で使用、③でも同じ値を再使用） =====
    float lNear1 = 1.0f,  lFar1 = 300.0f;
    float ow1    = 100.0f, oh1  = 100.0f;
    D3DXMATRIX Lproj1; D3DXMatrixOrthoLH(&Lproj1, ow1, oh1, lNear1, lFar1);

    // ==========================================================
    // ① 近景シャドウマップ（shadow0 = g_pRenderTarget2）へ描画
    // ==========================================================
    {
        // RT/DS 退避
        LPDIRECT3DSURFACE9 oldRT0=NULL, oldDS=NULL;
        g_pd3dDevice->GetRenderTarget(0, &oldRT0);
        g_pd3dDevice->GetDepthStencilSurface(&oldDS);

        // 近景 RT0
        LPDIRECT3DSURFACE9 rt0=NULL; g_pRenderTarget2->GetSurfaceLevel(0, &rt0);
        g_pd3dDevice->SetRenderTarget(0, rt0);
        g_pd3dDevice->SetDepthStencilSurface(g_pShadowZ);

        // VP を RT サイズに
        D3DSURFACE_DESC d0{}; g_pRenderTarget2->GetLevelDesc(0, &d0);
        D3DVIEWPORT9 vp0{0,0,(DWORD)d0.Width,(DWORD)d0.Height,0.0f,1.0f};
        g_pd3dDevice->SetViewport(&vp0);

        // 白クリア（未描画=1.0）
        g_pd3dDevice->Clear(0,NULL, D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,
                            D3DCOLOR_XRGB(255,255,255), 1.0f, 0);

        // ライト深度テクニック
        g_pd3dDevice->BeginScene();
        g_pEffect2->SetTechnique("TechniqueDepthFromLight");
        g_pEffect2->SetMatrix("g_matLightView", &Lview);
        g_pEffect2->SetFloat ("g_lightNear", lNear0);
        g_pEffect2->SetFloat ("g_lightFar",  lFar0);

        UINT np=0; g_pEffect2->Begin(&np,0); g_pEffect2->BeginPass(0);
        for (int idx=0; idx<25; ++idx)
        {
            int gx=idx%5-2, gz=idx/5-2;
            D3DXMATRIX W, LWVP; D3DXMatrixTranslation(&W, gx*SPACING, 0, gz*SPACING);
            LWVP = W * Lview * Lproj0;

            g_pEffect2->SetMatrix("g_matWorld",         &W);
            g_pEffect2->SetMatrix("g_matWorldViewProj", &LWVP);
            g_pEffect2->CommitChanges();

            for (DWORD i=0;i<g_dwNumMaterials;++i) g_pMesh->DrawSubset(i);
        }
        g_pEffect2->EndPass(); g_pEffect2->End();
        g_pd3dDevice->EndScene();

        // 復帰
        g_pd3dDevice->SetDepthStencilSurface(oldDS);
        g_pd3dDevice->SetRenderTarget(0, oldRT0);
        SAFE_RELEASE(rt0); SAFE_RELEASE(oldRT0); SAFE_RELEASE(oldDS);
    }

    // ==========================================================
    // ② 遠景シャドウマップ（shadow1 = g_pShadowMap1）へ描画
    // ==========================================================
    {
        LPDIRECT3DSURFACE9 oldRT0=NULL, oldDS=NULL;
        g_pd3dDevice->GetRenderTarget(0, &oldRT0);
        g_pd3dDevice->GetDepthStencilSurface(&oldDS);

        LPDIRECT3DSURFACE9 rt1=NULL; g_pShadowMap1->GetSurfaceLevel(0, &rt1);
        g_pd3dDevice->SetRenderTarget(0, rt1);
        g_pd3dDevice->SetDepthStencilSurface(g_pShadowZ1);

        D3DSURFACE_DESC d1{}; g_pShadowMap1->GetLevelDesc(0, &d1);
        D3DVIEWPORT9 vp1{0,0,(DWORD)d1.Width,(DWORD)d1.Height,0.0f,1.0f};
        g_pd3dDevice->SetViewport(&vp1);

        g_pd3dDevice->Clear(0,NULL, D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,
                            D3DCOLOR_XRGB(255,255,255), 1.0f, 0);

        g_pd3dDevice->BeginScene();
        g_pEffect2->SetTechnique("TechniqueDepthFromLight");
        g_pEffect2->SetMatrix("g_matLightView", &Lview);
        g_pEffect2->SetFloat ("g_lightNear", lNear1);
        g_pEffect2->SetFloat ("g_lightFar",  lFar1);

        UINT np=0; g_pEffect2->Begin(&np,0); g_pEffect2->BeginPass(0);
        for (int idx=0; idx<25; ++idx)
        {
            int gx=idx%5-2, gz=idx/5-2;
            D3DXMATRIX W, LWVP; D3DXMatrixTranslation(&W, gx*SPACING, 0, gz*SPACING);
            LWVP = W * Lview * Lproj1;

            g_pEffect2->SetMatrix("g_matWorld",         &W);
            g_pEffect2->SetMatrix("g_matWorldViewProj", &LWVP);
            g_pEffect2->CommitChanges();

            for (DWORD i=0;i<g_dwNumMaterials;++i) g_pMesh->DrawSubset(i);
        }
        g_pEffect2->EndPass(); g_pEffect2->End();
        g_pd3dDevice->EndScene();

        g_pd3dDevice->SetDepthStencilSurface(oldDS);
        g_pd3dDevice->SetRenderTarget(0, oldRT0);
        SAFE_RELEASE(rt1); SAFE_RELEASE(oldRT0); SAFE_RELEASE(oldDS);
    }

    // ==========================================================
    // ③ カメラからの描画（C=g_pPostTexture）— 2枚のSMを使って影マスク生成
    // ==========================================================
    {
        LPDIRECT3DSURFACE9 oldRT0=NULL, oldDS=NULL;
        g_pd3dDevice->GetRenderTarget(0, &oldRT0);
        g_pd3dDevice->GetDepthStencilSurface(&oldDS);

        // C（ポストテクスチャ）へ
        LPDIRECT3DSURFACE9 rtC=NULL; g_pPostTexture->GetSurfaceLevel(0, &rtC);
        g_pd3dDevice->SetRenderTarget(0, rtC);
        // 画面用 DS（AutoDepthStencil）を使用
        g_pd3dDevice->SetDepthStencilSurface(oldDS);

        D3DVIEWPORT9 vpC{0,0,(DWORD)SCREEN_W,(DWORD)SCREEN_H,0.0f,1.0f};
        g_pd3dDevice->SetViewport(&vpC);

        g_pd3dDevice->Clear(0,NULL, D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,
                            D3DCOLOR_ARGB(0,0,0,0), 1.0f, 0);

        // カメラ行列
        D3DXMATRIX V,P;
        D3DXMatrixPerspectiveFovLH(&P, D3DXToRadian(45.0f), (float)SCREEN_W/SCREEN_H, 1.0f, 100.0f);
        D3DXVECTOR3 eye(10.0f*sinf(g_fTime),5.0f,-10.0f*cosf(g_fTime)), at(0,0,0), up(0,1,0);
        D3DXMatrixLookAtLH(&V, &eye, &at, &up);

        // ①/② と **同じ** LVP と near/far をセット（ここがズレると影が出ない）
        D3DXMATRIX LVP0 = Lview * Lproj0;
        D3DXMATRIX LVP1 = Lview * Lproj1;

        g_pd3dDevice->BeginScene();

        g_pEffect2->SetTechnique("TechniqueWorldPos");
        g_pEffect2->SetMatrix("g_matView", &V);

        g_pEffect2->SetMatrix("g_LVP0", &LVP0);
        g_pEffect2->SetMatrix("g_LVP1", &LVP1);
        g_pEffect2->SetFloat ("g_lNear0", lNear0);
        g_pEffect2->SetFloat ("g_lFar0",  lFar0);
        g_pEffect2->SetFloat ("g_lNear1", lNear1);
        g_pEffect2->SetFloat ("g_lFar1",  lFar1);

        g_pEffect2->SetTexture("shadow0", g_pRenderTarget2);
        g_pEffect2->SetTexture("shadow1", g_pShadowMap1);

        // 各SMの 1/サイズ を設定（PCF ステップ用）
        D3DSURFACE_DESC sd0{}, sd1{};
        g_pRenderTarget2->GetLevelDesc(0, &sd0);
        g_pShadowMap1  ->GetLevelDesc(0, &sd1);
        g_pEffect2->SetFloat("g_texelW0", 1.0f/sd0.Width);
        g_pEffect2->SetFloat("g_texelH0", 1.0f/sd0.Height);
        g_pEffect2->SetFloat("g_texelW1", 1.0f/sd1.Width);
        g_pEffect2->SetFloat("g_texelH1", 1.0f/sd1.Height);

        g_pEffect2->SetFloat("g_shadowBias", 0.001f);
        g_pEffect2->SetFloat("g_splitZ",     15.0f);
        g_pEffect2->SetFloat("g_blendZ",      0.0f); // 最初は0で切替を確認

        UINT np=0; g_pEffect2->Begin(&np,0); g_pEffect2->BeginPass(0);
        for (int idx=0; idx<25; ++idx)
        {
            int gx=idx%5-2, gz=idx/5-2;
            D3DXMATRIX W, WVP; D3DXMatrixTranslation(&W, gx*SPACING, 0, gz*SPACING);
            WVP = W * V * P;

            g_pEffect2->SetMatrix("g_matWorld",         &W);
            g_pEffect2->SetMatrix("g_matWorldViewProj", &WVP);
            g_pEffect2->CommitChanges();

            for (DWORD i=0;i<g_dwNumMaterials;++i) g_pMesh->DrawSubset(i);
        }
        g_pEffect2->EndPass(); g_pEffect2->End();
        g_pd3dDevice->EndScene();

        // 復帰
        g_pd3dDevice->SetRenderTarget(0, oldRT0);
        SAFE_RELEASE(rtC); SAFE_RELEASE(oldRT0); SAFE_RELEASE(oldDS);
    }
}

void RenderPass3()
{
    HRESULT hr = E_FAIL;

    hr = g_pd3dDevice->Clear(0, NULL,
                             D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                             D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    assert(hr == S_OK);

    hr = g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    assert(hr == S_OK);

    // --- (A,C) 合成をバックバッファに描く ---
    hr = g_pd3dDevice->BeginScene();
    assert(hr == S_OK);

    hr = g_pEffect2->SetTechnique("TechniqueComposite");
    assert(hr == S_OK);

    UINT np=0;
    hr = g_pEffect2->Begin(&np,0);
    assert(hr == S_OK);

    hr = g_pEffect2->BeginPass(0);
    assert(hr == S_OK);

    // texture1=A, texture2=C
    hr = g_pEffect2->SetTexture("texture1", g_pRenderTarget);
    assert(hr == S_OK);

    hr = g_pEffect2->SetTexture("texture2", g_pPostTexture);
    assert(hr == S_OK);

    hr = g_pEffect2->CommitChanges();
    assert(hr == S_OK);

    DrawFullscreenQuad();

    hr = g_pEffect2->EndPass();
    assert(hr == S_OK);

    hr = g_pEffect2->End();
    assert(hr == S_OK);

    // --- デバッグ小窓：B(左上), C(左下) を 1/2 スケールでスプライト表示 ---
    if (false)
    {
        hr = g_pSprite->Begin(D3DXSPRITE_ALPHABLEND);
        assert(hr == S_OK);

        D3DXMATRIX m;
        // 左上
        {
            D3DXVECTOR2 sc(0.5f, 0.5f);
            D3DXVECTOR2 rotCenter(0, 0);
            D3DXVECTOR2 trans(0.0f, 0.0f);

            D3DXMatrixTransformation2D(&m, NULL, 0.0f, &sc, &rotCenter, 0.0f, &trans);

            hr = g_pSprite->SetTransform(&m);
            assert(hr == S_OK);

            hr = g_pSprite->Draw(g_pRenderTarget2, NULL, NULL, NULL, 0xFFFFFFFF);
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

            hr = g_pSprite->Draw(g_pShadowMap1, NULL, NULL, NULL, 0xFFFFFFFF);
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
