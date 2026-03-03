U0 Main()
{
  I64 x = 99;
  I64* p = &x;
  I64** pp = &p;
  "%d\n", **pp;
  **pp = 42;
  "%d\n", x;
}
