//T: polygon ps:PSMain vs:VSMain

#define GENERIC_FS_INPUT_HAS_COLOR
#include "generic-frag-shader-input.hlsl"

[[vk::binding(0, 0)]] cbuffer Uniforms {
  float u_ScaleA;
  float u_ScaleB;
  float u_Time;
  float u_AspectRatio;
  float u_Theta;
};

float4 PSMain(GenericFragShaderInput vertexAttribs) : SV_Target {
  return vertexAttribs.color;
}

GenericFragShaderInput VSMain(uint vertexId : SV_VertexID) {
  GenericFragShaderInput polygonVertexData;
  if (vertexId % 3 == 0) {
    polygonVertexData.position = float4(0.0, 0.0, 0.0, 1.0);
    polygonVertexData.color = float4(0.8, 0.7, 0.8, 1.0);
  } else {
    float    rotationAngle = u_Time;
    float2x2 rotationMatrix = {
      cos(rotationAngle), -sin(rotationAngle), 
      sin(rotationAngle), cos(rotationAngle)
    };
    float effectiveScale = (vertexId % 2  ? u_ScaleB : u_ScaleA);
    int outerVertexId = int(round(float(vertexId)/3.0));
    float2 vertexPosition = mul(rotationMatrix,
                                float2(sin(outerVertexId * u_Theta),
                                       cos(outerVertexId * u_Theta))) * float2(1.0, u_AspectRatio)
                                                                      * effectiveScale;
    polygonVertexData.position = float4(vertexPosition, 0.0, 1.0);
    polygonVertexData.color = float4(0.5 * (vertexPosition.x + 1.0), 0.5 * (vertexPosition.y + 1.0), abs(1.0 - vertexPosition.x), 1.0);
  }
  return polygonVertexData;
}
