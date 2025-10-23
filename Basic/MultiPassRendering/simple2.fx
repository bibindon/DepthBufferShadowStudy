


float4x4 g_matWorld;
float4x4 g_matWorldViewProj;

float4x4 g_matLightView;
float    g_lightNear;
float    g_lightFar;
float4x4 g_matLightViewProj;

float g_shadowTexelW;
float g_shadowTexelH;

// �e�̒[�ɕ\�������M�U�M�U��}���B0.002�`0.005 �Œ���
float g_shadowBias;

// �e�̔Z��(0 ~ 1)
float g_shadowIntensity = 0.5f;

bool g_bBlurEnable = true;

// �e�̃{�P�(�)
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

// �ϐ����̖�����OS�̓��[�J�����W�̈Ӗ�
// �ϐ����̖�����WS�̓O���[�o�����W�̈Ӗ�

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


    // ���`�[�x�i���C�g View ��� z �� near..far �Ő��K���j
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
    // �J�������猩���e�s�N�Z���̃��[���h���W�̈ʒu��
    // �����A�����̈ʒu���猩����A�[�x�͂�����H�A�����߂�
    //---------------------------------------------------------
    float4 vPosLightView = mul(float4(inWorldPos, 1.0f), g_matLightView);

    float  fDepthLightView = (vPosLightView.z - g_lightNear) / (g_lightFar - g_lightNear);
    fDepthLightView = saturate(fDepthLightView);

    //---------------------------------------------------------
    // �J�������猩���e�s�N�Z���̃��[���h���W�̈ʒu��
    // �����A�����̈ʒu���猩����AUV���W�͉��H�A�����߂�
    //---------------------------------------------------------
    float4 vClipLightView = mul(float4(inWorldPos, 1.0f), g_matLightViewProj);

    // ���C�g���猩�Ĕw�ʁiw <= 0�j�́u�e�Ȃ��v����
    if (vClipLightView.w <= 0)
    {
        outColor.a = 0.0f;
        return;
    }

    // 2D���ʂ�-1 ~ +1�͈̔͂ɐ��K�����������W���擾����
    float2 uvNormalizedView   = vClipLightView.xy / vClipLightView.w;                // [-1,1]

    // -1 ~ +1 �Ȃ̂�UV�摜�ɍ��킹�邽�߂� 0 ~ 1 �ɒ��߂���
    float2 uvLightView   = uvNormalizedView * float2(0.5f, -0.5f) + 0.5f;  // [0,1]

    // DX9�̔��e�N�Z���␳
    uvLightView += float2(0.5f * g_shadowTexelW, 0.5f * g_shadowTexelH);

    // �x�[�XUV���g�O�Ȃ�u�e�Ȃ��v
    if (any(uvLightView < 0.0f) || any(uvLightView > 1.0f))
    {
        outColor.a = 0.0f;
        return;
    }

    float shadow = 0.0f;

    if (g_bBlurEnable)
    {
        // �T���v�����O���ꂽ��
        float fShadowSum = 0.0f;

        // 1�e�N�Z���̃I�t�Z�b�g
        float2 uvTexel = float2(g_shadowTexelW, g_shadowTexelH);

        int nHalfSize = g_nBlurSize / 2;

        // ��ł��邱��
        const int SIZE_MAX = 13;

        // �{�J�V�̃��x���𒲐߂���
        // HLSL�ł�for���̊J�n�E�I�������ɒ萔�����g���Ȃ��̂ł�����Ƃ������׍H���K�v
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

                // �O��UV�́u�e�Ȃ��v= 0 �Ƃ��Đ����Ȃ��i= �T���v���l 0 �����j
                if (any(uvS < 0.0f) || any(uvS > 1.0f))
                {
                    // �������Ȃ��i0���Z�j
                }
                else
                {
                    // tex2D�ł͂Ȃ�tex2Dlod���g��Ȃ��Ă͂����Ȃ��B�������Ȃ��Ɠ����Ȃ�
                    float depthLightSpace = tex2Dlod(samplerLightZ, float4(uvS, 0, 0)).r;

                    // ��r�i���C�g������������Ήe�j
                    if (depthLightSpace < (fDepthLightView - g_shadowBias))
                    {
                        fShadowSum += 1.0f;
                    }
                }
            }
        }

        // 25�T���v���̕��ρi0..1�j
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

// 2���̉摜����`��Ԃō�������
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


