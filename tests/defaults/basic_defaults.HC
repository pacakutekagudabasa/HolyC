I64 Add(I64 a, I64 b = 100)
{
  return a + b;
}

F64 Scale(F64 x, F64 factor = 2.0)
{
  return x * factor;
}

U0 Main()
{
  Print("%d\n", Add(5, 3));
  Print("%d\n", Add(5));
  Print("%d\n", Add(10, 20));
  Print("%.1f\n", Scale(3.0));
  Print("%.1f\n", Scale(3.0, 4.0));
}
