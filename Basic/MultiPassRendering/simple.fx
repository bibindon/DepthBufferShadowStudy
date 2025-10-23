
// �Œ���̕`����s���G�t�F�N�g
// �}���`�����_�\�^�[�Q�b�g�Ő[�x�摜��`�悵�Ă��邪�A����͎g���Ȃ��B

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

    // �ȒP�ȃ��C�e�B���O
    float lightIntensity = 0.f;

    // ���s�����ɂ�郉�C�e�B���O����or�Ȃ�
    // �[�x�o�b�t�@�V���h�E��\��������A
    // �����o�[�g�g�U�Ɩ����f���̉e�͗]�v�Ȃ̂ŏ������ق���������������Ȃ��B
    // �������̓n�[�t�����o�[�g�ɂ��邩�B
    // �n�[�t�����o�[�g�͈����Ȃ������ڂ̂悤�Ɋ�����B
    if (false)
    {
        lightIntensity = dot(inNormal, normalize(g_lightDir.xyz));

        // �n�[�t�����o�[�g
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

    // 0..1�i��=0, ��=1�j
    float depthNdc = clipPosition.z / clipPosition.w;
    outDepth01 = saturate(depthNdc);
}

// �s�N�Z���V�F�[�_�[
// COLOR1�ɃO���[�X�P�[���Ő[�x����������
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

    // �߂��قǍ��A�����قǔ�
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

