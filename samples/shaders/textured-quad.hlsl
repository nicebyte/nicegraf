//T: textured-quad ps:PSMain vs:VSMain

struct PixelShaderInput {
  float4 clip_pos : SV_POSITION;
  float2 uv : ATTR0;
};

[[vk::binding(0, 0)]] cbuffer UniformData {
  float4x4 u_TransformMatrix;
};

PixelShaderInput VSMain(uint vid : SV_VertexID) {
  const float2 vertices[] = {
    float2(1.0, 1.0), float2(-1.0, 1.0), float2(1.0, -1.0),
    float2(1.0, -1.0), float2(-1.0, 1.0), float2(-1.0, -1.0)
  };
  const float2 uvs[] = {
    float2(1.0, 1.0), float2(0.0, 1.0), float2(1.0, 0.0),
    float2(1.0, 0.0), float2(0.0, 1.0), float2(0.0, 0.0)
  };
  PixelShaderInput result = {
    mul(u_TransformMatrix, float4(vertices[vid % 6], 0.0, 1.0)),
    2 * uvs[vid %6]
  };
  return result;
}

[[vk::binding(0, 1)]] uniform Texture2D tex;
[[vk::binding(1, 0)]] uniform sampler   smp;

float4 PSMain(PixelShaderInput input) : SV_TARGET {
  return tex.Sample(smp, input.uv);
}