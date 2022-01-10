// T: simple-texture vs:VSMain ps:PSMain

#define GENERIC_FS_INPUT_HAS_UV
#include "triangle.hlsl"

[[vk::binding(1, 0)]] uniform Texture2D textureImage;
[[vk::binding(2, 0)]] uniform sampler imageSampler;

float4 PSMain(GenericFragShaderInput vertexAttribs) : SV_Target {
  return textureImage.Sample(imageSampler, vertexAttribs.textureUv);
}

GenericFragShaderInput VSMain(uint vertexId : SV_VertexID) {
  return TriangleVertex(vertexId, 1.0, 0.0, 0.0);
}
