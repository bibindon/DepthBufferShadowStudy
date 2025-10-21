// main.cpp - 簡素化版
// MRT3でSSAOを実装

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9d.lib")

#include <d3d9.h>
#include <d3dx9.h>
#include <tchar.h>
#include <vector>

#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p)=NULL; } } while(0)

static const int kBackW = 1600;
static const int kBackH = 900;

LPDIRECT3D9                     g_pD3D = NULL;
LPDIRECT3DDEVICE9               g_pd3dDevice = NULL;
LPD3DXMESH                      g_pMeshMonkey = NULL;
LPD3DXMESH                      g_pMeshObstacle = NULL;
LPD3DXMESH                      g_pMeshSky = NULL;
std::vector<LPDIRECT3DTEXTURE9> g_pTexMonkey;
std::vector<LPDIRECT3DTEXTURE9> g_pTexObstacle;
std::vector<LPDIRECT3DTEXTURE9> g_pTexSky;
DWORD                           g_dwNumMaterials = 0;

LPD3DXEFFECT                    g_pEffect1 = NULL; // simple.fx
LPD3DXEFFECT                    g_pEffect2 = NULL; // simple2.fx

// MRT: 3枚
LPDIRECT3DTEXTURE9              g_pRenderTarget = NULL;  // RT0: color
LPDIRECT3DTEXTURE9              g_pRenderTargetZ = NULL; // RT1: Z画像
LPDIRECT3DTEXTURE9              g_pRenderTargetPos = NULL; // RT2: POS画像
LPDIRECT3DTEXTURE9              g_pAoTex = NULL; // AO(生)
LPDIRECT3DTEXTURE9              g_pAoTempBlur = NULL;

LPDIRECT3DVERTEXDECLARATION9    g_pQuadDecl = NULL;
bool                            g_bClose = false;

float                           g_posRange = 16.f;
bool                            g_bUseTexture = true;
bool                            g_bUseBlur = true;

D3DXMATRIX                      g_mView;
D3DXMATRIX                      g_mProj;

struct QuadVertex {
    float x, y, z, w;
    float u, v;
};

static void InitD3D(HWND hWnd);
static void Cleanup();
static void RenderPass1();
static void RenderPass2();
static void DrawFullscreenQuad();
LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern int WINAPI _tWinMain(_In_ HINSTANCE hInstance,
                            _In_opt_ HINSTANCE hPrevInstance,
                            _In_ LPWSTR lpCmdLine,
                            _In_ int nShowCmd);

int WINAPI _tWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR lpCmdLine,
                     _In_ int nShowCmd)
{
    WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = MsgProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = _T("SSAODemo");
    RegisterClassEx(&wc);

    RECT rc = { 0,0,kBackW,kBackH };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hWnd = CreateWindow(_T("SSAODemo"),
                             _T("SSAO Demo"),
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT,
                             CW_USEDEFAULT,
                             rc.right - rc.left,
                             rc.bottom - rc.top,
                             NULL,
                             NULL,
                             hInstance,
                             NULL);

    InitD3D(hWnd);
    ShowWindow(hWnd, SW_SHOWDEFAULT);

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
            RenderPass1();
            RenderPass2();
            g_pd3dDevice->Present(NULL, NULL, NULL, NULL);
        }

        if (g_bClose)
        {
            break;
        }
    }

    Cleanup();
    UnregisterClass(_T("SSAODemo"), hInstance);
    return 0;
}

