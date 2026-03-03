class CCounter {
  I64 count;

  U0 Init()
  {
    this.count = 0;
  }

  U0 Increment()
  {
    this.count = this.count + 1;
  }

  U0 Add(I64 n)
  {
    this.count = this.count + n;
  }

  I64 Get()
  {
    return this.count;
  }
}

U0 Main()
{
  CCounter c;
  c.count = 0;
  c.Increment();
  c.Increment();
  c.Increment();
  Print("%d\n", c.Get());

  c.Add(10);
  Print("%d\n", c.Get());
}
