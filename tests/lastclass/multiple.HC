class A
{
  I64 x;
};

class B
{
  I64 a;
  I64 b;
};

U0 Main()
{
  // B declare होने के बाद, lastclass = B होता है
  // B में b का offset = 8 होना चाहिए
  Print("%d\n", offset(lastclass, b));
}
