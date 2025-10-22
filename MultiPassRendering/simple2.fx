float4x4 g_matWorld;          // ���[���h�s��i�V�K�j
float4x4 g_matWorldViewProj;
float4 g_lightNormal = { 0.3f, 1.0f, 0.5f, 0.0f };
float4x4 g_matLightView;
float    g_lightNear = 1.0f;
float    g_lightFar  = 30.0f;   // �܂��̓^�C�g��

// �� �ǉ��FPCF�p�̃e�N�Z���T�C�Y�ƃo�C�A�X�iC++���Őݒ�j
float g_shadowTexelW;   // = 1.0 / shadowMapWidth
float g_shadowTexelH;   // = 1.0 / shadowMapHeight
float g_shadowBias;     // ��: 0.002�`0.005 �Œ���

// �� �ǉ��F���C�g�� ViewProj�i���C�g��B��������Ƃ��̍s�񂻂̂��́j
float4x4 g_matLightViewProj;

// �� �ǉ��F�V���h�E�i= �e�N�X�`��B�j���󂯎��
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
    // �K�v�Ȃ�x�[�X�J���[��ǂށi�C�Ӂj
    float4 baseColor = tex2D(textureSampler, inTexCoord0);

    // ���C�g View ��� z �� 0..1 �ɐ��K���i�����E�����ǂ���ł��g������`���j
    float4 positionLV   = mul(float4(inWorldPos, 1.0f), g_matLightView);
    float  depthLinear  = (positionLV.z - g_lightNear) / (g_lightFar - g_lightNear);
    float  depth01      = saturate(depthLinear);

    // �Ƃ肠�����[�x�������i�K�v�Ȃ� baseColor �֍����ɕύX�j
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

    // ��ʈʒu�i���C�g�� WVP �͊����� g_matWorldViewProj �𗘗p�j
    float4 clipPos = mul(vin.positionOS, g_matWorldViewProj);
    vout.positionCS = clipPos;

    // ���`�[�x�i���C�g View ��� z �� near..far �Ő��K���j
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

// ������`�͂��̂܂܁c

// �� �ǉ��F2���ڂ̃e�N�X�`��
texture texture2;
sampler textureSampler2 = sampler_state
{
    Texture   = (texture2);
    MipFilter = LINEAR;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
};

// �� 2������`����
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

// �ǉ��p�����[�^�i�����X�P�[���j
float g_worldVisScale = 0.02f;

float4 PixelShaderWorldPos(
    in float4 posCS     : POSITION0,
    in float2 uv        : TEXCOORD0,
    in float3 worldPos  : TEXCOORD1) : COLOR0
{
    // 1) ���C�gView��� z �� 0..1 �ɐ��K�� �� depthViewSpace
    float4 posLV = mul(float4(worldPos, 1.0f), g_matLightView);
    float  depthViewSpace = (posLV.z - g_lightNear) / (g_lightFar - g_lightNear);
    depthViewSpace = saturate(depthViewSpace);

    // 2) ���C�g�̃N���b�v��NDC��UV�iY���]�j�B����/�����ǂ���ł�OK
    float4 clipL = mul(float4(worldPos, 1.0f), g_matLightViewProj);
    // ���C�g���猩�Ĕw�ʂ��O�iw<=0�j�́u�e�Ȃ��v����
    if (clipL.w <= 0) {
        return float4(0,0,0,0);
    }
    float2 ndc   = clipL.xy / clipL.w;                // [-1,1]
    float2 uvL   = ndc * float2(0.5f, -0.5f) + 0.5f;  // [0,1]

    // DX9�̔��e�N�Z���␳�i�K�v�Ȃ�j�F�����_�e�N�X�`�����S�ɍ��킹��
    uvL += float2(0.5f * g_shadowTexelW, 0.5f * g_shadowTexelH);

    // �x�[�XUV���g�O�Ȃ�u�e�Ȃ��v
    if (any(uvL < 0.0f) || any(uvL > 1.0f)) {
        return float4(0,0,0,0);
    }

    float shadow = 0.0f;

    if (true)
    {
        // 3) 5x5 PCF�F���d�ݕ��ρB�O��UV�T���v���́u�e�Ȃ� = 0�v����
        float shadowSum = 0.0f;

        // 1�e�N�Z���̃I�t�Z�b�g
        float2 duv = float2(g_shadowTexelW, g_shadowTexelH);

        // ���S�}2��5x5
        [unroll]
        for (int j = -2; j <= 2; ++j)
        {
            [unroll]
            for (int i = -2; i <= 2; ++i)
            {
                float2 uvS = uvL + float2(i, j) * duv;

                // �O��UV�́u�e�Ȃ��v= 0 �Ƃ��Đ����Ȃ��i= �T���v���l 0 �����j
                if (any(uvS < 0.0f) || any(uvS > 1.0f)) {
                    // �������Ȃ��i0���Z�j
                } else {
                    float depthLightSpace = tex2D(shadowSampler, uvS).r;
                    // ��r�i���C�g������������Ήe�j
                    shadowSum += (depthLightSpace < (depthViewSpace - g_shadowBias)) ? 1.0f : 0.0f;
                }
            }
        }

        // 25�T���v���̕��ρi0..1�j
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

    // �w��F�e�� RGBA(0,0,0,0.5)�APCF���ςȂ̂� 0.5 * shadow
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


