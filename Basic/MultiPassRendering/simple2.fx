

float4x4 g_matWorld;
float4x4 g_matWorldViewProj;

float4x4 g_matLightView;
float    g_lightNear;
float    g_lightFar;
float4x4 g_matLightViewProj;

float g_shadowTexelW;   // = 1.0 / shadowMapWidth
float g_shadowTexelH;   // = 1.0 / shadowMapHeight

// �e�̒[�ɕ\�������M�U�M�U��}���B0.002�`0.005 �Œ���
float g_shadowBias;

// �e�̔Z��(0 ~ 1)
float g_shadowIntensity;

// �e�̃{�P�(�)
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

// �ϐ����̖�����OS�̓��[�J�����W�̈Ӗ�

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


    // ���`�[�x�i���C�g View ��� z �� near..far �Ő��K���j
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
    if (any(uvL < 0.0f) || any(uvL > 1.0f))
    {
        return float4(0,0,0,0);
    }

    float shadow = 0.0f;

    if (true)
    {
        // 3) 5x5 PCF�F���d�ݕ��ρB�O��UV�T���v���́u�e�Ȃ� = 0�v����
        float shadowSum = 0.0f;

        // 1�e�N�Z���̃I�t�Z�b�g
        float2 duv = float2(g_shadowTexelW, g_shadowTexelH);

        // ��ł��邱��
        const int SIZE = 3;

        // ���S�}2��5x5
        [unroll]
        for (int j = -(SIZE / 2); j <= (SIZE / 2); ++j)
        {
            [unroll]
            for (int i = -(SIZE / 2); i <= (SIZE / 2); ++i)
            {
                float2 uvS = uvL + float2(i, j) * duv;

                // �O��UV�́u�e�Ȃ��v= 0 �Ƃ��Đ����Ȃ��i= �T���v���l 0 �����j
                if (any(uvS < 0.0f) || any(uvS > 1.0f))
                {
                    // �������Ȃ��i0���Z�j
                }
                else
                {
                    float depthLightSpace = tex2D(samplerLightZ, uvS).r;
                    // ��r�i���C�g������������Ήe�j
                    if (depthLightSpace < (depthViewSpace - g_shadowBias))
                    {
                        shadowSum += 1.0f;
                    }
                }
            }
        }

        // 25�T���v���̕��ρi0..1�j
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

    // �w��F�e�� RGBA(0,0,0,0.5)�APCF���ςȂ̂� 0.5 * shadow
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

// 2���̉摜����`��Ԃō�������
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

// �������猩���[�x��`�悷��e�N�j�b�N
technique TechniqueDepthFromLight
{
    pass P0
    {
        VertexShader = compile vs_3_0 VS_DepthFromLight();
        PixelShader  = compile ps_3_0 PS_DepthFromLight();
    }
}

// �������猩���[�x�摜�ƃJ�������猩�����[���h���W���g���āA�e��`�悷��e�N�j�b�N
technique TechniqueWriteShadow
{
    pass P0
    {
        VertexShader = compile vs_3_0 VS_Base();
        PixelShader  = compile ps_3_0 PS_WriteShadow();
    }
}

// ��̉摜����������e�N�j�b�N
technique TechniqueComposite
{
    pass P0
    {
        VertexShader = compile vs_3_0 VS_Composite();
        PixelShader  = compile ps_3_0 PS_Composite();
    }
}


