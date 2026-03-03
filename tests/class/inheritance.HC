class CBase {
  I64 x;
  I64 y;
}

class CDerived : CBase {
  I64 z;
}

U0 Main()
{
  CDerived d;
  d.x = 1;
  d.y = 2;
  d.z = 3;
  Print("%d\n", d.x);
  Print("%d\n", d.y);
  Print("%d\n", d.z);
}
