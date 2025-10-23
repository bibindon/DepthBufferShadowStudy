

float4x4 g_matWorld;
float4x4 g_matWorldViewProj;

float4x4 g_matLightView;
float    g_lightNear;
float    g_lightFar;
float4x4 g_matLightViewProj;

float g_shadowTexelW;   // = 1.0 / shadowMapWidth
float g_shadowTexelH;   // = 1.0 / shadowMapHeight
float g_shadowBias;     // 例: 0.002～0.005 で調整

// --- CSM 追加 ---
float4x4 g_matView;    // カメラの View（距離判定用）

// 近景(0)／遠景(1) それぞれの LightView*Proj（=LVP）
float4x4 g_LVP0;
float4x4 g_LVP1;
float  g_lNear0, g_lFar0;
float  g_lNear1, g_lFar1;

texture shadow0;
texture shadow1;

sampler sShadow0 = sampler_state {
    Texture=(shadow0); MinFilter=POINT; MagFilter=POINT; MipFilter=NONE; AddressU=CLAMP; AddressV=CLAMP;
};
sampler sShadow1 = sampler_state {
    Texture=(shadow1); MinFilter=POINT; MagFilter=POINT; MipFilter=NONE; AddressU=CLAMP; AddressV=CLAMP;
};

float g_texelW0, g_texelH0;
float g_texelW1, g_texelH1;

float g_splitZ  = 30.0f; // カメラ view-space z での分割位置
float g_blendZ  = 0.0f;  // 継ぎ目フェード幅（0ならブレンド無し）

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

texture texture1;
sampler textureSampler = sampler_state {
    Texture = (texture1);
    MipFilter = NONE;
    MinFilter = POINT;
    MagFilter = POINT;
};

texture texture2;
sampler textureSampler2 = sampler_state
{
    Texture   = (texture2);
    MipFilter = LINEAR;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
};

float SampleShadow3x3(float3 worldPos,
                      float4x4 LVP,
                      sampler2D smp,
                      float2 texelWH,
                      float lNear, float lFar,
                      float bias)
{
    float4 clipL = mul(float4(worldPos,1), LVP);
    if (clipL.w <= 0) return 0.0f;

    float2 ndc = clipL.xy / clipL.w;
    float2 uv  = ndc * float2(0.5f,-0.5f) + 0.5f;
    uv += 0.5f * texelWH; // 半テクセル補正

    if (any(uv < 0.0f) || any(uv > 1.0f)) return 0.0f;

    float depthView = saturate(clipL.z / clipL.w);

    float sum = 0.0f;
    [unroll] for (int j=-1;j<=1;++j)
    {
        [unroll] for (int i=-1;i<=1;++i)
        {
            float2 uvS = uv + float2(i,j) * texelWH;
            if (any(uvS<0.0f)||any(uvS>1.0f)) continue;
            float d = tex2D(smp, uvS).r;
            sum += (d < (depthView - g_shadowBias)) ? 1.0f : 0.0f;
        }
    }
    return sum / 9.0f;
}

float SampleShadow5x5(float3 worldPos,
                      float4x4 LVP,
                      sampler2D smp,
                      float2 texelWH,
                      float lNear, float lFar,
                      float bias)
{
    float4 clipL = mul(float4(worldPos,1), LVP);
    if (clipL.w <= 0) return 0.0f;

    float2 ndc = clipL.xy / clipL.w;
    float2 uv  = ndc * float2(0.5f,-0.5f) + 0.5f;
    uv += 0.5f * texelWH; // 半テクセル補正

    if (any(uv < 0.0f) || any(uv > 1.0f)) return 0.0f;

    float depthView = saturate(clipL.z / clipL.w);

    float sum = 0.0f;
    [unroll] for (int j=-2;j<=2;++j)
    {
        [unroll] for (int i=-2;i<=2;++i)
        {
            float2 uvS = uv + float2(i,j) * texelWH;
            if (any(uvS<0.0f)||any(uvS>1.0f)) continue;
            float d = tex2D(smp, uvS).r;
            sum += (d < (depthView - g_shadowBias)) ? 1.0f : 0.0f;
        }
    }
    return sum / 25.0f;
}

//-------------------------------------------------------------------------
// Technique 1
//-------------------------------------------------------------------------

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

    float4 worldPos = mul(vin.positionOS, g_matWorld);           // ★ 追加：OS→WS
    float4 clipPos  = mul(vin.positionOS, g_matWorldViewProj);   // LWVP はそのまま使う
    float4 posLV    = mul(worldPos, g_matLightView);             // ★ WS→LightView に修正

    // 画面位置（ライトの WVP は既存の g_matWorldViewProj を利用）
    vout.positionCS = clipPos;

    // 線形深度（ライト View 空間 z を near..far で正規化）
    float  depthLinear = (posLV.z - g_lightNear) / (g_lightFar - g_lightNear);
    vout.depth01 = saturate(depthLinear);

    return vout;
}

float4 DepthFromLightPS(VSOutDepth pin) : COLOR0
{
    float d = pin.depth01;
    return float4(d, d, d, 1.0f);
}

//-------------------------------------------------------------------------
// Technique 2
//-------------------------------------------------------------------------

void VertexShaderWS(in  float4 inPositionOS  : POSITION,
                    in  float2 inTexCoord0   : TEXCOORD0,

                    out float4 outPositionCS : POSITION0,
                    out float2 outTexCoord0  : TEXCOORD0,
                    out float3 outWorldPos   : TEXCOORD1)
{
    float4 positionWS = mul(inPositionOS, g_matWorld);
    outWorldPos   = positionWS.xyz;

    outTexCoord0  = inTexCoord0;

    float4 positionCS = mul(inPositionOS, g_matWorldViewProj);
    outPositionCS = positionCS;
}

float4 PixelShaderWorldPos(float4 posCS:POSITION0, float2 uv:TEXCOORD0, float3 worldPos:TEXCOORD1) : COLOR0
{
    // 距離でカスケード選択
    float3 posVS = mul(float4(worldPos,1), g_matView).xyz;
    float  zView = posVS.z; // LHなら前方が+z

    float sh0 = SampleShadow3x3(worldPos, g_LVP0, sShadow0, float2(g_texelW0,g_texelH0), g_lNear0, g_lFar0, g_shadowBias);
    float sh1 = SampleShadow3x3(worldPos, g_LVP1, sShadow1, float2(g_texelW1,g_texelH1), g_lNear1, g_lFar1, g_shadowBias);

    float shadow;
    if (g_blendZ <= 0.0f)
    {
        shadow = (zView <= g_splitZ) ? sh0 : sh1;      // 二重適用なし
    }
    else
    {
        float z0 = g_splitZ - 0.5f * g_blendZ;
        float z1 = g_splitZ + 0.5f * g_blendZ;
        float w  = saturate((zView - z0) / max(z1 - z0, 1e-4));
        shadow = lerp(sh0, sh1, w);                    // 境界のみブレンド
    }

    return float4(0,0,0, 0.5f * shadow);
}

//-------------------------------------------------------------------------
// Technique 3
//-------------------------------------------------------------------------

void VertexShader1(in  float4 inPosition  : POSITION,
                   in  float2 inTexCood   : TEXCOORD0,

                   out float4 outPosition : POSITION,
                   out float2 outTexCood  : TEXCOORD0)
{
    outPosition = inPosition;
    outTexCood = inTexCood;
}

// 2枚の画像を線形補間で合成する
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

// 光源から見た深度を描画するテクニック
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

// 光源から見た深度画像とカメラから見たワールド座標を使って、影を描画するテクニック
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

// 二つの画像を合成するテクニック
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


