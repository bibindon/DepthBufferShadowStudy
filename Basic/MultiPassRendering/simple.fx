
// 最低限の描画を行うエフェクト
// マルチレンダ―ターゲットで深度画像を描画しているが、今回は使われない。

float4x4 g_matWorldViewProj;
float4 g_lightDir = { 0.4f, 0.5f, -0.4f, 0.0f };
float3 g_ambient = { 0.3f, 0.3f, 0.3f };

bool g_bUseTexture = true;

texture g_textureBase;
sampler textureSampler = sampler_state
{
    Texture = (g_textureBase);
    MipFilter = LINEAR;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
};

void VertexShader1(in float4 inPosition    : POSITION,
                   in float3 inNormal      : NORMAL,
                   in float2 inTexCoord0   : TEXCOORD0,

                   out float4 outPosition  : POSITION0,
                   out float2 outTexCoord0 : TEXCOORD0,
                   out float4 outDiffuse  : COLOR0,
                   out float outDepth01    : TEXCOORD1)
{
    float4 clipPosition = mul(inPosition, g_matWorldViewProj);
    outPosition = clipPosition;
    outTexCoord0 = inTexCoord0;

    // 簡単なライティング
    float lightIntensity = 0.f;

    // 平行光源によるライティングありorなし
    // 深度バッファシャドウを表示したら、
    // ランバート拡散照明モデルの影は余計なので消したほうがいいかもしれない。
    // もしくはハーフランバートにするか。
    // ハーフランバートは悪くない見た目のように感じる。
    if (false)
    {
        lightIntensity = dot(inNormal, normalize(g_lightDir.xyz));

        // ハーフランバート
        if (true)
        {
            lightIntensity = (lightIntensity + 1.0) * 0.5;
        }
    }
    else
    {
        lightIntensity = 1.0f;
    }
    
    outDiffuse.rgb = max(0, lightIntensity) + 0.3;
    outDiffuse.a = 1.0f;
    outDiffuse = saturate(outDiffuse);

    // 0..1（近=0, 遠=1）
    float depthNdc = clipPosition.z / clipPosition.w;
    outDepth01 = saturate(depthNdc);
}

// ピクセルシェーダー
// COLOR1にグレースケールで深度を書き込む
void PixelShaderMRT(in float2 inTexCoord0 : TEXCOORD0,
                    in float inDepth01    : TEXCOORD1,
                    in float4 inDiffuse : COLOR0,

                    out float4 outColor0  : COLOR0,
                    out float4 outColor1  : COLOR1)
{
    float4 baseColor = float4(0.5, 0.5, 0.5, 1.0);

    if (g_bUseTexture)
    {
        baseColor = tex2D(textureSampler, inTexCoord0);
    }

    outColor0 = baseColor * inDiffuse;

    // 近いほど黒、遠いほど白
    float d = inDepth01;
    outColor1 = float4(d, d, d, 1.0);
}

technique TechniqueMRT
{
    pass P0
    {
        VertexShader = compile vs_3_0 VertexShader1();
        PixelShader = compile ps_3_0 PixelShaderMRT();
    }
}

