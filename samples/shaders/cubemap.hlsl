// T: cubemap vs:VSMain ps:PSMain

#define GENERIC_FS_INPUT_HAS_CLIPSPACE_POS
#include "triangle.hlsl"

struct ShaderUniforms {
  float4x4 cameraTransform;
  float    aspectRatio;
};

[[vk::binding(0, 0)]] ConstantBuffer<ShaderUniforms> shaderUniforms;
[[vk::binding(1, 0)]] uniform TextureCube cubemapImage;
[[vk::binding(2, 0)]] uniform sampler imageSampler;

float4 PSMain(GenericFragShaderInput vertexAttribs) : SV_Target {
  float3 direction = mul(shaderUniforms.cameraTransform,
                         float4(-vertexAttribs.clipSpacePosition.x * shaderUniforms.aspectRatio,
                                 vertexAttribs.clipSpacePosition.y,
                                -1.0,
                                 0.0)).xyz;
  return cubemapImage.Sample(imageSampler, direction);
}

GenericFragShaderInput VSMain(uint vertexId : SV_VertexID) {
  return TriangleVertex(vertexId, 1.0, 0.0, 0.0);
}
