//T: imgui ps:PSMain vs:VSMain

struct ImGuiVSInput {
  float2 position : ATTR0;
  float2 uv : TEXCOORD0;
  float4 color : COLOR0;
};

struct ImGuiPSInput {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
  float4 color : COLOR0;
};

[[vk::binding(0, 0)]] cbuffer MatUniformBuffer : register(b0){
  float4x4 u_Projection;
}

[[vk::binding(1, 0)]] uniform Texture2D u_Texture;
[[vk::binding(2, 0)]] uniform sampler u_Sampler;

ImGuiPSInput VSMain(ImGuiVSInput input) {
  ImGuiPSInput output = {
    mul(u_Projection, float4(input.position, 0.0, 1.0)),
    input.uv,
    input.color
  };
  return output;
}

float4 PSMain(ImGuiPSInput input) : SV_TARGET {
  return input.color * u_Texture.Sample(u_Sampler, input.uv);
}
