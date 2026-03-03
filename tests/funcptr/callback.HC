I64 Apply(I64 fp, I64 x, I64 y)
{
  return fp(x, y);
}

I64 Sub(I64 a, I64 b)
{
  return a - b;
}

I64 Div(I64 a, I64 b)
{
  return a / b;
}

U0 Main()
{
  Print("%d\n", Apply(&Sub, 10, 3));
  Print("%d\n", Apply(&Div, 20, 4));
}
