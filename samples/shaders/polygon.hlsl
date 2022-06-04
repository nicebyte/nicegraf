//T: polygon ps:PSMain vs:VSMain

#define GENERIC_FS_INPUT_HAS_COLOR
#include "generic-frag-shader-input.hlsl"

struct VertShaderUniforms {
  float scaleA;
  float scaleB;
  float time;
  float aspectRatio;
  float theta;
};


[[vk::binding(0, 0)]] ConstantBuffer<VertShaderUniforms> vertShaderUniforms;

float4 PSMain(GenericFragShaderInput vertexAttribs) : SV_Target {
  return vertexAttribs.color;
}

GenericFragShaderInput VSMain(uint vertexId : SV_VertexID) {
  GenericFragShaderInput polygonVertexData;
  if (vertexId % 3 == 0) {
    polygonVertexData.position = float4(0.0, 0.0, 0.0, 1.0);
    polygonVertexData.color = float4(0.8, 0.7, 0.8, 1.0);
  } else {
    float    rotationAngle = vertShaderUniforms.time;
    float2x2 rotationMatrix = {
      cos(rotationAngle), -sin(rotationAngle),
      sin(rotationAngle), cos(rotationAngle)
    };
    float effectiveScale = (vertexId % 2  ? vertShaderUniforms.scaleB : vertShaderUniforms.scaleA);
    int outerVertexId = int(round(float(vertexId)/3.0));
    float theta = vertShaderUniforms.theta;
    float2 vertexPosition = mul(rotationMatrix,
                                float2(sin(outerVertexId * theta),
                                       cos(outerVertexId * theta))) * float2(1.0, vertShaderUniforms.aspectRatio)
                                                                    * effectiveScale;
    polygonVertexData.position = float4(vertexPosition, 0.0, 1.0);
    polygonVertexData.color = float4(0.5 * (vertexPosition.x + 1.0), 0.5 * (vertexPosition.y + 1.0), abs(1.0 - vertexPosition.x), 1.0);
    polygonVertexData.position.y *= -1.0;
  }
  return polygonVertexData;
}
