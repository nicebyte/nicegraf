//T: instancing ps:PSMain vs:VSMain

#include "quat.hlsl"

struct PixelShaderInput {
  float4 clipSpacePosition : SV_Position;
  float2 textureUv : TEXCOORD0;
};

struct VertexShaderInput {
  float3 objSpacePosition : SV_Position;
  float2 textureUv : TEXCOORD0;
};

struct ShaderUniforms {
  float4x4 worldToClipTransform;
  float timestamp;
};

[[vk::binding(0, 0)]] ConstantBuffer<ShaderUniforms> shaderUniforms;
[[vk::binding(1, 0)]] Buffer<float3> perInstanceData;

PixelShaderInput VSMain(VertexShaderInput vertexAttrs, int instanceIdx : SV_InstanceID) {
  float4 worldSpaceTranslation = float4(perInstanceData.Load(instanceIdx), 0.0);
  const float oscillationFrequency = 5.0;
  float  oscillationPhase = worldSpaceTranslation.x * worldSpaceTranslation.y;
  float4 oscillationOffset = float4(0.0, sin(oscillationFrequency * (shaderUniforms.timestamp + oscillationPhase)), 0.0, 0.0);
  float4 rotationQuat = quatFromAxisAngle(worldSpaceTranslation.xyz, shaderUniforms.timestamp);
  float4 worldSpacePosition = rotateByQuat(float4(vertexAttrs.objSpacePosition, 1.0), rotationQuat) +
                              worldSpaceTranslation +
                              oscillationOffset;
  PixelShaderInput result = {
    mul(shaderUniforms.worldToClipTransform, worldSpacePosition),
    vertexAttrs.textureUv
  };
  return result;
}

[[vk::binding(2, 0)]] uniform Texture2D modelTexture;
[[vk::binding(3, 0)]] uniform sampler textureSampler;

float4 PSMain(PixelShaderInput fragmentAttribs) : SV_Target {
    return modelTexture.Sample(textureSampler, fragmentAttribs.textureUv);
}