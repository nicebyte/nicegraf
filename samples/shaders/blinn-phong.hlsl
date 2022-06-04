//T: blinn-phong vs:VSMain ps:PSMain

[[vk::constant_id(0)]] const uint enableHalfLambert = 0;

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
  clipSpacePosition.y *= -1.0;
   PixelShaderInput result = {
    clipSpacePosition,
    viewSpaceNormal,
    viewSpacePosition,
  };
  return result;
}

float computeCosineFactor(float3 direction, float3 normal) {
  float cosineFactor = dot(direction, normal);
  if (enableHalfLambert == 0) {
    return max(0.0, cosineFactor);
  } else {
    cosineFactor = 0.5 * cosineFactor + 0.5;
    cosineFactor *= cosineFactor;
    return cosineFactor;
  }
}

float3 computeIrradiance(float3 intensity, float3 direction, float3 normal, float distSquared) {
  return intensity * computeCosineFactor(direction, normal) / distSquared; 
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
        normalize(shaderUniforms.viewSpaceDirectionalLightDirection.xyz),
        viewSpaceNormal.xyz,
        1.0f);
    float3 specularReflectanceFromPointLight =
      shaderUniforms.specularCoefficient.rgb *
      computeSpecular(
        fragmentAttribs.viewSpacePosition.xyz,
        viewSpaceDirectionToPointLight.xyz,
        viewSpaceNormal.xyz,
        shaderUniforms.shininess); 
    float3 specularReflectanceFromDirectionalLight =
      shaderUniforms.specularCoefficient.rgb *
      computeSpecular(
        fragmentAttribs.viewSpacePosition.xyz,
        normalize(shaderUniforms.viewSpaceDirectionalLightDirection.xyz),
        viewSpaceNormal.xyz,
        shaderUniforms.shininess); 
        
    float3 pointLightContribution = (shaderUniforms.diffuseReflectance.rgb + specularReflectanceFromPointLight) * pointLightIrradiance;
    float3 directionalLightContribution = (shaderUniforms.diffuseReflectance.rgb + specularReflectanceFromDirectionalLight) * directionalLightIrradiance;
    
    return float4(pointLightContribution + directionalLightContribution + shaderUniforms.ambientLightIntensity.rgb, 1.0);
}
