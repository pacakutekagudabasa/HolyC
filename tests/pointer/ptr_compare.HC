U0 Main()
{
  I64 a[3];
  I64* p = &a[0];
  I64* q = &a[2];
  if (p < q) "p < q\n";
  if (p == p) "p == p\n";
  if (q > p) "q > p\n";
}
