


float4x4 g_matWorld;
float4x4 g_matWorldViewProj;

float4x4 g_matLightView;
float    g_lightNear;
float    g_lightFar;
float4x4 g_matLightViewProj;

float g_shadowTexelW;
float g_shadowTexelH;

// 影の端に表示されるギザギザを抑制。0.002～0.005 で調整
float g_shadowBias;

// 影の濃さ(0 ~ 1)
float g_shadowIntensity = 0.5f;

bool g_bBlurEnable = true;

// 影のボケ具合(奇数)
int g_nBlurSize;

texture g_texLightZ;
sampler samplerLightZ = sampler_state
{
    Texture   = (g_texLightZ);
    MinFilter = POINT;
    MagFilter = POINT;
    MipFilter = NONE;
    AddressU  = CLAMP;
    AddressV  = CLAMP;
};

texture g_texBase;
sampler samplerBase = sampler_state {
    Texture = (g_texBase);
    MipFilter = NONE;
    MinFilter = POINT;
    MagFilter = POINT;
};

texture g_texShadow;
sampler samplerShadow = sampler_state
{
    Texture   = (g_texShadow);
    MipFilter = LINEAR;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
};

// 変数名の末尾のOSはローカル座標の意味
// 変数名の末尾のWSはグローバル座標の意味

//-------------------------------------------------------------------------
// Technique 1
//-------------------------------------------------------------------------

struct VSInDepth
{
    float4 vPosOS  : POSITION0;
};

struct VSOutDepth
{
    float4 vPos    : POSITION0;
    float  fDepth  : TEXCOORD0;
};

VSOutDepth VS_DepthFromLight(VSInDepth vin)
{
    VSOutDepth vout;

    float4 clipPos  = mul(vin.vPosOS, g_matWorldViewProj);
    vout.vPos = clipPos;

    float4 inWorldPos = mul(vin.vPosOS, g_matWorld);
    float4 vPosLightView    = mul(inWorldPos, g_matLightView);


    // 線形深度（ライト View 空間 z を near..far で正規化）
    float  depthLinear = (vPosLightView.z - g_lightNear) / (g_lightFar - g_lightNear);
    vout.fDepth = saturate(depthLinear);

    return vout;
}

float4 PS_DepthFromLight(VSOutDepth pin) : COLOR0
{
    float d = pin.fDepth;
    return float4(d, d, d, 1.0f);
}

//-------------------------------------------------------------------------
// Technique 2
//-------------------------------------------------------------------------

void VS_Base(in  float4 inPosOS     : POSITION,
             in  float2 inUV        : TEXCOORD0,

             out float4 outPos      : POSITION0,
             out float2 outUV       : TEXCOORD0,
             out float3 outWorldPos : TEXCOORD1)
{
    float4 vPos = mul(inPosOS, g_matWorldViewProj);
    outPos = vPos;
    outUV = inUV;

    float4 posWS = mul(inPosOS, g_matWorld);
    outWorldPos = posWS.xyz;
}

