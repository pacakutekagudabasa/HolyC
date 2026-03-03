I64 Add(I64 a, I64 b)
{
  return a + b;
}

I64 Mul(I64 a, I64 b)
{
  return a * b;
}

U0 Main()
{
  I64 fp = &Add;
  Print("%d\n", fp(3, 4));
  fp = &Mul;
  Print("%d\n", fp(3, 4));
}
