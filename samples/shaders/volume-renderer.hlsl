// T: volume-renderer vs:VSMain ps:PSMain

struct VertexShaderInput {
  float4 position : SV_Position;
  float3 textureCoordinate : TexCoord;
};

struct VolumeRendererUniforms {
  float4x4 transformMatrix;
  float    aspectRatio;
};

[[vk::binding(0,1)]] ConstantBuffer<VolumeRendererUniforms> shaderUniforms;
[[vk::binding(0,0)]] Texture3D volumeImage;

VertexShaderInput VSMain(uint vertexId: SV_VertexID, uint instanceId : SV_InstanceID) {
 const float2 vertices[] = {
    float2(1.0, -1.0), float2(-1.0, -1.0), float2(1.0, 1.0),
    float2(1.0, 1.0), float2(-1.0, -1.0), float2(-1.0, 1.0)
  };
  vertexId = vertexId % 6;
  float w, h, d;
  volumeImage.GetDimensions(w, h, d);
  float3 xyz = float3(vertices[vertexId], 2.0 * (instanceId/d) - 1.0);
  float3 uvw = xyz * float3(1.0, -1.0, 1.0);
  xyz.y *= shaderUniforms.aspectRatio;
  uvw = mul(shaderUniforms.transformMatrix, float4(uvw, 1.0)).xyz;
  uvw.xy = 0.5 * uvw.xy + 0.5;
  VertexShaderInput result = {
    float4(xyz.xy, 0.0, 1.0),
    uvw,
  };
  return result;
}

[[vk::binding(1,0)]] sampler volumeSampler;

float4 PSMain(VertexShaderInput input) : SV_Target {
  float alpha = volumeImage.Sample(volumeSampler, input.textureCoordinate).r;
  return float4(1., 1., 1., alpha);
}