//T: polygon ps:PSMain vs:VSMain

[[vk::binding(0, 0)]] cbuffer Uniforms {
  float u_ScaleA;
  float u_ScaleB;
  float u_Time;
  float u_AspectRatio;
  float u_Theta;
};

struct PSInput {
  float4 pos : SV_POSITION;
  float4 color : COLOR0;
};

float4 PSMain(PSInput inp) : SV_TARGET {
  return inp.color;
}

PSInput VSMain(uint vid : SV_VertexID) {
  PSInput res;
  if (vid % 3 == 0) {
    res.pos = float4(0.0, 0.0, 0.0, 1.0);
    res.color = float4(0.8, 0.7, 0.8, 1.0);
  } else {
    float    rotation_angle = u_Time;
    float2x2 rotation_matrix = {
      cos(rotation_angle), -sin(rotation_angle), 
      sin(rotation_angle), cos(rotation_angle)
    };
    float effective_scale = (vid % 2  ? u_ScaleB : u_ScaleA);
    int outer_vertex_id = int(round(float(vid)/3.0));
    float2 vtx_pos = mul(rotation_matrix,
                         float2(sin(outer_vertex_id * u_Theta),
                                cos(outer_vertex_id * u_Theta))) * float2(1.0, u_AspectRatio)
                                                         * effective_scale;
    res.pos = float4(vtx_pos, 0.0, 1.0);
    res.color = float4(0.5 * (res.pos.x + 1.0), 0.5 * (res.pos.y + 1.0), abs(1.0 - res.pos.x), 1.0);
  }
  return res;
}
