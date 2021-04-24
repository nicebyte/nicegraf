//T: fullscreen-triangle ps:PSMain vs:VSMain
//T: small-triangle ps:PSMain vs:VSMain define:SCALE=0.25

#include "triangle.hlsl"

#ifndef SCALE
#define SCALE 1.0
#endif

float4 PSMain(Triangle_PSInput ps_in) : SV_TARGET {
  return ps_in.color;
}

Triangle_PSInput VSMain(uint vid : SV_VertexID) {
  return Triangle(vid, SCALE, 0.0, 0.0);
}
