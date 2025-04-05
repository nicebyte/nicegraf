//T: textured-quad ps:PSMain vs:VSMain define:GENERIC_FS_INPUT_HAS_UV=1
//T: textured-quad-image-array ps:PSMain vs:VSMain define:GENERIC_FS_INPUT_HAS_UV=1 define:USE_IMAGE_ARRAY=1
//T: textured-quad-multiple-images ps:PSMain vs:VSMain define:GENERIC_FS_INPUT_HAS_UV=1 define:NUM_IMAGES=4

#include "generic-frag-shader-input.hlsl"

struct ShaderUniforms {
  float4x4 transformMatrix;
#if defined(USE_IMAGE_ARRAY)
  float imageArrayIdx;
#endif
#if defined(NUM_IMAGES)
  uint imageIdx;
#endif
};

[[vk::binding(0, 0)]] ConstantBuffer<ShaderUniforms> shaderUniforms;

GenericFragShaderInput VSMain(uint vertexId : SV_VertexID) {
  const float2 vertices[] = {
    float2(1.0, -1.0), float2(-1.0, -1.0), float2(1.0, 1.0),
    float2(1.0, 1.0), float2(-1.0, -1.0), float2(-1.0, 1.0)
  };
  const float2 uvs[] = {
    float2(1.0, 1.0), float2(0.0, 1.0), float2(1.0, 0.0),
    float2(1.0, 0.0), float2(0.0, 1.0), float2(0.0, 0.0)
  };
  vertexId = vertexId % 6;
  GenericFragShaderInput result = {
    mul(shaderUniforms.transformMatrix,
        float4(vertices[vertexId], 0.0, 1.0)),
    2 * uvs[vertexId]
  };
  return result;
}

#if defined(USE_IMAGE_ARRAY)
#define TEXTURE_IMAGE_TYPE Texture2DArray
#else
#define TEXTURE_IMAGE_TYPE Texture2D
#endif

#if !defined(NUM_IMAGES)
#define NUM_IMAGES (1)
#endif

[[vk::binding(0, 1)]] uniform TEXTURE_IMAGE_TYPE textureImage[NUM_IMAGES];
[[vk::binding(1, 0)]] uniform sampler imageSampler;

float4 PSMain(GenericFragShaderInput vertexAttribs) : SV_Target {
#if defined(USE_IMAGE_ARRAY)
  float3 sampleCoords = float3(vertexAttribs.textureUv, shaderUniforms.imageArrayIdx);
#else
  float2 sampleCoords = vertexAttribs.textureUv;
#endif
#if NUM_IMAGES > 1
  uint i = shaderUniforms.imageIdx % NUM_IMAGES;
#else
  uint i = 0u;
#endif
  return textureImage[i].Sample(imageSampler, sampleCoords);
}