void PS_WriteShadow(in float4 inPos       : POSITION0,
                    in float2 inUV        : TEXCOORD0,
                    in float3 inWorldPos  : TEXCOORD1,

                    out float4 outColor   : COLOR0)
{
    outColor = float4(0, 0, 0, 0);
    
    //---------------------------------------------------------
    // カメラから見た各ピクセルのワールド座標の位置を
    // もし、光源の位置から見たら、深度はいくら？、を求める
    //---------------------------------------------------------
    float4 vPosLightView = mul(float4(inWorldPos, 1.0f), g_matLightView);

    float  fDepthLightView = (vPosLightView.z - g_lightNear) / (g_lightFar - g_lightNear);
    fDepthLightView = saturate(fDepthLightView);

    //---------------------------------------------------------
    // カメラから見た各ピクセルのワールド座標の位置を
    // もし、光源の位置から見たら、UV座標は何？、を求める
    //---------------------------------------------------------
    float4 vClipLightView = mul(float4(inWorldPos, 1.0f), g_matLightViewProj);

    // ライトから見て背面（w <= 0）は「影なし」扱い
    if (vClipLightView.w <= 0)
    {
        outColor.a = 0.0f;
        return;
    }

    // 2D平面の-1 ~ +1の範囲に正規化させた座標を取得する
    float2 uvNormalizedView   = vClipLightView.xy / vClipLightView.w;                // [-1,1]

    // -1 ~ +1 なのでUV画像に合わせるために 0 ~ 1 に調節する
    float2 uvLightView   = uvNormalizedView * float2(0.5f, -0.5f) + 0.5f;  // [0,1]

    // DX9の半テクセル補正
    uvLightView += float2(0.5f * g_shadowTexelW, 0.5f * g_shadowTexelH);

    // ベースUVが枠外なら「影なし」
    if (any(uvLightView < 0.0f) || any(uvLightView > 1.0f))
    {
        outColor.a = 0.0f;
        return;
    }

    float shadow = 0.0f;

    if (g_bBlurEnable)
    {
        // サンプリングされた個数
        float fShadowSum = 0.0f;

        // 1テクセルのオフセット
        float2 uvTexel = float2(g_shadowTexelW, g_shadowTexelH);

        int nHalfSize = g_nBlurSize / 2;

        // 奇数であること
        const int SIZE_MAX = 13;

        // ボカシのレベルを調節する
        // HLSLではfor文の開始・終了条件に定数しか使えないのでちょっとした小細工が必要
        for (int j = -(SIZE_MAX / 2); j <= (SIZE_MAX / 2); ++j)
        {
            int j2 = abs(j);

            if (j2 > nHalfSize)
            {
                continue;
            }

            for (int i = -(SIZE_MAX / 2); i <= (SIZE_MAX / 2); ++i)
            {
                int i2 = abs(i);

                if (i2 > nHalfSize)
                {
                    continue;
                }

                float2 uvS = uvLightView + float2(i, j) * uvTexel;

                // 外れUVは「影なし」= 0 として数えない（= サンプル値 0 扱い）
                if (any(uvS < 0.0f) || any(uvS > 1.0f))
                {
                    // 何もしない（0加算）
                }
                else
                {
                    // tex2Dではなくtex2Dlodを使わなくてはいけない。そうしないと動かない
                    float depthLightSpace = tex2Dlod(samplerLightZ, float4(uvS, 0, 0)).r;

                    // 比較（ライト側が小さければ影）
                    if (depthLightSpace < (fDepthLightView - g_shadowBias))
                    {
                        fShadowSum += 1.0f;
                    }
                }
            }
        }

        // 25サンプルの平均（0..1）
        shadow = fShadowSum / pow(g_nBlurSize, 2);
    }
    else
    {
        float depthLightSpace = tex2D(samplerLightZ, uvLightView).r;
        if (depthLightSpace < (fDepthLightView - g_shadowBias))
        {
            shadow = 1.0f;
        }
        else
        {
            shadow = 0.0f;
        }
    }

    outColor.a = shadow * g_shadowIntensity;
}

//-------------------------------------------------------------------------
// Technique 3
//-------------------------------------------------------------------------

void VS_Composite(in  float4 inPos  : POSITION,
                  in  float2 inUV   : TEXCOORD0,

                  out float4 outPos : POSITION,
                  out float2 outUV  : TEXCOORD0)
{
    outPos = inPos;
    outUV = inUV;
}

// 2枚の画像を線形補間で合成する
void PS_Composite(in float4 inPos     : POSITION,
                  in float2 inUV      : TEXCOORD0,

                  out float4 outColor : COLOR0)
{
    float4 vBaseColor = tex2D(samplerBase,  inUV);
    float4 vShadowColor = tex2D(samplerShadow, inUV);

    float4 result = float4(0, 0, 0, 0);

    result.rgb = vBaseColor.rgb * vShadowColor.a;
    result = lerp(vBaseColor, vShadowColor, vShadowColor.a);

    result.a = 1.f;

    outColor = result;
}

// 光源から見た深度を描画するテクニック
technique TechniqueDepthFromLight
{
    pass P0
    {
        VertexShader = compile vs_3_0 VS_DepthFromLight();
        PixelShader  = compile ps_3_0 PS_DepthFromLight();
    }
}

// 光源から見た深度画像とカメラから見たワールド座標を使って、影を描画するテクニック
technique TechniqueWriteShadow
{
    pass P0
    {
        VertexShader = compile vs_3_0 VS_Base();
        PixelShader  = compile ps_3_0 PS_WriteShadow();
    }
}

// 二つの画像を合成するテクニック
technique TechniqueComposite
{
    pass P0
    {
        VertexShader = compile vs_3_0 VS_Composite();
        PixelShader  = compile ps_3_0 PS_Composite();
    }
}