void InitD3D(HWND hWnd)
{
    g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D16;
    pp.hDeviceWindow = hWnd;
    pp.MultiSampleType = D3DMULTISAMPLE_NONE;

    g_pD3D->CreateDevice(D3DADAPTER_DEFAULT,
                         D3DDEVTYPE_HAL,
                         hWnd,
                         D3DCREATE_HARDWARE_VERTEXPROCESSING,
                         &pp,
                         &g_pd3dDevice);

    // monkey.blend.xロード
    {
        LPD3DXBUFFER pMtrlBuf = NULL;
        D3DXLoadMeshFromX(L"monkey.blend.x",
                          D3DXMESH_SYSTEMMEM,
                          g_pd3dDevice,
                          NULL,
                          &pMtrlBuf,
                          NULL,
                          &g_dwNumMaterials,
                          &g_pMeshMonkey);

        D3DXMATERIAL* mtrls = (D3DXMATERIAL*)pMtrlBuf->GetBufferPointer();
        g_pTexMonkey.resize(g_dwNumMaterials, NULL);

        for (DWORD i = 0; i < g_dwNumMaterials; ++i)
        {
            if (mtrls[i].pTextureFilename && mtrls[i].pTextureFilename[0] != '\0')
            {
                LPDIRECT3DTEXTURE9 tex = NULL;
                D3DXCreateTextureFromFileA(g_pd3dDevice, mtrls[i].pTextureFilename, &tex);
                g_pTexMonkey[i] = tex;
            }
        }
        pMtrlBuf->Release();
    }

    // 障害物
    // sphere.xロード
    {
        LPD3DXBUFFER pMtrlBuf = NULL;
        D3DXLoadMeshFromX(L"sphere.x",
                          D3DXMESH_SYSTEMMEM,
                          g_pd3dDevice,
                          NULL,
                          &pMtrlBuf,
                          NULL,
                          &g_dwNumMaterials,
                          &g_pMeshObstacle);

        D3DXMATERIAL* mtrls = (D3DXMATERIAL*)pMtrlBuf->GetBufferPointer();
        g_pTexObstacle.resize(g_dwNumMaterials, NULL);

        for (DWORD i = 0; i < g_dwNumMaterials; ++i)
        {
            if (mtrls[i].pTextureFilename && mtrls[i].pTextureFilename[0] != '\0')
            {
                LPDIRECT3DTEXTURE9 tex = NULL;
                D3DXCreateTextureFromFileA(g_pd3dDevice, mtrls[i].pTextureFilename, &tex);
                g_pTexObstacle[i] = tex;
            }
        }
        pMtrlBuf->Release();
    }

    // sky.blend.xロード
    {
        LPD3DXBUFFER pMtrlBuf = NULL;
        D3DXLoadMeshFromX(_T("sky.blend.x"), D3DXMESH_SYSTEMMEM, g_pd3dDevice,
                          NULL, &pMtrlBuf, NULL, &g_dwNumMaterials,
                          &g_pMeshSky);

        D3DXMATERIAL* mtrls = (D3DXMATERIAL*)pMtrlBuf->GetBufferPointer();
        g_pTexSky.resize(g_dwNumMaterials, NULL);

        for (DWORD i = 0; i < g_dwNumMaterials; ++i)
        {
            if (mtrls[i].pTextureFilename && mtrls[i].pTextureFilename[0] != '\0')
            {
                LPDIRECT3DTEXTURE9 tex = NULL;
                D3DXCreateTextureFromFileA(g_pd3dDevice, mtrls[i].pTextureFilename, &tex);
                g_pTexSky[i] = tex;
            }
        }
        pMtrlBuf->Release();
    }


    // エフェクト
    D3DXCreateEffectFromFile(g_pd3dDevice,
                             _T("../x64/Debug/simple.cso"),
                             NULL,
                             NULL,
                             0,
                             NULL,
                             &g_pEffect1,
                             NULL);

    D3DXCreateEffectFromFile(g_pd3dDevice,
                             _T("../x64/Debug/simple2.cso"),
                             NULL,
                             NULL,
                             0,
                             NULL,
                             &g_pEffect2,
                             NULL);

    D3DXCreateTexture(g_pd3dDevice,
                      kBackW,
                      kBackH,
                      1,
                      D3DUSAGE_RENDERTARGET,
                      D3DFMT_A8R8G8B8,
                      D3DPOOL_DEFAULT,
                      &g_pRenderTarget);

    D3DXCreateTexture(g_pd3dDevice,
                      kBackW,
                      kBackH,
                      1,
                      D3DUSAGE_RENDERTARGET,
                      D3DFMT_A16B16G16R16F,
                      D3DPOOL_DEFAULT,
                      &g_pRenderTargetZ);

    D3DXCreateTexture(g_pd3dDevice,
                      kBackW,
                      kBackH,
                      1,
                      D3DUSAGE_RENDERTARGET,
                      D3DFMT_A16B16G16R16F,
                      D3DPOOL_DEFAULT,
                      &g_pRenderTargetPos);

    D3DXCreateTexture(g_pd3dDevice,
                      kBackW,
                      kBackH,
                      1,
                      D3DUSAGE_RENDERTARGET,
                      D3DFMT_A8R8G8B8,
                      D3DPOOL_DEFAULT,
                      &g_pAoTex);

    D3DXCreateTexture(g_pd3dDevice,
                      kBackW,
                      kBackH,
                      1,
                      D3DUSAGE_RENDERTARGET,
                      D3DFMT_A8R8G8B8,
                      D3DPOOL_DEFAULT,
                      &g_pAoTempBlur);

    D3DVERTEXELEMENT9 elems[] =
    {
        {0, 0,  D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        D3DDECL_END()
    };

    g_pd3dDevice->CreateVertexDeclaration(elems, &g_pQuadDecl);
}

void Cleanup()
{
    for (size_t i = 0; i < g_pTexMonkey.size(); ++i)
    {
        SAFE_RELEASE(g_pTexMonkey[i]);
    }

    SAFE_RELEASE(g_pMeshMonkey);
    SAFE_RELEASE(g_pMeshObstacle);
    SAFE_RELEASE(g_pMeshSky);
    SAFE_RELEASE(g_pEffect1);
    SAFE_RELEASE(g_pEffect2);
    SAFE_RELEASE(g_pRenderTarget);
    SAFE_RELEASE(g_pRenderTargetZ);
    SAFE_RELEASE(g_pRenderTargetPos);
    SAFE_RELEASE(g_pAoTex);
    SAFE_RELEASE(g_pAoTempBlur);
    SAFE_RELEASE(g_pQuadDecl);
    SAFE_RELEASE(g_pd3dDevice);
    SAFE_RELEASE(g_pD3D);
}

void RenderPass1()
{
    LPDIRECT3DSURFACE9 pOldRT0 = NULL;
    g_pd3dDevice->GetRenderTarget(0, &pOldRT0);

    LPDIRECT3DSURFACE9 pRT0 = NULL;
    LPDIRECT3DSURFACE9 pRT1 = NULL;
    LPDIRECT3DSURFACE9 pRT2 = NULL;

    g_pRenderTarget->GetSurfaceLevel(0, &pRT0);
    g_pRenderTargetZ->GetSurfaceLevel(0, &pRT1);
    g_pRenderTargetPos->GetSurfaceLevel(0, &pRT2);

    g_pd3dDevice->SetRenderTarget(0, pRT0);
    g_pd3dDevice->SetRenderTarget(1, pRT1);
    g_pd3dDevice->SetRenderTarget(2, pRT2);

    static float t = 0.0f;
    t += -0.01f;

    D3DXMATRIX mWorld;
    D3DXMATRIX mView;
    D3DXMATRIX mProj;
    D3DXMATRIX mWorldViewProj;

    D3DXMatrixIdentity(&mWorld);

    D3DXVECTOR3 eye(10.0f * sinf(t), 5.0f, -10.0f * cosf(t));
    D3DXVECTOR3 at(0, 2, 0);
    D3DXVECTOR3 up(0, 1, 0);
    D3DXMatrixLookAtLH(&mView, &eye, &at, &up);

    D3DXMatrixPerspectiveFovLH(&mProj,
                               D3DXToRadian(45.0f),
                               (float)kBackW / (float)kBackH,
                               1.0f,
                               1000.0f);

    mWorldViewProj = mWorld * mView * mProj;

    g_pd3dDevice->Clear(0,
                        NULL,
                        D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                        D3DCOLOR_XRGB(100, 100, 100),
                        1.0f,
                        0);

    g_pd3dDevice->BeginScene();

    g_pEffect1->SetMatrix("g_matWorld", &mWorld);
    g_pEffect1->SetMatrix("g_matView", &mView);
    g_pEffect1->SetMatrix("g_matWorldViewProj", &mWorldViewProj);
    g_pEffect1->SetFloat("g_fNear", 1.0f);
    g_pEffect1->SetFloat("g_fFar", 1000.0f);
    g_pEffect1->SetFloat("g_posRange", g_posRange);

    g_pEffect1->SetTechnique("TechniqueMRT");
    UINT nPass = 0;
    g_pEffect1->Begin(&nPass, 0);
    g_pEffect1->BeginPass(0);

    for (DWORD i = 0; i < g_dwNumMaterials; ++i)
    {
        if (g_pTexMonkey[i])
        {
            if (g_bUseTexture)
            {
                g_pEffect1->SetBool("g_bUseTexture", TRUE);
                g_pEffect1->SetTexture("g_texBase", g_pTexMonkey[i]);
            }
        }
        else
        {
            g_pEffect1->SetBool("g_bUseTexture", FALSE);
            g_pEffect1->SetTexture("g_texBase", NULL);
        }
        g_pEffect1->CommitChanges();
        g_pMeshMonkey->DrawSubset(i);
    }

    static float t2 = 0.0f;
    t2 += 0.02f;
    D3DXMatrixTranslation(&mWorld, cosf(t2) * 2, sinf(t2) * 2 - 2.0f, sinf(t2) * 2);
    g_pEffect1->SetMatrix("g_matWorld", &mWorld);
    for (DWORD i = 0; i < g_dwNumMaterials; ++i)
    {
        if (g_pTexObstacle[i])
        {
            if (g_bUseTexture)
            {
                g_pEffect1->SetBool("g_bUseTexture", TRUE);
                g_pEffect1->SetTexture("g_texBase", g_pTexObstacle[i]);
            }
        }
        else
        {
            g_pEffect1->SetBool("g_bUseTexture", FALSE);
            g_pEffect1->SetTexture("g_texBase", NULL);
        }
        g_pEffect1->CommitChanges();
        g_pMeshObstacle->DrawSubset(0);
    }

//    for (DWORD i = 0; i < g_dwNumMaterials; ++i)
//    {
//        if (g_pTexSky[i])
//        {
//            if (g_bUseTexture)
//            {
//                g_pEffect1->SetBool("g_bUseTexture", TRUE);
//                g_pEffect1->SetTexture("g_texBase", g_pTexSky[i]);
//            }
//        }
//        else
//        {
//            g_pEffect1->SetBool("g_bUseTexture", FALSE);
//            g_pEffect1->SetTexture("g_texBase", NULL);
//        }
//        g_pEffect1->CommitChanges();
//        g_pMeshSky->DrawSubset(0);
//    }

    g_pEffect1->EndPass();
    g_pEffect1->End();
    g_pd3dDevice->EndScene();

    g_pd3dDevice->SetRenderTarget(2, NULL);
    g_pd3dDevice->SetRenderTarget(1, NULL);
    g_pd3dDevice->SetRenderTarget(0, pOldRT0);

    SAFE_RELEASE(pRT0);
    SAFE_RELEASE(pRT1);
    SAFE_RELEASE(pRT2);
    SAFE_RELEASE(pOldRT0);

    g_mView = mView;
    g_mProj = mProj;
}

void RenderPass2()
{
    // 共通
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    D3DXVECTOR2 invSize(1.0f / kBackW, 1.0f / kBackH);
    g_pEffect2->SetFloatArray("g_invSize", (FLOAT*)&invSize, 2);
    UINT n = 0;

    //---------------------------------------------------------------
    // Pass A: AO作成 → g_pAoTex
    //---------------------------------------------------------------
    {
        g_pEffect2->SetTechnique("TechniqueAO_Create");
        g_pEffect2->SetMatrix("g_matView", &g_mView);
        g_pEffect2->SetMatrix("g_matProj", &g_mProj);
        g_pEffect2->SetFloat("g_fNear", 1.0f);
        g_pEffect2->SetFloat("g_fFar", 1000.0f);
        g_pEffect2->SetFloat("g_posRange", g_posRange);
        g_pEffect2->SetTexture("texZ", g_pRenderTargetZ);
        g_pEffect2->SetTexture("texPos", g_pRenderTargetPos);

        g_pEffect2->SetFloat("g_aoStrength",    1.6f);
        g_pEffect2->SetFloat("g_aoStepWorld",   2.0f);

        g_pEffect2->SetFloat("g_edgeZ",         0.006f);

        // これ以上なら輪郭とみなす（小さすぎは通常面）
        g_pEffect2->SetFloat("g_farAdoptMinZ",  0.00001f);

        // これ以上は大きすぎ（別オブジェクト/空）→遠側採用しない
        g_pEffect2->SetFloat("g_farAdoptMaxZ",  0.01f);

        LPDIRECT3DSURFACE9 pAo = NULL;
        g_pAoTex->GetSurfaceLevel(0, &pAo);
        g_pd3dDevice->SetRenderTarget(0, pAo);

        g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
        g_pd3dDevice->BeginScene();
        g_pEffect2->Begin(&n, 0);
        g_pEffect2->BeginPass(0);
        DrawFullscreenQuad();
        g_pEffect2->EndPass();
        g_pEffect2->End();
        g_pd3dDevice->EndScene();
        SAFE_RELEASE(pAo);
    }

    if (g_bUseBlur)
    {
        g_pEffect2->SetFloat("g_depthReject", 0.0001f);

        //---------------------------------------------------------------
        // Pass B: 横ブラー → g_pAoTemp
        //---------------------------------------------------------------
        {
            g_pEffect2->SetTechnique("TechniqueAO_BlurH");
            g_pEffect2->SetTexture("texAO", g_pAoTex);

            LPDIRECT3DSURFACE9 pTemp = NULL;
            g_pAoTempBlur->GetSurfaceLevel(0, &pTemp);
            g_pd3dDevice->SetRenderTarget(0, pTemp);

            g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
            g_pd3dDevice->BeginScene();
            g_pEffect2->Begin(&n, 0);
            g_pEffect2->BeginPass(0);
            DrawFullscreenQuad();
            g_pEffect2->EndPass();
            g_pEffect2->End();
            g_pd3dDevice->EndScene();

            SAFE_RELEASE(pTemp);
        }

        //---------------------------------------------------------------
        // Pass C: 縦ブラー → g_pAoTex（←出力先をBackBufferから変更）
        //---------------------------------------------------------------
        {
            g_pEffect2->SetTechnique("TechniqueAO_BlurV");
            g_pEffect2->SetTexture("texAO", g_pAoTempBlur);

            LPDIRECT3DSURFACE9 pAo2 = NULL;
            g_pAoTex->GetSurfaceLevel(0, &pAo2);
            g_pd3dDevice->SetRenderTarget(0, pAo2);

            g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
            g_pd3dDevice->BeginScene();
            g_pEffect2->Begin(&n, 0);
            g_pEffect2->BeginPass(0);
            DrawFullscreenQuad();
            g_pEffect2->EndPass();
            g_pEffect2->End();
            g_pd3dDevice->EndScene();

            SAFE_RELEASE(pAo2);
        }
    }

    //---------------------------------------------------------------
    // Pass D: 合成（Color × AO） → BackBuffer
    //---------------------------------------------------------------
    {
        g_pEffect2->SetTechnique("TechniqueAO_Composite");
        g_pEffect2->SetTexture("texColor", g_pRenderTarget);
        g_pEffect2->SetTexture("texAO", g_pAoTex);
        g_pEffect2->SetFloatArray("g_invSize", (FLOAT*)&invSize, 2);

        LPDIRECT3DSURFACE9 pBack = NULL;
        g_pd3dDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBack);
        g_pd3dDevice->SetRenderTarget(0, pBack);

        g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
        g_pd3dDevice->BeginScene();
        g_pEffect2->Begin(&n, 0);
        g_pEffect2->BeginPass(0);
        DrawFullscreenQuad();
        g_pEffect2->EndPass();
        g_pEffect2->End();
        g_pd3dDevice->EndScene();

        SAFE_RELEASE(pBack);
    }

    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

// 怪しい処理
void DrawFullscreenQuad()
{
    QuadVertex v[4] = {};
    const float du = 0.5f / (float)kBackW;
    const float dv = 0.5f / (float)kBackH;

    v[0] = { -1, -1, 0, 1, 0 + du, 1 - dv };
    v[1] = { -1,  1, 0, 1, 0 + du, 0 + dv };
    v[2] = {  1, -1, 0, 1, 1 - du, 1 - dv };
    v[3] = {  1,  1, 0, 1, 1 - du, 0 + dv };

    g_pd3dDevice->SetVertexDeclaration(g_pQuadDecl);

    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,
                                  2,
                                  v,
                                  sizeof(QuadVertex));
}

LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        g_bClose = true;
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
