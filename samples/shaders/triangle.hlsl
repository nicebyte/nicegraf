#include "generic-frag-shader-input.hlsl"

GenericFragShaderInput TriangleVertex(uint vertexId, float scale, float2 offset, float depth) {
  float4 pos[] = {
    float4(-1.0,  1.0, 0.0, 1.0),
    float4( 3.0,  1.0, 0.0, 1.0),
    float4(-1.0, -3.0, 0.0, 1.0)
  };
  const float2 texcoords[] = {
    float2(0.0, 1.0), float2(2.0, 1.0), float2(0.0, -1.0)
  };
  const float4 colors[] = {
    float4(1.0, 0.0, 0.0, 1.0),
    float4(0.0, 1.0, 0.0, 1.0),
    float4(0.0, 0.0, 1.0, 1.0)
  };
  GenericFragShaderInput triangleVertexData;
  vertexId = vertexId % 3;
  triangleVertexData.position = float4(pos[vertexId].xyz * scale, 1.0) + float4(offset, depth, 0.0);
#if defined(GENERIC_FS_INPUT_HAS_UV)  
  triangleVertexData.textureUv = texcoords[vertexId];
#endif

#if defined(GENERIC_FS_INPUT_HAS_COLOR)  
  triangleVertexData.color    = colors[vertexId];
#endif

#if defined(GENERIC_FS_INPUT_HAS_CLIPSPACE_POS)
  triangleVertexData.clipSpacePosition = triangleVertexData.position;
#endif
  return triangleVertexData;
}