#include "stdlib/vector.HC"

U0 Main()
{
  CVec v;
  VecInit(&v);

  VecPush(&v, 10);
  VecPush(&v, 20);
  VecPush(&v, 30);

  Print("len=%d\n", VecLen(&v));
  Print("get[0]=%d\n", VecGet(&v, 0));
  Print("get[1]=%d\n", VecGet(&v, 1));
  Print("get[2]=%d\n", VecGet(&v, 2));

  I64 top = VecPop(&v);
  Print("pop=%d len=%d\n", top, VecLen(&v));

  VecFree(&v);
}
