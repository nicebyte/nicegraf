//T: textured-quad ps:PSMain vs:VSMain

#define GENERIC_FS_INPUT_HAS_UV
#include "generic-frag-shader-input.hlsl"

[[vk::binding(0, 0)]] cbuffer UniformData {
  float4x4 u_TransformMatrix;
};

GenericFragShaderInput VSMain(uint vertexId : SV_VertexID) {
  const float2 vertices[] = {
    float2(1.0, 1.0), float2(-1.0, 1.0), float2(1.0, -1.0),
    float2(1.0, -1.0), float2(-1.0, 1.0), float2(-1.0, -1.0)
  };
  const float2 uvs[] = {
    float2(1.0, 1.0), float2(0.0, 1.0), float2(1.0, 0.0),
    float2(1.0, 0.0), float2(0.0, 1.0), float2(0.0, 0.0)
  };
  vertexId = vertexId % 6;
  GenericFragShaderInput result = {
    mul(u_TransformMatrix, float4(vertices[vertexId], 0.0, 1.0)),
    2 * uvs[vertexId]
  };
  return result;
}

[[vk::binding(0, 1)]] uniform Texture2D textureImage;
[[vk::binding(1, 0)]] uniform sampler   imageSampler;

float4 PSMain(GenericFragShaderInput vertexAttribs) : SV_Target {
  return textureImage.Sample(imageSampler, vertexAttribs.textureUv);
}