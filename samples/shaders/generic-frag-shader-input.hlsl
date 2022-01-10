struct GenericFragShaderInput {
  float4 position : SV_Position;
  
#if defined(GENERIC_FS_INPUT_HAS_COLOR)
  float4 color : NGF_COLOR;
#endif

#if defined(GENERIC_FS_INPUT_HAS_CLIPSPACE_POS)
  float4 clipSpacePosition : NGF_CLIP_SPACE_POSITION;
#endif

#if defined(GENERIC_FS_INPUT_HAS_UV)
  float2 textureUv : NGF_UV;
#endif

};