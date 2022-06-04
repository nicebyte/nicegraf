//T: instancing ps:PSMain vs:VSMain

#include "quat.hlsl"

#define GENERIC_FS_INPUT_HAS_UV
#include "generic-frag-shader-input.hlsl"

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

GenericFragShaderInput VSMain(VertexShaderInput vertexAttrs, int instanceIdx : SV_InstanceID) {
  float4 worldSpaceTranslation = float4(perInstanceData.Load(instanceIdx), 0.0);
  const float oscillationFrequency = 5.0;
  float  oscillationPhase = worldSpaceTranslation.x * worldSpaceTranslation.y;
  float4 oscillationOffset = float4(0.0, sin(oscillationFrequency * (shaderUniforms.timestamp + oscillationPhase)), 0.0, 0.0);
  float4 rotationQuat = quatFromAxisAngle(worldSpaceTranslation.xyz, shaderUniforms.timestamp);
  float4 worldSpacePosition = rotateByQuat(float4(vertexAttrs.objSpacePosition, 1.0), rotationQuat) +
                              worldSpaceTranslation +
                              oscillationOffset;
  float4 clipSpacePosition = mul(shaderUniforms.worldToClipTransform, worldSpacePosition);
  clipSpacePosition.y *= -1.0;
  GenericFragShaderInput result = {
    clipSpacePosition,
    vertexAttrs.textureUv
  };
  return result;
}

[[vk::binding(2, 0)]] uniform Texture2D modelTexture;
[[vk::binding(3, 0)]] uniform sampler textureSampler;

float4 PSMain(GenericFragShaderInput fragmentAttribs) : SV_Target {
    return modelTexture.Sample(textureSampler, fragmentAttribs.textureUv);
}