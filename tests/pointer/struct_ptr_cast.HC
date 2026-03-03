class Point { I64 x; I64 y; }
U0 Main() {
  Point a;
  a.x = 3;
  a.y = 7;
  Point b;
  MemCpy(&b, &a, 16);
  "%d %d\n", b.x, b.y;
}
