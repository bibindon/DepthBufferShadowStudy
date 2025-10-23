

float4x4 g_matWorld;
float4x4 g_matWorldViewProj;

float4x4 g_matLightView;
float    g_lightNear;
float    g_lightFar;
float4x4 g_matLightViewProj;

float g_shadowTexelW;   // = 1.0 / shadowMapWidth
float g_shadowTexelH;   // = 1.0 / shadowMapHeight

// 影の端に表示されるギザギザを抑制。0.002～0.005 で調整
float g_shadowBias;

// 影の濃さ(0 ~ 1)
float g_shadowIntensity;

// 影のボケ具合(奇数)
float g_shadowBlur;

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

    float4 worldPos = mul(vin.vPosOS, g_matWorld);
    float4 posLV    = mul(worldPos, g_matLightView);


    // 線形深度（ライト View 空間 z を near..far で正規化）
    float  depthLinear = (posLV.z - g_lightNear) / (g_lightFar - g_lightNear);
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

void VS_Base(in  float4 inPositionOS  : POSITION,
                    in  float2 inTexCoord0   : TEXCOORD0,

                    out float4 outPositionCS : POSITION0,
                    out float2 outTexCoord0  : TEXCOORD0,
                    out float3 outWorldPos   : TEXCOORD1)
{
    float4 positionWS = mul(inPositionOS, g_matWorld);
    outWorldPos   = positionWS.xyz;

    outTexCoord0  = inTexCoord0;

    float4 vPos = mul(inPositionOS, g_matWorldViewProj);
    outPositionCS = vPos;
}

float4 PS_WriteShadow(in float4 posCS     : POSITION0,
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
    if (any(uvL < 0.0f) || any(uvL > 1.0f))
    {
        return float4(0,0,0,0);
    }

    float shadow = 0.0f;

    if (true)
    {
        // 3) 5x5 PCF：等重み平均。外れUVサンプルは「影なし = 0」扱い
        float shadowSum = 0.0f;

        // 1テクセルのオフセット
        float2 duv = float2(g_shadowTexelW, g_shadowTexelH);

        // 奇数であること
        const int SIZE = 3;

        // 中心±2の5x5
        [unroll]
        for (int j = -(SIZE / 2); j <= (SIZE / 2); ++j)
        {
            [unroll]
            for (int i = -(SIZE / 2); i <= (SIZE / 2); ++i)
            {
                float2 uvS = uvL + float2(i, j) * duv;

                // 外れUVは「影なし」= 0 として数えない（= サンプル値 0 扱い）
                if (any(uvS < 0.0f) || any(uvS > 1.0f))
                {
                    // 何もしない（0加算）
                }
                else
                {
                    float depthLightSpace = tex2D(samplerLightZ, uvS).r;
                    // 比較（ライト側が小さければ影）
                    if (depthLightSpace < (depthViewSpace - g_shadowBias))
                    {
                        shadowSum += 1.0f;
                    }
                }
            }
        }

        // 25サンプルの平均（0..1）
        shadow = shadowSum / pow(SIZE, 2);
    }
    else
    {
        float depthLightSpace = tex2D(samplerLightZ, uvL).r;
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

//-------------------------------------------------------------------------
// Technique 3
//-------------------------------------------------------------------------

void VS_Composite(in  float4 inPosition  : POSITION,
                   in  float2 inTexCood   : TEXCOORD0,

                   out float4 outPosition : POSITION,
                   out float2 outTexCood  : TEXCOORD0)
{
    outPosition = inPosition;
    outTexCood = inTexCood;
}

// 2枚の画像を線形補間で合成する
float4 PS_Composite(in float4 inPosition : POSITION,
                   in float2 inTexCood  : TEXCOORD0) : COLOR0
{
    float4 vBaseColor = tex2D(samplerBase,  inTexCood);
    float4 vShadowColor = tex2D(samplerShadow, inTexCood);

    float4 result = float4(0, 0, 0, 0);

    result.rgb = vBaseColor.rgb * vShadowColor.a;
    result = lerp(vBaseColor, vShadowColor, vShadowColor.a);

    result.a = 1.f;

    return result;
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


