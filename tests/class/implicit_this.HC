class Counter
{
  I64 n;
  U0 Inc() { n++; }
  I64 Get() { return n; }
}

U0 Main()
{
  Counter c;
  c.n = 0;
  c.Inc();
  c.Inc();
  "%d\n", c.Get();
}
