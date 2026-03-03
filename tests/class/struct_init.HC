class Vec3 { F64 x; F64 y; F64 z; }
U0 Main() {
  Vec3 v = {1.0, 2.0, 3.0};
  "%g %g %g\n", v.x, v.y, v.z;
}
