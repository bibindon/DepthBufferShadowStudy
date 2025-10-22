float4x4 g_matWorld;          // ワールド行列（新規）
float4x4 g_matWorldViewProj;
float4 g_lightNormal = { 0.3f, 1.0f, 0.5f, 0.0f };
float4x4 g_matLightView;
float    g_lightNear = 1.0f;
float    g_lightFar  = 30.0f;   // まずはタイトに

// ★ 追加：PCF用のテクセルサイズとバイアス（C++側で設定）
float g_shadowTexelW;   // = 1.0 / shadowMapWidth
float g_shadowTexelH;   // = 1.0 / shadowMapHeight
float g_shadowBias;     // 例: 0.002～0.005 で調整

// ★ 追加：ライトの ViewProj（ライトでBを作ったときの行列そのもの）
float4x4 g_matLightViewProj;

// ★ 追加：シャドウ（= テクスチャB）を受け取る
texture textureShadow;
sampler shadowSampler = sampler_state
{
    Texture   = (textureShadow);
    MinFilter = POINT;
    MagFilter = POINT;
    MipFilter = NONE;
    AddressU  = CLAMP;
    AddressV  = CLAMP;
};

float3 g_ambient = { 0.3f, 0.3f, 0.3f };

bool g_bUseTexture = true;

texture texture1;
sampler textureSampler = sampler_state {
    Texture = (texture1);
    MipFilter = NONE;
    MinFilter = POINT;
    MagFilter = POINT;
};

void VertexShader1(in  float4 inPosition  : POSITION,
                   in  float2 inTexCood   : TEXCOORD0,
                   out float4 outPosition : POSITION,
                   out float2 outTexCood  : TEXCOORD0)
{
    outPosition = inPosition;
    outTexCood = inTexCood;
}

void VertexShaderWS
(
    in  float4 inPositionOS  : POSITION,
    in  float2 inTexCoord0   : TEXCOORD0,
    out float4 outPositionCS : POSITION0,
    out float2 outTexCoord0  : TEXCOORD0,
    out float3 outWorldPos   : TEXCOORD1
)
{
    float4 positionWS = mul(inPositionOS, g_matWorld);
    outWorldPos   = positionWS.xyz;

    outTexCoord0  = inTexCoord0;

    float4 positionCS = mul(inPositionOS, g_matWorldViewProj);
    outPositionCS = positionCS;
}

float4 PixelShader1
(
    in float4 inPositionCS : POSITION,
    in float2 inTexCoord0  : TEXCOORD0,
    in float3 inWorldPos   : TEXCOORD1
) : COLOR0
{
    // 必要ならベースカラーを読む（任意）
    float4 baseColor = tex2D(textureSampler, inTexCoord0);

    // ライト View 空間 z を 0..1 に正規化（直交・透視どちらでも使える線形化）
    float4 positionLV   = mul(float4(inWorldPos, 1.0f), g_matLightView);
    float  depthLinear  = (positionLV.z - g_lightNear) / (g_lightFar - g_lightNear);
    float  depth01      = saturate(depthLinear);

    // とりあえず深度を可視化（必要なら baseColor へ合成に変更可）
    //return float4(depth01, depth01, depth01, 1.0f);
    return baseColor;
}

technique Technique1
{
    pass Pass1
    {
        CullMode = NONE;
        VertexShader = compile vs_3_0 VertexShaderWS();
        PixelShader  = compile ps_3_0 PixelShader1();
    }
}

struct VSInDepth
{
    float4 positionOS : POSITION0;
};

struct VSOutDepth
{
    float4 positionCS : POSITION0;
    float  depth01    : TEXCOORD0;
};

VSOutDepth DepthFromLightVS(VSInDepth vin)
{
    VSOutDepth vout;

    // 画面位置（ライトの WVP は既存の g_matWorldViewProj を利用）
    float4 clipPos = mul(vin.positionOS, g_matWorldViewProj);
    vout.positionCS = clipPos;

    // 線形深度（ライト View 空間 z を near..far で正規化）
    float4 posLV = mul(vin.positionOS, g_matLightView);
    float  depthLinear = (posLV.z - g_lightNear) / (g_lightFar - g_lightNear);
    vout.depth01 = saturate(depthLinear);

    return vout;
}

float4 DepthFromLightPS(VSOutDepth pin) : COLOR0
{
    float d = pin.depth01;
    return float4(d, d, d, 1.0f);
}

