#include "stdlib/set.HC"

U0 Main()
{
  SetNew();
  SetAdd(10);
  SetAdd(20);
  SetAdd(10);
  "%d\n", SetHas(10);
  "%d\n", SetHas(20);
  "%d\n", SetHas(30);
  SetRemove(10);
  "%d\n", SetHas(10);
  SetFree();
}
