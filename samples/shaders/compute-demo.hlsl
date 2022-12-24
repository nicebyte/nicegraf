// T: compute-demo cs:CSMain

[[vk::binding(0, 0)]] RWTexture2D<float4> outputImage;

float2 f(float2 x, float2 c) {
  return mul(x, float2x2(x.x, x.y, -x.y, x.x)) + c;
}

float3 palette(float t, float3 a, float3 b, float3 c, float3 d) {
  return a + b * cos(6.28318 * (c * t + d)); /* thanks, iq */
}

[numthreads(4, 4, 1)] void CSMain(uint3 tid
                                  : SV_DispatchThreadID) {
  float2 uv         = float2 ((float)tid.x / 512.0f, (float)tid.y / 512.0f);
  float2 c          = float2(-0.6, 0.0) + (2.0*uv - 1.0);
  float2 x          = float2(0.0, 0.0);
  bool   escaped    = false;
  int    iterations = 0;
  for (int i = 0; i < 50; i++) {
    iterations = i;
    x          = f(x, c);
    if (length(x) > 2.0) {
      escaped = true;
      break;
    }
  }
  outputImage[tid.xy] = (escaped ? float4(
                                               palette(
                                                   float(iterations) / 50.0,
                                                   float3(0.3, 0.2, 0.4),
                                                   float3(0.2, 0.1, 0.0),
                                                   float3(1.0, 1.0, 1.0),
                                                   float3(0.3, 0.5, 0.2)),
                                               1.0)
                                         : float4(0.0, 0.0, 0.0, 1.0));
}
