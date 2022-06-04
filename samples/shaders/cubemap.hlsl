// T: cubemap vs:VSMain ps:PSMain define:GENERIC_FS_INPUT_HAS_CLIPSPACE_POS=1
// T: cubemap-array vs:VSMain ps:PSMain define:GENERIC_FS_INPUT_HAS_CLIPSPACE_POS=1 define:USE_CUBEMAP_ARRAY=1

#include "triangle.hlsl"

struct ShaderUniforms {
  float4x4 cameraTransform;
  float    aspectRatio;
#if defined(USE_CUBEMAP_ARRAY)
  float    cubemapArrayIndex;
#endif
};

#if defined(USE_CUBEMAP_ARRAY)
#define TEXTURE_IMAGE_TYPE TextureCubeArray
#else
#define TEXTURE_IMAGE_TYPE TextureCube
#endif

[[vk::binding(0, 0)]] ConstantBuffer<ShaderUniforms> shaderUniforms;
[[vk::binding(1, 0)]] uniform TEXTURE_IMAGE_TYPE cubemapImage;
[[vk::binding(2, 0)]] uniform sampler imageSampler;

float4 PSMain(GenericFragShaderInput vertexAttribs) : SV_Target {
  float3 direction = -mul(shaderUniforms.cameraTransform,
                          float4(vertexAttribs.clipSpacePosition.x * shaderUniforms.aspectRatio,
                                 vertexAttribs.clipSpacePosition.y,
                                 1.0,
                                 0.0)).xyz;
#if defined(USE_CUBEMAP_ARRAY)
  float4 cubemapSampleCoords = float4(direction, shaderUniforms.cubemapArrayIndex);
#else
  float3 cubemapSampleCoords = direction;
#endif
  return cubemapImage.Sample(imageSampler, cubemapSampleCoords);
}

GenericFragShaderInput VSMain(uint vertexId : SV_VertexID) {
  return TriangleVertex(vertexId, 1.0, 0.0, 0.0);
}
