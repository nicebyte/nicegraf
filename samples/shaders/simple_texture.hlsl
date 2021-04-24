// T: simple-texture vs:VSMain ps:PSMain

#include "triangle.hlsl"

[[vk::binding(1, 0)]] uniform Texture2D tex;
[[vk::binding(2, 0)]] uniform sampler samp;

float4 PSMain(Triangle_PSInput ps_in) : SV_TARGET {
  return tex.Sample(samp, ps_in.texcoord);
}

Triangle_PSInput VSMain(uint vid : SV_VertexID) {
  return Triangle(vid, 1.0, 0.0, 0.0);
}
