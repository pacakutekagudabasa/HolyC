I64 Counter()
{
  static I64 count = 0;
  count++;
  return count;
}

U0 Main()
{
  "%d\n", Counter();
  "%d\n", Counter();
  "%d\n", Counter();
}
