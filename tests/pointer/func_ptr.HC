I64 Add(I64 a, I64 b) { return a + b; }
U0 Main() {
  I64 (*fp)(I64, I64) = &Add;
  "%d\n", fp(3, 4);
}