technique TechniqueDepthFromLight
{
    pass P0
    {
        CullMode = NONE;
        ZEnable = TRUE;
        ZWriteEnable = TRUE;

        VertexShader = compile vs_3_0 DepthFromLightVS();
        PixelShader  = compile ps_3_0 DepthFromLightPS();
    }
}

// 既存定義はそのまま…

// ▼ 追加：2枚目のテクスチャ
texture texture2;
sampler textureSampler2 = sampler_state
{
    Texture   = (texture2);
    MipFilter = LINEAR;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
};

// ▼ 2枚を線形合成
float4 CompositePS(in float4 inPosition : POSITION,
                   in float2 inTexCood  : TEXCOORD0) : COLOR0
{
    float4 a = tex2D(textureSampler,  inTexCood);
    float4 b = tex2D(textureSampler2, inTexCood);

    float4 result = float4(0, 0, 0, 0);

    result.rgb = a.rgb * b.a;
    result = lerp(a, b, b.a);

    result.a = 1.f;

    return result;
}

technique TechniqueComposite
{
    pass P0
    {
        CullMode = NONE;
        AlphaBlendEnable = FALSE;

        VertexShader = compile vs_3_0 VertexShader1();
        PixelShader  = compile ps_3_0 CompositePS();
    }
}

// 追加パラメータ（可視化スケール）
float g_worldVisScale = 0.02f;

float4 PixelShaderWorldPos(
    in float4 posCS     : POSITION0,
    in float2 uv        : TEXCOORD0,
    in float3 worldPos  : TEXCOORD1) : COLOR0
{
    // 1) ライトView空間 z を 0..1 に正規化 → depthViewSpace
    float4 posLV = mul(float4(worldPos, 1.0f), g_matLightView);
    float  depthViewSpace = (posLV.z - g_lightNear) / (g_lightFar - g_lightNear);
    depthViewSpace = saturate(depthViewSpace);

    // 2) ライトのクリップ→NDC→UV（Y反転）。直交/透視どちらでもOK
    float4 clipL = mul(float4(worldPos, 1.0f), g_matLightViewProj);
    // ライトから見て背面や手前（w<=0）は「影なし」扱い
    if (clipL.w <= 0) {
        return float4(0,0,0,0);
    }
    float2 ndc   = clipL.xy / clipL.w;                // [-1,1]
    float2 uvL   = ndc * float2(0.5f, -0.5f) + 0.5f;  // [0,1]

    // DX9の半テクセル補正（必要なら）：レンダテクスチャ中心に合わせる
    uvL += float2(0.5f * g_shadowTexelW, 0.5f * g_shadowTexelH);

    // ベースUVが枠外なら「影なし」
    if (any(uvL < 0.0f) || any(uvL > 1.0f)) {
        return float4(0,0,0,0);
    }

    float shadow = 0.0f;

    if (true)
    {
        // 3) 5x5 PCF：等重み平均。外れUVサンプルは「影なし = 0」扱い
        float shadowSum = 0.0f;

        // 1テクセルのオフセット
        float2 duv = float2(g_shadowTexelW, g_shadowTexelH);

        // 中心±2の5x5
        [unroll]
        for (int j = -2; j <= 2; ++j)
        {
            [unroll]
            for (int i = -2; i <= 2; ++i)
            {
                float2 uvS = uvL + float2(i, j) * duv;

                // 外れUVは「影なし」= 0 として数えない（= サンプル値 0 扱い）
                if (any(uvS < 0.0f) || any(uvS > 1.0f)) {
                    // 何もしない（0加算）
                } else {
                    float depthLightSpace = tex2D(shadowSampler, uvS).r;
                    // 比較（ライト側が小さければ影）
                    shadowSum += (depthLightSpace < (depthViewSpace - g_shadowBias)) ? 1.0f : 0.0f;
                }
            }
        }

        // 25サンプルの平均（0..1）
        shadow = shadowSum / 25.0f;
    }
    else
    {
        float depthLightSpace = tex2D(shadowSampler, uvL).r;
        if (depthLightSpace < (depthViewSpace - g_shadowBias))
        {
            shadow = 1.0f;
        }
        else
        {
            shadow = 0.0f;
        }
    }

    // 指定：影は RGBA(0,0,0,0.5)、PCF平均なので 0.5 * shadow
    return float4(0.0f, 0.0f, 0.0f, 0.5f * shadow);
}

technique TechniqueWorldPos
{
    pass P0
    {
        CullMode    = NONE;
        ZEnable     = TRUE;
        ZWriteEnable= TRUE;
        VertexShader = compile vs_3_0 VertexShaderWS();
        PixelShader  = compile ps_3_0 PixelShaderWorldPos();
    }
}


