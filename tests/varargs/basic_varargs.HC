I64 FirstArg(I64 a, ..)
{
  return a;
}

U0 Main()
{
  Print("%d\n", FirstArg(42));
  Print("%d\n", FirstArg(10, 20, 30));
}
