float4x4 g_matWorldViewProj;
float4 g_lightNormal = { 0.3f, 1.0f, 0.5f, 0.0f };
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

void PixelShader1(in float4 inPosition    : POSITION,
                  in float2 inTexCood     : TEXCOORD0,
                  out float4 outColor     : COLOR)
{
    float4 workColor = (float4)0;
    workColor = tex2D(textureSampler, inTexCood);

    float average = workColor.r * 0.2 + workColor.g * 0.7 + workColor.b * 0.1;

    if (true)
    {
        workColor.r += (workColor.r - average);
        workColor.g += (workColor.g - average);
        workColor.b += (workColor.b - average);
    }
    else
    {
        workColor.r -= (workColor.r - average) / 2.f;
        workColor.g -= (workColor.g - average) / 2.f;
        workColor.b -= (workColor.b - average) / 2.f;
    }

    workColor = saturate(workColor);
    outColor = workColor;
}

technique Technique1
{
    pass Pass1
    {
        CullMode = NONE;
        VertexShader = compile vs_3_0 VertexShader1();
        PixelShader  = compile ps_3_0 PixelShader1();
    }
}

/* === ここから追加：光源から見た深度を描くテクニック === */
// 追加の定数
float4x4 g_matLightView;
float    g_lightNear = 1.0f;
float    g_lightFar  = 30.0f;   // まずはタイトに

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

// ▼ 追加：合成比
float g_mix = 0.5f;   // 0 = texture1, 1 = texture2

// ▼ そのまま描く（色いじらない）ブリット
float4 BlitPS(in float4 inPosition : POSITION,
              in float2 inTexCood  : TEXCOORD0) : COLOR0
{
    return tex2D(textureSampler, inTexCood);
}

technique TechniqueBlit
{
    pass P0
    {
        CullMode = NONE;
        VertexShader = compile vs_3_0 VertexShader1(); // 既存のVSを再利用
        PixelShader  = compile ps_3_0 BlitPS();
    }
}

// ▼ 2枚を線形合成
float4 CompositePS(in float4 inPosition : POSITION,
                   in float2 inTexCood  : TEXCOORD0) : COLOR0
{
    float4 a = tex2D(textureSampler,  inTexCood);
    float4 b = tex2D(textureSampler2, inTexCood);
    return lerp(a, b, g_mix);
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

