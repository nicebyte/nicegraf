//T: blinn-phong vs:VSMain ps:PSMain

struct PixelShaderInput {
  float4 clipSpacePosition : SV_Position;
  float4 viewSpaceInterpNormal : ATTR0;
  float4 viewSpacePosition : ATTR1;
};

struct VertexShaderInput {
  float3 objSpacePosition : SV_Position;
  float3 objSpaceNormal : ATTR0;
};

struct ShaderUniforms {
  float4x4 objToViewTransform;
  float4x4 viewToClipTransform;
  float4   ambientLightIntensity;
  float4   viewSpacePointLightPosition;
  float4   pointLightIntensity;
  float4   viewSpaceDirectionalLightDirection;
  float4   directionalLightIntensity;
  float4   diffuseReflectance;
  float4   specularCoefficient;
  float    shininess;
};

[[vk::binding(0, 0)]] ConstantBuffer<ShaderUniforms> shaderUniforms;

PixelShaderInput VSMain(VertexShaderInput vertexAttrs) {
  float4 viewSpacePosition = mul(shaderUniforms.objToViewTransform, float4(vertexAttrs.objSpacePosition, 1.0));
  float4 viewSpaceNormal =  normalize(mul(shaderUniforms.objToViewTransform, float4(vertexAttrs.objSpaceNormal, 0.0))); // TODO inverse transpose.
  float4 clipSpacePosition = mul(shaderUniforms.viewToClipTransform, viewSpacePosition);
   PixelShaderInput result = {
    clipSpacePosition,
    viewSpaceNormal,
    viewSpacePosition,
  };
  return result;
}

float3 computeIrradiance(float3 intensity, float3 direction, float3 normal, float distSquared) {
  float cosineFactor = max(0.0, dot(direction, normal));
  return intensity * cosineFactor / distSquared; 
}

float3 computeSpecular(float3 position, float3 lightDirection, float3 normal, float shininess) {
  float3 directionToObserver = normalize(-position);
  float3 halfwayVector = normalize(directionToObserver + lightDirection);
  return pow(max(0.0, dot(normal, halfwayVector)), shininess);
}

float4 PSMain(PixelShaderInput fragmentAttribs) : SV_Target {
    float4 viewSpaceNormal = normalize(fragmentAttribs.viewSpaceInterpNormal);
    float4 viewSpaceVectorToPointLight = shaderUniforms.viewSpacePointLightPosition - fragmentAttribs.viewSpacePosition;
    float distanceToPointLightSquared = dot(viewSpaceVectorToPointLight, viewSpaceVectorToPointLight);
    float4 viewSpaceDirectionToPointLight = normalize(viewSpaceVectorToPointLight);
    float3 pointLightIrradiance =
      computeIrradiance(
        shaderUniforms.pointLightIntensity.rgb,
        viewSpaceDirectionToPointLight.xyz,
        viewSpaceNormal.xyz,
        distanceToPointLightSquared);
    float3 directionalLightIrradiance =
      computeIrradiance(
        shaderUniforms.directionalLightIntensity.rgb,
        normalize(shaderUniforms.viewSpaceDirectionalLightDirection),
        viewSpaceNormal,
        1.0f);
    float3 specularReflectanceFromPointLight =
      shaderUniforms.specularCoefficient * 
      computeSpecular(
        fragmentAttribs.viewSpacePosition.xyz,
        viewSpaceDirectionToPointLight.xyz,
        viewSpaceNormal,
        shaderUniforms.shininess); 
    float3 specularReflectanceFromDirectionalLight =
      shaderUniforms.specularCoefficient * 
      computeSpecular(
        fragmentAttribs.viewSpacePosition.xyz,
        normalize(shaderUniforms.viewSpaceDirectionalLightDirection),
        viewSpaceNormal,
        shaderUniforms.shininess); 
        
    float3 pointLightContribution = (shaderUniforms.diffuseReflectance + specularReflectanceFromPointLight) * pointLightIrradiance;
    float3 directionalLightContribution = (shaderUniforms.diffuseReflectance + specularReflectanceFromDirectionalLight) * directionalLightIrradiance;
    
    return float4(pointLightContribution + directionalLightContribution + shaderUniforms.ambientLightIntensity, 1.0);
}