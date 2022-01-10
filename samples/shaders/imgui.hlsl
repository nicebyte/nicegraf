//T: imgui ps:PSMain vs:VSMain

#define GENERIC_FS_INPUT_HAS_UV
#define GENERIC_FS_INPUT_HAS_COLOR
#include "generic-frag-shader-input.hlsl"

struct ImGuiVSInput {
  float2 position : ATTR0;
  float2 uv : TEXCOORD0;
  float4 color : COLOR0;
};

struct VertShaderUniforms {
  float4x4 projectionTransform;
};

[[vk::binding(0, 0)]] ConstantBuffer<VertShaderUniforms> vertShaderUniforms;

GenericFragShaderInput VSMain(ImGuiVSInput input) {
  GenericFragShaderInput vertexData = {
    mul(vertShaderUniforms.projectionTransform,
        float4(input.position, 0.0, 1.0)),
    input.color,
    input.uv
  };
  return vertexData;
}

[[vk::binding(1, 0)]] uniform Texture2D textureImage;
[[vk::binding(2, 0)]] uniform sampler imageSampler;

float4 PSMain(GenericFragShaderInput vertexAttribs) : SV_Target {
  return vertexAttribs.color * textureImage.Sample(imageSampler, vertexAttribs.textureUv);
}
