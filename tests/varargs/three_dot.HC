// Test करो कि ... (three-dot) syntax vararg marker की तरह काम करता है
I64 Sum(I64 n, ...) {
  I64 total = 0;
  I64 i;
  for (i = 0; i < n; i++) total = total + __vararg_get(i);
  return total;
}
U0 Main() { "%d\n", Sum(3, 10, 20, 30); }
