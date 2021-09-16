// Helper functions for quaternions.

float4 quatFromAxisAngle(float3 axis, float angle) {
  float3 n = normalize(axis);
  return float4(sin(angle/2.0) * n, cos(angle/2.0));
}

float4 quatMul(float4 lhs, float4 rhs) {
  const float x1 = lhs[0],
              x2 = rhs[0],
              y1 = lhs[1],
              y2 = rhs[1],
              z1 = lhs[2],
              z2 = rhs[2],
              w1 = lhs[3],
              w2 = rhs[3];

  return float4(x1 * w1 + y1 * z2 - z1 * y2 + x2 * w1,
                y1 * w2 - x1 * z2 + z1 * x2 + y2 * w1,
                x1 * y2 - y1 * x2 + z1 * w2 + z2 * w1,
                w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2);
}

float4 rotateByQuat(float4 a, float4 q) {
  float x = a[0], y = a[1], z = a[2];
  float qx = q[0], qy = q[1], qz = q[2], qw = q[3];

  float ix = qw * x + qy * z - qz * y;
  float iy = qw * y + qz * x - qx * z;
  float iz = qw * z + qx * y - qy * x;
  float iw = -qx * x - qy * y - qz * z;

  return float4(ix * qw + iw * -qx + iy * -qz - iz * -qy,
                iy * qw + iw * -qy + iz * -qx - ix * -qz,
                iz * qw + iw * -qz + ix * -qy - iy * -qx,
                a[3]);
}