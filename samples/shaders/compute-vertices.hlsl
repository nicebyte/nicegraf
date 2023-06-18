struct VertexData {
  float3 position;
  float3 normal;
};

struct VertexOutput {
  float4 position : SV_Position;
  float  height : ATTR0;
};

// T: render-vertices vs:VSMain ps:PSMain

struct VertexShaderUniforms {
  float4x4 objToViewTransform;
  float4x4 viewToClipTransform;
};

[[vk::binding(0, 0)]] ConstantBuffer<VertexShaderUniforms> vertShaderUniforms;

#define maxAmplitude 0.05

VertexOutput VSMain(uint vertID : SV_VertexID, float4 pos : SV_Position) {
  VertexOutput result;
  result.height = pos.y;
  result.position =
      mul(vertShaderUniforms.viewToClipTransform,
          mul(vertShaderUniforms.objToViewTransform, float4(pos.xyz, 1.0)));
  return result;
}

float4 PSMain(VertexOutput pxIn) : SV_Target {
  float shade = saturate(-(pxIn.height / (maxAmplitude + 0.04)) * 0.5 + 0.5);
  shade       = shade * shade;
  return shade * float4(1., 1., 1.0, 1.0);
}

// T: compute-vertices cs:CSMain

struct ComputeShaderUniforms {
  float4 time;
};

[[vk::binding(0, 1)]] RWStructuredBuffer<float4>            outputBuffer;
[[vk::binding(1, 1)]] ConstantBuffer<ComputeShaderUniforms> computeShaderUniforms;

[numthreads(2, 2, 1)] void CSMain(uint3 tid
                                  : SV_DispatchThreadID) {
  const uint vertsPerSide  = 512;
  uint       vertID        = tid.y * vertsPerSide + tid.x;
  uint2      vertRowColumn = tid.xy;
  float2     vertUV        = float2(
      (float)vertRowColumn.x / (float)(vertsPerSide - 1),
      (float)vertRowColumn.y / (float)(vertsPerSide - 1));
  float2 vertXZ = vertUV * 2.0 - float2(1.0, 1.0);
  float  height = maxAmplitude *
      sin(cos(computeShaderUniforms.time.x * 2.0 + vertRowColumn.x * 0.1) + vertRowColumn.y * 0.1);
  float4 position      = float4(vertXZ.x, height, vertXZ.y, 1.0);
  outputBuffer[vertID] = position;
}