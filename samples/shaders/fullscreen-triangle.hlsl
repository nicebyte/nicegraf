//T: fullscreen-triangle ps:PSMain vs:VSMain
//T: small-triangle ps:PSMain vs:VSMain define:SCALE=0.25

#define GENERIC_FS_INPUT_HAS_COLOR
#include "triangle.hlsl"

#ifndef SCALE
#define SCALE 1.0
#endif

float4 PSMain(GenericFragShaderInput vertexAttribs) : SV_TARGET {
  return vertexAttribs.color;
}

GenericFragShaderInput VSMain(uint vertexId : SV_VertexID) {
  return TriangleVertex(vertexId, SCALE, 0.0, 0.0);
}
