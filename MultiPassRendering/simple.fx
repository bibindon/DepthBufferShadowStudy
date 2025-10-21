
float4x4 g_matWorld;
float4x4 g_matView;
float4x4 g_matWorldViewProj;

float g_fNear;
float g_fFar;

float g_posRange;

bool g_bUseTexture = false;

texture g_texBase;
sampler sampBase = sampler_state
{
    Texture = (g_texBase);
    MinFilter = POINT;
    MagFilter = POINT;
    MipFilter = POINT;
    AddressU = WRAP;
    AddressV = WRAP;
};

void VertexShader1(in float4 inPosition   : POSITION,
                   in float4 inNormal     : NORMAL0,
                   in float4 inTexCood    : TEXCOORD0,

                   out float4 outPosition : POSITION,
                   out float4 outDiffuse  : COLOR0,
                   out float4 outTexCood  : TEXCOORD0,
                   out float outViewZ     : TEXCOORD1,
                   out float3 outWorldPos : TEXCOORD2)
{
    float4 worldPos = mul(inPosition, g_matWorld);
    outPosition = mul(worldPos, g_matWorldViewProj);

    // 簡単なライティング
    float lightIntensity = 0.f;

    // 平行光源によるライティングありorなし
    if (true)
    {
        lightIntensity = dot(inNormal, normalize(float4(-0.3, 1.0, -0.5, 0)));
    }
    else
    {
        lightIntensity = 1.0f;
    }

    outDiffuse.rgb = max(0, lightIntensity) + 0.3;
    outDiffuse.a = 1.0f;

    outTexCood = inTexCood;
    
    float4 vpos = mul(worldPos, g_matView);
    outViewZ = vpos.z;
    outWorldPos = worldPos.xyz;
}

void PixelShaderMRT3(in float4 inScreenColor : COLOR0,
                     in float2 inTexCood     : TEXCOORD0,
                     in float  inViewZ       : TEXCOORD1,
                     in float3 inWorldPos    : TEXCOORD2,

                     out float4 outColor     : COLOR0, // Color
                     out float4 outZ         : COLOR1, // Z画像
                     out float4 outPosWS     : COLOR2  // POS画像
)
{
    float3 lit = inScreenColor.rgb;
    float3 base = lit;

    if (g_bUseTexture)
    {
        float3 tex = tex2D(sampBase, inTexCood).rgb;
        base = tex * lit;
    }

    outColor = float4(base, 1.0f);

    float linearZ = saturate((inViewZ - g_fNear) / (g_fFar - g_fNear));
    outZ = float4(linearZ, linearZ, linearZ, linearZ);

    float3 normalizedPosWS = inWorldPos / g_posRange;
    float3 enc = saturate(normalizedPosWS * 0.5 + 0.5);
    outPosWS = float4(enc, 1.0f);
}

// MRT...Multi Render Target
technique TechniqueMRT
{
    pass P0
    {
        CullMode = NONE;
        VertexShader = compile vs_3_0 VertexShader1();
        PixelShader = compile ps_3_0 PixelShaderMRT3();
    }
}

